#pragma once

#include <mcp_core/McpClientSession.h>
#include <mcp_core/McpTool.h>
#include <mcp_core/McpResource.h>
#include <mcp_core/McpPrompt.h>
#include <mcp_core/McpOAuthClient.h>
#include <mcp_qt_transport/QtHttpSseTransport.h>
#include <mcp_qt_transport/QtProcessStdioTransport.h>

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <functional>
#include <memory>

namespace mcp_qt {

struct McpResult {
    bool isError{false};
    QJsonObject data;
    QString errorString;
};

struct McpQtTool {
    QString name;
    QString description;
    QJsonObject inputSchema;
};

class McpQtClient;
class McpQtClientBuilder {
public:
    McpQtClientBuilder& setTransportHttp(const QString& url);
    McpQtClientBuilder& setTransportStdio(const QString& command, const QStringList& args = {});
    McpQtClientBuilder& setClientInfo(const QString& name, const QString& version);
    McpQtClientBuilder& setTimeout(int ms);
    std::shared_ptr<McpQtClient> buildAndConnect(QString* errorString = nullptr);
private:
    int m_transportType{0}; // 0=none, 1=http, 2=stdio
    QString m_url_or_cmd;
    QStringList m_args;
    QString m_clientName{QStringLiteral("mcp-qt-client")};
    QString m_clientVersion{QStringLiteral("1.0.0")};
    int m_timeoutMs{10000};
};

/**
 * @brief 高层 MCP 客户端（QObject，信号/槽，语义对齐 TS SDK `Client`）
 *
 * @code
 *   // HTTP/SSE
 *   auto c = McpQtClient::connectHttp("http://localhost:8080/mcp");
 *   // Stdio 子进程
 *   auto c = McpQtClient::connectStdio("python", {"server.py"});
 *   // OAuth
 *   auto c = McpQtClient::connectWithOAuth({.serverUrl="...", .clientId="..."});
 *
 *   auto tools = c->listTools();
 *   c->callTool("add", {{"a",5},{"b",3}});
 *   c->setLoggingLevel("debug");
 * @endcode
 */
class McpQtClient : public QObject {
    Q_OBJECT
public:
    using Ptr = std::shared_ptr<McpQtClient>;

    struct OAuthConfig {
        QString serverUrl;
        QString clientId;
        QString clientSecret;
        QString redirectUri{QStringLiteral("http://localhost:3000/callback")};
        QStringList scopes;
    };

    ~McpQtClient() override;

    // ========== 工厂（对齐 TS `new Client({name,version}).connect(transport)`）==========

    /// HTTP/SSE 连接
    static Ptr connectHttp(const QString& serverUrl,
                           const QString& clientName = QStringLiteral("mcp-qt-client"),
                           const QString& clientVersion = QStringLiteral("1.0.0"),
                           int timeoutMs = 10000,
                           QString* errorString = nullptr);

    /// Stdio 子进程连接
    static Ptr connectStdio(const QString& command, const QStringList& args = {},
                            const QString& clientName = QStringLiteral("mcp-qt-client"),
                            const QString& clientVersion = QStringLiteral("1.0.0"),
                            int timeoutMs = 10000,
                            QString* errorString = nullptr);

    /// HTTP/SSE + OAuth
    static Ptr connectWithOAuth(const OAuthConfig& oauth,
                                const QString& clientName = QStringLiteral("mcp-qt-client"),
                                const QString& clientVersion = QStringLiteral("1.0.0"),
                                int timeoutMs = 30000);

    // ========== Server Info（对齐 TS `getServerCapabilities()` 等）==========

    QJsonObject serverInfo() const;
    QJsonObject serverCapabilities() const;
    QString negotiatedProtocolVersion() const;
    QString instructions() const;

    // ========== Tools（对齐 TS `listTools()`, `callTool()`）==========

    using ProgressCallback = std::function<void(float progress, float total, const QString& message)>;

    std::vector<McpQtTool> listTools(int timeoutMs = 10000);
    std::vector<McpQtTool> listTools(const QString& cursor, QString* nextCursor, int timeoutMs = 10000);
    std::vector<McpQtTool> fetchAllTools(int timeoutMs = 10000);

    /// 调用工具（同步，对齐 TS `callTool()`）
    McpResult callTool(const QString& name, const QJsonObject& arguments, int timeoutMs = 10000);

    /// 调用工具 + 进度通知（对齐 TS `callTool({...}, {onProgress})`）
    McpResult callTool(const QString& name, const QJsonObject& arguments,
                       ProgressCallback onProgress, int timeoutMs = 10000);

    /// 调用工具（纯异步，防止阻塞主线程）
    void callToolAsync(const QString& name, const QJsonObject& arguments,
                       std::function<void(McpResult)> callback,
                       ProgressCallback onProgress = nullptr);

    /// 调用工具（纯异步，绑定上下文保护生命周期，回调切换到接收方所在线程）
    void callToolAsync(const QString& name, const QJsonObject& arguments,
                       QObject* context,
                       std::function<void(McpResult)> callback,
                       ProgressCallback onProgress = nullptr);

    // ========== Resources（对齐 TS `listResources()`, `readResource()`, `subscribeResource()`）==========

    QJsonObject listResources(int timeoutMs = 10000);
    QJsonObject listResources(const QString& cursor, QString* nextCursor, int timeoutMs = 10000);
    QJsonObject fetchAllResources(int timeoutMs = 10000);
    QJsonObject readResource(const QString& uri, int timeoutMs = 10000);
    bool subscribeResource(const QString& uri, int timeoutMs = 10000);
    bool unsubscribeResource(const QString& uri, int timeoutMs = 10000);

    // ========== Resource Templates（对齐 TS `listResourceTemplates()`）==========

    std::vector<mcp::McpResourceTemplate> listResourceTemplates(int timeoutMs = 10000);
    std::vector<mcp::McpResourceTemplate> listResourceTemplates(const QString& cursor, QString* nextCursor, int timeoutMs = 10000);
    std::vector<mcp::McpResourceTemplate> fetchAllResourceTemplates(int timeoutMs = 10000);

    // ========== Prompts（对齐 TS `listPrompts()`, `getPrompt()`）==========

    QJsonObject listPrompts(int timeoutMs = 10000);
    QJsonObject listPrompts(const QString& cursor, QString* nextCursor, int timeoutMs = 10000);
    QJsonObject fetchAllPrompts(int timeoutMs = 10000);
    QJsonObject getPrompt(const QString& name, const QJsonObject& arguments, int timeoutMs = 10000);

    // ========== 其他（对齐 TS `ping()`, `complete()`, `setLoggingLevel()`）==========

    bool ping(int timeoutMs = 5000);
    QJsonObject complete(const QJsonObject& ref, const QJsonObject& argument, int timeoutMs = 10000);
    /// 设置服务端日志级别，发送 logging/setLevel 请求
    bool setLoggingLevel(const QString& level, int timeoutMs = 5000);

    // ========== 双向能力（对齐 TS `setRequestHandler()`）==========

    using ElicitationHandler = std::function<void(const QJsonObject& params, std::function<void(const QJsonObject& result, const QJsonObject& error)> callback)>;
    void setElicitationHandler(ElicitationHandler handler);
    void setElicitationHandler(QObject* context, ElicitationHandler handler);

    using SamplingHandler = std::function<void(const QJsonObject& params, std::function<void(const QJsonObject& result, const QJsonObject& error)> callback)>;
    void setSamplingHandler(SamplingHandler handler);
    void setSamplingHandler(QObject* context, SamplingHandler handler);

    using RootsProvider = std::function<void(std::function<void(const QJsonArray& result, const QJsonObject& error)> callback)>;
    void setRootsProvider(RootsProvider provider);
    void setRootsProvider(QObject* context, RootsProvider provider);
    void notifyRootsListChanged();

    // ========== 通知（对齐 TS `notification()` 等）==========

    void registerNotificationHandler(const QString& method, std::function<void(const QJsonObject& params)> handler);
    void registerNotificationHandler(const QString& method, QObject* context, std::function<void(const QJsonObject& params)> handler);
    void enableNotificationDebounce(const QString& method, int debounceMs = 100);
    /// 发送任意通知给服务端
    void sendNotification(const QString& method, const QJsonObject& params);

    /// 发送请求（异步，对齐 TS `client.request()`）。返回 requestId，可用于 cancelRequest
    int64_t sendRequest(const QString& method, const QJsonObject& params,
                        std::function<void(const QJsonObject& result, const QJsonObject& error)> callback,
                        ProgressCallback onProgress = nullptr);

    /// 发送请求（纯异步，绑定上下文保护生命周期，回调切换到接收方所在线程）
    int64_t sendRequest(const QString& method, const QJsonObject& params,
                        QObject* context,
                        std::function<void(const QJsonObject& result, const QJsonObject& error)> callback,
                        ProgressCallback onProgress = nullptr);
    /// 取消指定请求
    void cancelRequest(int64_t requestId);

    // ========== 能力（对齐 TS `registerCapabilities()`）==========

    void registerCapability(const QString& name, const QJsonObject& config);

    // ========== 生命周期（对齐 TS `close()`）==========

    bool isConnected() const;
    /// 优雅关闭（发送 shutdown 请求后关闭 transport）
    void close(int timeoutMs = 5000);

    // ========== 异步连接 ==========

    /// 连接到已有 transport（对齐 TS `connect(transport)`）
    bool connectToTransport(std::shared_ptr<mcp::IMcpTransport> transport,
                           const QString& clientName, const QString& clientVersion, int timeoutMs = 10000, QString* errorString = nullptr);

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);
    
    /// 收到服务端的任意通知
    void notificationReceived(const QString& method, const QJsonObject& params);

private:
    explicit McpQtClient(QObject* parent = nullptr);

    bool doInitialize(const QString& clientName, const QString& clientVersion, int timeoutMs, QString* errorString = nullptr);
    bool doOAuth(const OAuthConfig& oauth);

    static nlohmann::json toNlohmann(const QJsonObject& obj);
    static QJsonObject fromNlohmann(const nlohmann::json& j);

    std::shared_ptr<mcp::McpClientSession> m_session;
    std::shared_ptr<mcp::McpOAuthClient> m_oauth;
    bool m_initialized{false};
    mutable std::map<QString, QJsonObject> m_toolSchemaCache;
    bool validateToolSchemaLocally(const QString& name, const QJsonObject& arguments, QString* errorString) const;
};

} // namespace mcp_qt
