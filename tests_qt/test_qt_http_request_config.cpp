#include "mcp_qt_transport/QtHttpSseTransport.h"
#include "tests/common.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkProxy>
#include <QList>

// 辅助等待事件循环运转
static void waitEvents(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// 绝对安全、无悬空事件的本地单线程 Mock HTTP 服务器/代理服务器
class IntegrationMockServer : public QObject {
public:
    QTcpServer server;
    QList<QTcpSocket*> sockets;
    QList<QByteArray> receivedRequests;

    IntegrationMockServer() {
        QObject::connect(&server, &QTcpServer::newConnection, [this]() {
            QTcpSocket* socket = server.nextPendingConnection();
            if (!socket) return;
            sockets.append(socket);

            QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
                if (socket->state() != QAbstractSocket::ConnectedState) return;
                QByteArray data = socket->readAll();
                receivedRequests.append(data);

                std::string req(data.constData(), data.size());
                if (req.find("GET ") != std::string::npos) {
                    // 无论是普通 GET 还是代理 GET，均回应成功以维持连接
                    socket->write("HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/event-stream\r\n"
                                  "Connection: keep-alive\r\n\r\n");
                    socket->write("event: endpoint\ndata: /post\n\n");
                    socket->flush();
                } else if (req.find("POST ") != std::string::npos) {
                    socket->write("HTTP/1.1 200 OK\r\n"
                                  "Content-Type: application/json\r\n\r\n"
                                  "{\"jsonrpc\":\"2.0\",\"id\":123,\"result\":{}}");
                    socket->flush();
                    socket->disconnectFromHost();
                }
            });
        });
        server.listen(QHostAddress::LocalHost, 0);
    }

    ~IntegrationMockServer() {
        for (auto* s : sockets) {
            if (s) {
                s->disconnect();
                s->close();
                s->deleteLater();
            }
        }
        server.close();
    }

    quint16 port() const { return server.serverPort(); }
};

void test_qt_http_request_config() {
    // 1. 验证高级配置 (Headers, Proxy) 的内存存取
    {
        mcp_qt::QtHttpSseTransport transport("http://127.0.0.1:12345/sse");

        mcp_qt::QtHttpRequestConfig config;
        config.defaultHeaders.insert("X-Test-Custom-Header", "hello-world-value");
        QNetworkProxy proxy(QNetworkProxy::HttpProxy, "my-proxy.example.com", 8080);
        config.proxy = proxy;
        transport.setRequestConfig(config);

        auto savedConfig = transport.requestConfig();
        QByteArray val = savedConfig.defaultHeaders.value("X-Test-Custom-Header");
        std::string checkHeader(val.constData(), val.size());
        TM_ASSERT_EQ(checkHeader, "hello-world-value", "custom header store check");
        TM_ASSERT_TRUE(savedConfig.proxy.has_value(), "proxy store check");
        TM_ASSERT_EQ(savedConfig.proxy->hostName().toStdString(), "my-proxy.example.com", "proxy host check");
    }

    // 2. 端到端：验证自定义 Header 在 SSE GET 握手和 POST 消息中均被物理注入并发送
    {
        IntegrationMockServer mock;

        mcp_qt::QtHttpSseTransport transport(
            QString("http://127.0.0.1:%1/sse").arg(mock.port()).toStdString()
        );

        mcp_qt::QtHttpRequestConfig config;
        config.defaultHeaders.insert("X-Test-Custom-Header", "hello-world-value");
        transport.setRequestConfig(config);

        transport.start();

        int elapsed = 0;
        while (mock.receivedRequests.isEmpty() && elapsed < 2000) {
            waitEvents(20);
            elapsed += 20;
        }

        TM_ASSERT_FALSE(mock.receivedRequests.isEmpty(), "Server must receive the GET handshake");
        
        transport.send("{\"jsonrpc\":\"2.0\",\"id\":123,\"method\":\"ping\"}");

        elapsed = 0;
        while (mock.receivedRequests.size() < 2 && elapsed < 2000) {
            waitEvents(20);
            elapsed += 20;
        }

        TM_ASSERT_TRUE(mock.receivedRequests.size() >= 2, "Server must receive the POST request");

        bool getCarried = false;
        bool postCarried = false;

        for (const auto& data : mock.receivedRequests) {
            std::string reqStr(data.constData(), data.size());
            if (reqStr.find("GET ") != std::string::npos) {
                if (reqStr.find("X-Test-Custom-Header: hello-world-value") != std::string::npos) {
                    getCarried = true;
                }
            } else if (reqStr.find("POST ") != std::string::npos) {
                if (reqStr.find("X-Test-Custom-Header: hello-world-value") != std::string::npos) {
                    postCarried = true;
                }
            }
        }

        TM_ASSERT_TRUE(getCarried, "HTTP SSE GET request must carry custom header");
        TM_ASSERT_TRUE(postCarried, "HTTP POST message request must carry custom header");

        transport.close();
        waitEvents(100);
    }

    // 3. 端到端：验证 QNetworkProxy 确实使请求改走代理服务器路径发送
    {
        IntegrationMockServer proxyMock; // 作为代理服务器

        // 连接目标是一个“虚构”的、局域网内无法直接访问的主机名
        mcp_qt::QtHttpSseTransport transport("http://arbitrary-mock-target.internal/sse");

        // 配置代理为本地的 proxyMock
        mcp_qt::QtHttpRequestConfig config;
        config.proxy = QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxyMock.port());
        transport.setRequestConfig(config);

        // 启动连接：若代理生效，请求将被转发至 proxyMock 端口
        transport.start();

        int elapsed = 0;
        while (proxyMock.receivedRequests.isEmpty() && elapsed < 2000) {
            waitEvents(20);
            elapsed += 20;
        }

        TM_ASSERT_FALSE(proxyMock.receivedRequests.isEmpty(), "Proxy server must receive the forwarded connection");

        // 验证收到的代理请求行中包含指向虚构目标主机的绝对 URL
        bool proxyPathVerified = false;
        for (const auto& data : proxyMock.receivedRequests) {
            std::string reqStr(data.constData(), data.size());
            if (reqStr.find("http://arbitrary-mock-target.internal/sse") != std::string::npos) {
                proxyPathVerified = true;
                break;
            }
        }

        TM_ASSERT_TRUE(proxyPathVerified, "HTTP Request must flow through proxy and carry the target absolute URL");

        transport.close();
        waitEvents(100);
    }
}
