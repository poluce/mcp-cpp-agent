#include "mcp_qt_client/McpResourceRouter.h"
#include "mcp_qt_client/McpServerManager.h"
#include <QJsonArray>
#include <QJsonObject>

namespace mcp_qt {

McpResourceRouter::McpResourceRouter(McpServerManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager) {}

QJsonArray McpResourceRouter::fetchAllResources(int timeoutMs) const {
    QJsonArray allResources;
    if (!m_manager) return allResources;

    for (const QString& serverName : m_manager->serverNames()) {
        auto client = m_manager->client(serverName);
        if (!client) continue;
        
        QJsonObject result = client->fetchAllResources(timeoutMs);
        QJsonArray resources = result["resources"].toArray();
        for (int i = 0; i < resources.size(); ++i) {
            QJsonObject r = resources[i].toObject();
            // 改写 URI 加上 serverName
            // e.g. file:///abc -> mcp-serverName-file:///abc
            r["uri"] = "mcp-" + serverName + "-" + r["uri"].toString();
            allResources.append(r);
        }
    }
    return allResources;
}

QPair<QString, QString> McpResourceRouter::parseResourceUri(const QString& nameSpacedUri) const {
    if (!m_manager) return {};
    for (const QString& serverName : m_manager->serverNames()) {
        QString prefix = "mcp-" + serverName + "-";
        if (nameSpacedUri.startsWith(prefix)) {
            return {serverName, nameSpacedUri.mid(prefix.length())};
        }
    }
    return {};
}

QJsonObject McpResourceRouter::readResource(const QString& nameSpacedUri, int timeoutMs) {
    auto parsed = parseResourceUri(nameSpacedUri);
    if (parsed.first.isEmpty()) {
        return QJsonObject{{"error", "Unknown resource namespace"}};
    }
    auto client = m_manager->client(parsed.first);
    if (!client) {
        return QJsonObject{{"error", "Client disconnected"}};
    }
    return client->readResource(parsed.second, timeoutMs);
}

void McpResourceRouter::readResourceAsync(const QString& nameSpacedUri, std::function<void(const QJsonObject&, const QString&)> callback) {
    auto parsed = parseResourceUri(nameSpacedUri);
    if (parsed.first.isEmpty()) {
        callback(QJsonObject(), "Unknown resource namespace");
        return;
    }
    auto client = m_manager->client(parsed.first);
    if (!client) {
        callback(QJsonObject(), "Client disconnected");
        return;
    }
    client->readResourceAsync(parsed.second, callback);
}

} // namespace mcp_qt
