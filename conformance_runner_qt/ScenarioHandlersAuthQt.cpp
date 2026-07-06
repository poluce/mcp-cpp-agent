#include "RunnerConfig.h"
#include <mcp_qt_client/McpQtClient.h>
#include <mcp_qt_transport/QtHttpSseTransport.h>
#include <mcp_core/McpOAuthClient.h>
#include <QTimer>
#include <iostream>

namespace mcp_conformance {

// ========== 基本场景（McpQtClient / Qt 原生 QNAM）==========

int runInitialize(const RunnerConfig& c) {
    QString err;
    auto cl = mcp_qt::McpQtClient::connectHttpAndWait(QString::fromStdString(c.serverUrl), "mcp-conformance-client-cpp", "1.0.0", 10000, &err);
    if (!cl) {
        std::cerr << "[runInitialize] connectHttpAndWait failed: " << err.toStdString() << std::endl;
        return 1;
    }
    
    QEventLoop loop;
    bool hasError = false;
    QString listToolsErr;
    cl->listToolsAsync("", [&](const std::vector<mcp_qt::McpQtTool>&, const QString&, const QString& errVal) {
        hasError = !errVal.isEmpty();
        listToolsErr = errVal;
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (hasError) {
        std::cerr << "[runInitialize] listToolsAsync failed: " << listToolsErr.toStdString() << std::endl;
        return 1;
    }
    return 0;
}

int runToolsCall(const RunnerConfig& c) {
    auto cl = mcp_qt::McpQtClient::connectHttpAndWait(QString::fromStdString(c.serverUrl));
    if (!cl) return 1;
    
    QEventLoop loop;
    bool hasError = false;
    cl->listToolsAsync("", [&](const std::vector<mcp_qt::McpQtTool>&, const QString&, const QString& err) {
        hasError = !err.isEmpty();
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    if (hasError) return 1;

    QJsonObject a; a["a"] = 5; a["b"] = 3;
    auto res = cl->callTool("add_numbers", a);
    return (res.isError || res.data.isEmpty()) ? 1 : 0;
}

int runSseRetry(const RunnerConfig& c) {
    auto cl = mcp_qt::McpQtClient::connectHttpAndWait(QString::fromStdString(c.serverUrl));
    if (!cl) return 1;

    QEventLoop loop;
    bool hasError = false;
    cl->listToolsAsync("", [&](const std::vector<mcp_qt::McpQtTool>&, const QString&, const QString& err) {
        hasError = !err.isEmpty();
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    if (hasError) return 1;

    auto res = cl->callTool("get_system_time", QJsonObject{});
    return (res.isError || res.data.isEmpty()) ? 1 : 0;
}

int runElicitationDefaults(const RunnerConfig& c) {
    // 使用 createForTest + connectToTransportAndWait，确保在 initialize 前注册 handler 和 capability
    auto cl = mcp_qt::McpQtClient::createForTest();

    // 预注册 elicitation capability（在 connectToTransportAndWait 中会在 initialize 前生效）
    QJsonObject ec; ec["form"] = QJsonObject{{"applyDefaults", true}};
    cl->registerCapability("elicitation", ec);

    // 预置 handler（connectToTransportAndWait 中会在 start/initialize 前安装 to session）
    cl->setElicitationHandler([](const QJsonObject&, std::function<void(const QJsonObject&, const QJsonObject&)> callback) {
        QJsonObject r; r["action"] = "accept"; r["content"] = QJsonObject{};
        callback(r, QJsonObject{});
    });

    // 现在连接——connectToTransportAndWait 会先应用 handler 和能力，再 start 和 initialize
    auto t = std::make_shared<mcp_qt::QtStatelessHttpTransport>(QString::fromStdString(c.serverUrl));
    QString errStr;
    if (!cl->connectToTransportAndWait(t, "mcp-qt-client", "1.0.0", 10000, &errStr)) return 1;

    // 先获取工具列表
    QEventLoop loop1;
    std::vector<mcp_qt::McpQtTool> tools;
    cl->listToolsAsync("", [&](const std::vector<mcp_qt::McpQtTool>& t, const QString&, const QString& err) {
        tools = t;
        loop1.quit();
    });
    QTimer::singleShot(10000, &loop1, &QEventLoop::quit);
    loop1.exec();

    if (tools.empty()) {
        std::cerr << "[Elicitation] No tools available" << std::endl;
        return 1;
    }

    // 调用第一个可用的工具
    QString toolName = tools[0].name;
    std::cerr << "[Elicitation] Calling tool: " << toolName.toStdString() << std::endl;

    QEventLoop loop2;
    bool hasError = false;
    cl->callToolAsync(toolName, QJsonObject{},
        [&](mcp_qt::McpResult res) {
            hasError = res.isError || res.data.isEmpty();
            loop2.quit();
        });
    QTimer::singleShot(10000, &loop2, &QEventLoop::quit);
    loop2.exec();

    return hasError ? 1 : 0;
}

// ========== Auth 场景（QtStatelessHttpTransport + OAuth 支持）==========

static int _raQt(const RunnerConfig& c, bool ct) {
    auto cl = mcp_qt::McpQtClient::createForTest();
    auto t = std::make_shared<mcp_qt::QtStatelessHttpTransport>(QString::fromStdString(c.serverUrl));

    // 设置 OAuth 支持
    auto oc = cl->oauthClient();
    t->setTokenProvider([oc]() -> std::string {
        if (!oc) return {};
        return oc->getCurrentToken().accessToken;
    });
    t->setAuthRetryHandler([oc, &c](const std::string& wwwAuth) -> bool {
        if (!oc) return false;
        nlohmann::json ctx;
        // 使用 context 中的所有 OAuth 相关字段
        if (!c.context.empty()) {
            if (c.context.contains("client_id")) ctx["client_id"] = c.context["client_id"];
            if (c.context.contains("client_secret")) ctx["client_secret"] = c.context["client_secret"];
            if (c.context.contains("private_key_pem")) ctx["private_key_pem"] = c.context["private_key_pem"];
            if (c.context.contains("signing_algorithm")) ctx["signing_algorithm"] = c.context["signing_algorithm"];
        }
        return mcp_qt::McpQtClient::runOAuthFlow(c.serverUrl, ctx, wwwAuth, oc);
    });

    QString errStr;
    // 使用 connectToTransportAndWait 自动进行 initialize 握手与 Auth 认证逻辑
    if (!cl->connectToTransportAndWait(t, "mcp-qt-client", "1.0.0", 15000, &errStr)) return 1;

    QEventLoop loop;
    bool hasError = false;
    cl->listToolsAsync("", [&](const std::vector<mcp_qt::McpQtTool>&, const QString&, const QString& err) {
        hasError = !err.isEmpty();
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    if (hasError) return 1;

    if (ct) {
        auto res = cl->callTool("get_system_time", QJsonObject{});
        if (res.isError) return 1;
    }
    return 0;
}

int runAuthFlow(const RunnerConfig& c) { return _raQt(c, true); }
int runClientCredentialsFlow(const RunnerConfig& c) { return _raQt(c, false); }

} // namespace mcp_conformance
