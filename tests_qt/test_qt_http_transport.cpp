#include "mcp_qt_transport/QtHttpSseTransport.h"
#include "tests/common.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <functional>
#include <vector>

// 辅助等待事件循环运转
static void waitEvents(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// 轻量级 Mock HTTP 服务器
class MockHttpServer : public QObject {
public:
    QTcpServer server;
    QByteArray lastRequestData;
    QByteArray responseHeader;
    QByteArray responseBody;
    int requestCount{0};
    std::function<void(QTcpSocket*, const QByteArray&)> customHandler{nullptr};

    MockHttpServer() {
        connect(&server, &QTcpServer::newConnection, this, &MockHttpServer::handleConnection);
        server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return server.serverPort(); }

    void handleConnection() {
        QTcpSocket* socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, [this, socket]() {
            QByteArray request = socket->readAll();
            lastRequestData = request;
            requestCount++;

            if (customHandler) {
                customHandler(socket, request);
            } else {
                socket->write(responseHeader);
                socket->write(responseBody);
                socket->disconnectFromHost();
            }
        });
    }
};

void test_qt_transport_rejects_send_before_start() {
    mcp_qt::QtHttpSseTransport transport("https://example.test/mcp");
    TM_ASSERT_FALSE(transport.send(R"({"jsonrpc":"2.0"})"), "send before start should fail");
}

// 1. 验证协议版本运行时动态下发
void test_qt_transport_updates_protocol_version_runtime() {
    MockHttpServer mock;
    mock.responseHeader = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nMCP-Session-Id: s1\r\n\r\n";
    mock.responseBody = "event: endpoint\ndata: /post\n\n";

    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>(
        QString("http://127.0.0.1:%1/sse").arg(mock.port()).toStdString()
    );

    transport->start();
    waitEvents(100); // 等待 SSE GET 请求发出并建立

    // 修改版本
    transport->setProtocolVersion("2026-99-99");

    // 拦截 POST 请求
    mock.lastRequestData.clear();
    mock.responseHeader = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
    mock.responseBody = "{}";

    transport->send("{}");
    waitEvents(100); // 等待 POST 发生

    std::string req(mock.lastRequestData.constData(), mock.lastRequestData.size());
    TM_ASSERT_STR_CONTAINS(req, "Mcp-Protocol-Version: 2026-99-99", "POST should contain updated protocol version");

    transport->close();
    waitEvents(50);
}

// 2. 验证 401 挑战重试
void test_qt_transport_auth_retry() {
    MockHttpServer mock;
    std::string currentToken = "old-token";

    mock.customHandler = [&](QTcpSocket* socket, const QByteArray& request) {
        std::string reqStr(request.constData(), request.size());
        if (reqStr.find("Authorization: Bearer my-new-token") != std::string::npos) {
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nMCP-Session-Id: s-auth-ok\r\n\r\n");
            socket->write("event: endpoint\ndata: /post\n\n");
        } else {
            socket->write("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Bearer error=\"invalid_token\"\r\n\r\n");
        }
        socket->disconnectFromHost();
    };

    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>(
        QString("http://127.0.0.1:%1/sse").arg(mock.port()).toStdString()
    );

    transport->setTokenProvider([&]() { return currentToken; });
    transport->setAuthRetryHandler([&](const std::string&) {
        currentToken = "my-new-token";
        return true; // 同意重试
    });

    transport->start();
    waitEvents(150); // 应该完成 401 并自动重新 openSse 成功

    std::string lastReq(mock.lastRequestData.constData(), mock.lastRequestData.size());
    TM_ASSERT_STR_CONTAINS(lastReq, "Authorization: Bearer my-new-token", "Retry request should contain updated bearer token");

    transport->close();
    waitEvents(50);
}

// 3. 验证 POST 流式响应拆包与多行 data: 拼接解析
void test_qt_transport_post_event_stream_parsing() {
    MockHttpServer mock;
    mock.responseHeader = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nMCP-Session-Id: s2\r\n\r\n";
    mock.responseBody = "event: endpoint\ndata: /post\n\n";

    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>(
        QString("http://127.0.0.1:%1/sse").arg(mock.port()).toStdString()
    );

    std::vector<std::string> receivedMessages;
    transport->setOnMessage([&](const std::string& msg) {
        receivedMessages.push_back(msg);
    });

    transport->start();
    waitEvents(100);

    // 模拟两个事件，其中第一个是多行合并 data，第二个是单行 data
    mock.responseHeader = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n";
    mock.responseBody = "data: {\"part\":1}\ndata: {\"part\":2}\n\ndata: {\"single\":3}\n\n";

    transport->send("{}");
    waitEvents(100);

    TM_ASSERT_EQ(receivedMessages.size(), size_t(2), "Should receive exactly 2 messages from POST");
    if (receivedMessages.size() >= 2) {
        // 第一条消息应为多行拼接
        TM_ASSERT_EQ(receivedMessages[0], std::string("{\"part\":1}\n{\"part\":2}"), "Multiline event data should be merged with newline");
        // 第二条为常规
        TM_ASSERT_EQ(receivedMessages[1], std::string("{\"single\":3}"), "Second event should match");
    }

    transport->close();
    waitEvents(50);
}

// 4. 验证当 POST 响应虽然包含 "data:" 但 Content-Type 不是 text/event-stream 时，不会被误判为 SSE
void test_qt_transport_post_json_with_data_field() {
    MockHttpServer mock;
    mock.responseHeader = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nMCP-Session-Id: s3\r\n\r\n";
    mock.responseBody = "event: endpoint\ndata: /post\n\n";

    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>(
        QString("http://127.0.0.1:%1/sse").arg(mock.port()).toStdString()
    );

    std::vector<std::string> receivedMessages;
    transport->setOnMessage([&](const std::string& msg) {
        receivedMessages.push_back(msg);
    });

    transport->start();
    waitEvents(100);

    // 模拟普通 JSON 响应，包含 "data:" 且 Content-Type 是 application/json
    mock.responseHeader = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
    mock.responseBody = "{\"status\":\"ok\",\"data\":{\"info\":\"hello\"}}";

    transport->send("{}");
    waitEvents(100);

    TM_ASSERT_EQ(receivedMessages.size(), size_t(1), "Should receive exactly 1 message from POST");
    if (receivedMessages.size() >= 1) {
        TM_ASSERT_EQ(receivedMessages[0], std::string("{\"status\":\"ok\",\"data\":{\"info\":\"hello\"}}"), 
                     "Message should be received intact without split");
    }

    transport->close();
    waitEvents(50);
}

// 5. 验证当 401 发生且重试失败（或者 handler 拒绝）时，彻底阻断重连
void test_qt_transport_auth_retry_failure_stops_reconnect() {
    MockHttpServer mock;
    mock.responseHeader = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Bearer error=\"invalid_token\"\r\n\r\n";

    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>(
        QString("http://127.0.0.1:%1/sse").arg(mock.port()).toStdString()
    );

    int closeCount = 0;
    transport->setOnClose([&]() { ++closeCount; });

    transport->setAuthRetryHandler([&](const std::string&) {
        return false; // 明确拒绝重试，认证失败
    });

    transport->start();
    waitEvents(150); // 应该完成 401 请求并立即触发 onClose 阻断重连

    TM_ASSERT_EQ(closeCount, 1, "Should close transport when auth retry fails");
    TM_ASSERT_EQ(mock.requestCount, 1, "Should only make 1 request and not enter reconnect loop");

    // 状态一致性校验
    TM_ASSERT_FALSE(transport->send("{}"), "send after transport closed should fail");

    // 再次手动 close，不应该触发第二次 onClose
    transport->close();
    waitEvents(20);
    TM_ASSERT_EQ(closeCount, 1, "onClose should not be triggered multiple times");
}

// 6. 验证在运行中（长连接不中断）动态注入 TokenProvider，仍然能实时热更新到 worker 内部状态
void test_qt_transport_updates_token_provider_runtime() {
    MockHttpServer mock;

    mock.customHandler = [&](QTcpSocket* socket, const QByteArray& request) {
        std::string reqStr(request.constData(), request.size());
        if (reqStr.find("GET /sse") != std::string::npos) {
            // GET 流正常建立
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nMCP-Session-Id: s-runtime\r\n\r\n");
            socket->write("event: endpoint\ndata: /post\n\n");
        } else if (reqStr.find("POST /post") != std::string::npos) {
            // POST 正常回复 200
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
            socket->write("{}");
        }
        socket->disconnectFromHost();
    };

    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>(
        QString("http://127.0.0.1:%1/sse").arg(mock.port()).toStdString()
    );

    // 启动时没有配置 TokenProvider
    transport->start();
    waitEvents(100); // 正常握手进入运行态

    // 运行时动态注入 TokenProvider
    transport->setTokenProvider([&]() { return "runtime-token-ok"; });

    // 在不 close / 不重新 start 的前提下发送消息
    mock.lastRequestData.clear();
    transport->send("{}");
    waitEvents(100); // 等待异步 POST 消息发送完毕

    std::string lastReq(mock.lastRequestData.constData(), mock.lastRequestData.size());
    TM_ASSERT_STR_CONTAINS(lastReq, "Authorization: Bearer runtime-token-ok", "Runtime setter should dynamically update token on running worker");

    transport->close();
    waitEvents(50);
}

// 7. 验证当 POST 响应返回 401/403 且重试失败时，拦截其 body 发送，仅触发 onError 信号
void test_qt_transport_post_auth_failure_blocks_message() {
    MockHttpServer mock;
    mock.responseHeader = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nMCP-Session-Id: s4\r\n\r\n";
    mock.responseBody = "event: endpoint\ndata: /post\n\n";

    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>(
        QString("http://127.0.0.1:%1/sse").arg(mock.port()).toStdString()
    );

    std::vector<std::string> receivedMessages;
    transport->setOnMessage([&](const std::string& msg) {
        receivedMessages.push_back(msg);
    });

    std::string lastError;
    transport->setOnError([&](const std::string& err) {
        lastError = err;
    });

    transport->start();
    waitEvents(100);

    // 拦截 POST，返回 401 且带有 HTML/错误 JSON 载荷
    mock.responseHeader = "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\n\r\n";
    mock.responseBody = "{\"error\":\"unauthorized_client\"}";

    transport->send("{}");
    waitEvents(100);

    // 验证没有消息被误分发，但错误被成功上报
    TM_ASSERT_EQ(receivedMessages.size(), size_t(0), "Should not emit any message when POST auth fails");
    TM_ASSERT_STR_CONTAINS(lastError, "HTTP 401", "Should report HTTP 401 post authentication error");

    transport->close();
    waitEvents(50);
}
