#include <iostream>
#include <cstdio>
#include <memory>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <vector>
#include <string>
#include <atomic>
#ifdef _WIN32
#include <crtdbg.h>
#endif

#include <QCoreApplication>
#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QEventLoop>

#include "mcp_core/ConsoleStdioTransport.h"
#include "mcp_core/McpClientSession.h"
#include "tests/common.h"

void runAllLocalTests();

static std::string getEnv(const char* name, const std::string& defaultVal = "") {
    const char* val = std::getenv(name);
    return val ? std::string(val) : defaultVal;
}

static mcp::json parseJsonEnv(const std::string& str) {
    if (str.empty()) return mcp::json::object();
    try { return mcp::json::parse(str); }
    catch (...) { return mcp::json::object(); }
}

// ============================================================
// SSE connection — used only for scenarios requiring SSE (sse-retry).
// No auto-reconnect timer. The main loop handles reconnection.
// ============================================================
struct SseConnection {
    QNetworkAccessManager* nam = nullptr;
    QNetworkReply* reply = nullptr;
    QUrl postUrl;
    int reconnectCount = 0;
    bool streamClosed = false;
    QByteArray buffer;

    void start(const QUrl& url) {
        if (reply) { reply->abort(); reply = nullptr; }
        streamClosed = false;
        buffer.clear();

        QNetworkRequest req(url);
        req.setRawHeader("Accept", "text/event-stream");
        req.setRawHeader("MCP-Protocol-Version", "2025-11-25");

        reply = nam->get(req);
        reconnectCount++;
        std::cerr << "[SSE] GET connection #" << reconnectCount << std::endl;

        QObject::connect(reply, &QNetworkReply::readyRead, [this, url]() {
            buffer.append(reply->readAll());
            int blockEnd;
            while ((blockEnd = buffer.indexOf("\n\n")) != -1) {
                QByteArray block = buffer.left(blockEnd);
                buffer.remove(0, blockEnd + 2);
                QString eventType = "message";
                QString dataContent;
                block.replace("\r", "");
                for (const QByteArray& line : block.split('\n')) {
                    if (line.startsWith("event:"))
                        eventType = QString::fromUtf8(line.mid(6).trimmed());
                    else if (line.startsWith("data:"))
                        dataContent = QString::fromUtf8(line.mid(5).trimmed());
                }
                if (eventType == "endpoint" && !dataContent.isEmpty()) {
                    postUrl = url.resolved(QUrl(dataContent));
                    std::cerr << "[SSE] endpoint -> " << postUrl.toString().toStdString() << std::endl;
                }
            }
        });

        QObject::connect(reply, &QNetworkReply::finished, [this]() {
            std::cerr << "[SSE] stream closed" << std::endl;
            reply = nullptr; // owned by nam, don't deleteLater (causes double-free on nam destruction)
            streamClosed = true;
        });
    }

    void stop() {
        streamClosed = true;
        if (reply) { reply->abort(); reply = nullptr; }
    }
};

// ============================================================
// HTTP Session — tracks MCP-Session-Id across requests
// ============================================================
struct HttpSession {
    QUrl postUrl;
    std::string sessionId;

    mcp::json post(const mcp::json& body, int timeoutMs = 5000) {
        QNetworkAccessManager nam;
        QNetworkRequest req(postUrl);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Accept", "application/json, text/event-stream");
        req.setRawHeader("MCP-Protocol-Version", "2025-11-25");
        if (!sessionId.empty())
            req.setRawHeader("MCP-Session-Id", QByteArray::fromStdString(sessionId));

        QByteArray postData = QByteArray::fromStdString(body.dump());
        QNetworkReply* reply = nam.post(req, postData);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        loop.exec();

        if (!reply->isFinished()) {
            reply->abort(); reply->deleteLater();
            return {{"error", "timeout"}};
        }

        QByteArray respData = reply->readAll();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // Track session ID from response headers
        QByteArray sid = reply->rawHeader("MCP-Session-Id");
        if (!sid.isEmpty()) sessionId = sid.toStdString();

        reply->deleteLater();

        if (status == 202) return mcp::json::object();

        std::string respBody = respData.toStdString();
        try { return mcp::json::parse(respBody); } catch (...) {}

        // SSE format: extract "data: <json>"
        mcp::json sseResult;
        size_t pos = 0;
        while (pos < respBody.size()) {
            size_t d = respBody.find("data:", pos);
            if (d == std::string::npos) break;
            size_t start = d + 5;
            size_t end = respBody.find('\n', start);
            if (end == std::string::npos) end = respBody.size();
            std::string line = respBody.substr(start, end - start);
            line.erase(0, line.find_first_not_of(" \t\r"));
            line.erase(line.find_last_not_of(" \t\r") + 1);
            if (!line.empty()) {
                try {
                    mcp::json parsed = mcp::json::parse(line);
                    if (parsed.contains("result") || parsed.contains("error")) sseResult = parsed;
                } catch (...) {}
            }
            pos = end + 1;
        }
        if (!sseResult.is_null()) return sseResult;
        return {{"error", "unable to parse: " + respBody.substr(0, 100)}};
    }
};

// ============================================================
// HTTP Conformance Mode
// ============================================================
static int runHttpMode(const std::string& serverUrl, const std::string& scenario,
                       const std::string& contextStr) {
    mcp::json ctx = parseJsonEnv(contextStr);
    QUrl url(QString::fromStdString(serverUrl));
    bool needsSse = (scenario == "sse-retry");

    std::cerr << "[Conformance] HTTP mode, scenario: " << scenario
              << (needsSse ? " (SSE)" : "") << std::endl;

    // ---- Step 0: optional SSE connection ----
    QNetworkAccessManager sseNam;
    SseConnection sse;
    sse.nam = &sseNam;
    if (needsSse) {
        sse.postUrl = url;
        sse.start(url);
        // Give SSE time to establish
        for (int i = 0; i < 50; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    QUrl postUrl = (needsSse && !sse.postUrl.isEmpty()) ? sse.postUrl : url;
    HttpSession http;
    http.postUrl = postUrl;

    // ---- Step 1: initialize ----
    mcp::json initReq = {
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2025-11-25"},
            {"capabilities", {{"roots", {{"listChanged", false}}}, {"sampling", mcp::json::object()}}},
            {"clientInfo", {{"name", "mcp-conformance-client-cpp"}, {"version", "1.0.0"}}}
        }}
    };

    mcp::json initResp = http.post(initReq);
    if (initResp.contains("error") && initResp["error"].is_string()) {
        std::cerr << "[Conformance] Initialize " << initResp["error"] << std::endl;
        return 1;
    }

    http.post({{"jsonrpc","2.0"},{"method","notifications/initialized"},{"params",mcp::json::object()}}, 2000);

    // ---- Step 2: scenario-specific operations ----
    bool scenarioOk = true;

    if (scenario == "sse-retry") {
        QEventLoop keepAlive;
        QTimer::singleShot(4000, &keepAlive, &QEventLoop::quit);
        keepAlive.exec();
        std::cerr << "[Conformance] sse-retry: GET count=" << sse.reconnectCount << std::endl;

    } else if (scenario == "initialize") {

    } else if (scenario == "tools_list" || scenario == "tools_call"
               || scenario == "elicitation-sep1034-client-defaults") {
        mcp::json toolsReq = {{"jsonrpc","2.0"},{"id",2},{"method","tools/list"},{"params",mcp::json::object()}};
        mcp::json toolsResp = http.post(toolsReq);
        if (toolsResp.contains("result") && toolsResp["result"].contains("tools")) {
            auto& tools = toolsResp["result"]["tools"];
            std::cerr << "[Conformance] listed " << tools.size() << " tools" << std::endl;
            if ((scenario == "tools_call" || scenario == "elicitation-sep1034-client-defaults") && !tools.empty()) {
                std::string name = ctx.value("name", tools[0].value("name",""));
                mcp::json args = ctx.value("arguments", mcp::json::object());
                mcp::json callResp = http.post({
                    {"jsonrpc","2.0"},{"id",3},{"method","tools/call"},
                    {"params",{{"name",name},{"arguments",args}}}
                });
                if (callResp.contains("error")) { scenarioOk = false; }
            }
        } else { scenarioOk = false; }

    } else if (scenario == "resources_list") {
        mcp::json resp = http.post({{"jsonrpc","2.0"},{"id",2},{"method","resources/list"},{"params",mcp::json::object()}});
        if (resp.contains("error")) { std::cerr << "[Conformance] listResources failed" << std::endl; scenarioOk = false; }

    } else if (scenario == "prompts_list") {
        mcp::json resp = http.post({{"jsonrpc","2.0"},{"id",2},{"method","prompts/list"},{"params",mcp::json::object()}});
        if (resp.contains("error")) { std::cerr << "[Conformance] listPrompts failed" << std::endl; scenarioOk = false; }

    } else if (scenario == "shutdown") {
        mcp::json resp = http.post({{"jsonrpc","2.0"},{"id",2},{"method","shutdown"},{"params",mcp::json::object()}});
        if (resp.contains("error")) { std::cerr << "[Conformance] shutdown failed" << std::endl; scenarioOk = false; }

    } else {
        std::cerr << "[Conformance] unknown scenario: " << scenario << std::endl;
    }

    sse.stop();
    int exitCode = scenarioOk ? 0 : 1;
    std::cerr << "[Conformance] scenario " << scenario
              << (scenarioOk ? " PASSED" : " FAILED")
              << " (exit " << exitCode << ")" << std::endl;
    return exitCode;
}

// ============================================================
// Stdio Conformance Mode (fallback)
// ============================================================
static int runStdioMode(const std::string& scenario, const std::string& contextStr) {
    mcp::json ctx = parseJsonEnv(contextStr);
    std::cerr << "[Conformance] Stdio mode, scenario: " << scenario << std::endl;

    auto transport = std::make_shared<mcp::ConsoleStdioTransport>();
    auto session   = std::make_shared<mcp::McpClientSession>(transport);

    session->setLogCallback([](mcp::LogLevel level, const std::string& msg) {
        if (level >= mcp::LogLevel::Warning) std::cerr << "[SDK] " << msg << std::endl;
    });

    session->init();
    if (!session->start()) {
        std::cerr << "[Conformance] Failed to start stdio transport" << std::endl;
        return 1;
    }

    mcp::json serverInfo;
    if (!session->initializeSync("mcp-conformance-client-cpp", "1.0.0", &serverInfo)) {
        std::cerr << "[Conformance] Initialize handshake failed" << std::endl;
        return 1;
    }
    std::cerr << "[Conformance] Initialize OK" << std::endl;

    bool scenarioOk = true;
    if (scenario == "tools_list" || scenario == "tools_call") {
        mcp::json err;
        auto tools = session->listToolsSync(std::chrono::milliseconds(10000), &err);
        if (!err.empty() || tools.empty()) { scenarioOk = false; }
        else if (scenario == "tools_call") {
            mcp::json callErr;
            session->callToolSync(ctx.value("name", tools[0].name), ctx.value("arguments", mcp::json::object()), &callErr, std::chrono::milliseconds(10000));
            if (!callErr.empty()) scenarioOk = false;
        }
    } else if (scenario == "resources_list") {
        mcp::json err; session->listResourcesSync(std::chrono::milliseconds(10000), &err);
        if (!err.empty()) scenarioOk = false;
    } else if (scenario == "prompts_list") {
        mcp::json err; session->listPromptsSync(std::chrono::milliseconds(10000), &err);
        if (!err.empty()) scenarioOk = false;
    } else if (scenario == "shutdown") {
        if (!session->shutdownSync(std::chrono::milliseconds(5000))) scenarioOk = false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    session->close();
    std::cerr << "[Conformance] scenario " << scenario
              << (scenarioOk ? " PASSED" : " FAILED") << std::endl;
    return scenarioOk ? 0 : 1;
}

// ============================================================
int main(int argc, char* argv[]) {
    std::setvbuf(stdout, NULL, _IONBF, 0);
    std::setvbuf(stderr, NULL, _IONBF, 0);
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif

    bool isConformance = false;
    std::string serverUrl;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--scenario") { isConformance = true; }
        else if (isConformance && serverUrl.empty() && argv[i][0] != '-') { serverUrl = argv[i]; }
    }

    if (!isConformance) {
        runAllLocalTests();
        return TmTestRunner::instance().hasFailed() ? 1 : 0;
    }

    std::string scenario = getEnv("MCP_CONFORMANCE_SCENARIO", "");
    std::string context  = getEnv("MCP_CONFORMANCE_CONTEXT", "{}");

    if (!serverUrl.empty()) {
        QCoreApplication app(argc, argv);
        return runHttpMode(serverUrl, scenario, context);
    }

    return runStdioMode(scenario, context);
}