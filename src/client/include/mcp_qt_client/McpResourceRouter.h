#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QFuture>
#include <QPair>
#include <mcp_qt_client/McpQtClient.h>

namespace mcp_qt {

class McpServerManager;

class McpResourceRouter : public QObject {
    Q_OBJECT
public:
    explicit McpResourceRouter(McpServerManager* manager, QObject* parent = nullptr);
    ~McpResourceRouter() override = default;

    // 获取所有资源列表，URI 会被重写为 mcp-{serverName}- 前缀以避免冲突
    QJsonArray fetchAllResources(int timeoutMs = 10000) const;

    // 解析 URI，返回 {serverName, originalUri}
    QPair<QString, QString> parseResourceUri(const QString& nameSpacedUri) const;

    // 读资源
    QJsonObject readResource(const QString& nameSpacedUri, int timeoutMs = 10000);
    void readResourceAsync(const QString& nameSpacedUri, std::function<void(const QJsonObject& result, const QString& error)> callback);

    // 异步获取所有资源
    void fetchAllResourcesAsync(std::function<void(const QJsonArray& resources)> callback, int timeoutMs = 10000) const;

private:
    McpServerManager* m_manager{nullptr};
};

} // namespace mcp_qt
