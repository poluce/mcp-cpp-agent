#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <memory>
#include <mcp_qt_client/McpQtClient.h>

namespace mcp_qt {

/**
 * @brief 多服务器生命周期管理器 (McpServerManager)
 * 
 * 职责：
 * 1. 读取并解析 mcp_servers.json 配置文件。
 * 2. 循环调用 McpQtClientBuilder 实例化并连接多个客户端。
 * 3. 存储并管理各客户端的生命周期（按服务器名字，如 "github-mcp", "figma-mcp" 存储）。
 * 4. 统一接管所有 client 的 connected, disconnected, errorOccurred, toolsChanged 等信号并重新分发聚合。
 */
class McpServerManager : public QObject {
    Q_OBJECT
public:
    explicit McpServerManager(QObject* parent = nullptr);
    ~McpServerManager() override;

    // 从 JSON 配置加载（支持 stdio 和 sse 连接方式，且包含自动注入 env 环境变量）
    bool loadConfig(const QJsonObject& configObject);
    bool loadConfigFile(const QString& filePath);

    // 显式注册客户端（如果已存在同名客户端，则会先关闭并注销旧的）
    void registerClient(const QString& serverName, std::shared_ptr<McpQtClient> client);
    
    // 注销客户端
    void unregisterClient(const QString& serverName);

    // 获取特定客户端指针
    std::shared_ptr<McpQtClient> client(const QString& serverName) const;

    // 获取所有客户端
    QHash<QString, std::shared_ptr<McpQtClient>> clients() const;

    // 获取所有已注册的服务器名字
    QStringList serverNames() const;

    // 优雅关闭所有客户端
    void closeAll(int timeoutMs = 5000);

signals:
    // 状态与事件的聚合信号
    void clientConnected(const QString& serverName);
    void clientDisconnected(const QString& serverName);
    void clientErrorOccurred(const QString& serverName, const QString& error);
    void clientToolsChanged(const QString& serverName, const std::vector<mcp_qt::McpQtTool>& newTools);

private:
    void setupClientSignals(const QString& serverName, const std::shared_ptr<McpQtClient>& client);

    QHash<QString, std::shared_ptr<McpQtClient>> m_clients;
};

} // namespace mcp_qt
