#include <mcp_qt_client/McpToolRouter.h>
#include <mcp_qt_client/McpServerManager.h>
#include <QDebug>

namespace mcp_qt {

McpToolRouter::McpToolRouter(McpServerManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager) {}

QPair<QString, QString> McpToolRouter::parseToolName(const QString& nameSpacedToolName) const {
    if (!m_manager) {
        return {};
    }
    
    // 遍历所有已注册的 serverNames 尝试进行前缀匹配
    // 这种做法可兼容 serverName 中包含 "_" 字符的情况
    QStringList servers = m_manager->serverNames();
    for (const QString& serverName : servers) {
        QString prefix = serverName + QStringLiteral("_");
        if (nameSpacedToolName.startsWith(prefix)) {
            QString originalToolName = nameSpacedToolName.mid(prefix.length());
            return {serverName, originalToolName};
        }
    }
    
    // 后备方案：以第一个 "_" 为界进行分割
    int index = nameSpacedToolName.indexOf(QStringLiteral("_"));
    if (index > 0) {
        QString serverName = nameSpacedToolName.left(index);
        QString originalToolName = nameSpacedToolName.mid(index + 1);
        if (m_manager->client(serverName)) {
            return {serverName, originalToolName};
        }
    }
    
    return {};
}

QJsonArray McpToolRouter::exportAllToolsToLlmFormat(McpQtClient::LlmFormat format) const {
    QJsonArray result;
    if (!m_manager) return result;

    auto clientsMap = m_manager->clients();
    for (auto it = clientsMap.begin(); it != clientsMap.end(); ++it) {
        QString serverName = it.key();
        auto client = it.value();
        if (!client) continue;

        // 获取缓存在该客户端中的工具，而无需重复发送网络 RPC 获取列表
        std::vector<McpQtTool> tools = client->cachedTools();
        for (const auto& tool : tools) {
            McpQtTool modifiedTool = tool;
            modifiedTool.name = serverName + QStringLiteral("_") + tool.name;
            result.append(McpQtClient::exportToolToLlmFormat(modifiedTool, format));
        }
    }
    return result;
}

QFuture<McpResult> McpToolRouter::callToolFuture(const QString& nameSpacedToolName, const QJsonObject& arguments) {
    auto parsed = parseToolName(nameSpacedToolName);
    if (parsed.first.isEmpty()) {
        QPromise<McpResult> promise;
        promise.start();
        McpResult errRes;
        errRes.isError = true;
        errRes.errorString = QStringLiteral("Failed to resolve server for namespaced tool: ") + nameSpacedToolName;
        promise.addResult(errRes);
        promise.finish();
        return promise.future();
    }

    auto client = m_manager->client(parsed.first);
    if (!client) {
        QPromise<McpResult> promise;
        promise.start();
        McpResult errRes;
        errRes.isError = true;
        errRes.errorString = QStringLiteral("Client not found for server: ") + parsed.first;
        promise.addResult(errRes);
        promise.finish();
        return promise.future();
    }

    return client->callToolFuture(parsed.second, arguments);
}

void McpToolRouter::callToolAsync(const QString& nameSpacedToolName, const QJsonObject& arguments,
                                  std::function<void(McpResult)> callback,
                                  McpQtClient::ProgressCallback onProgress) {
    auto parsed = parseToolName(nameSpacedToolName);
    if (parsed.first.isEmpty()) {
        McpResult errRes;
        errRes.isError = true;
        errRes.errorString = QStringLiteral("Failed to resolve server for namespaced tool: ") + nameSpacedToolName;
        if (callback) {
            QMetaObject::invokeMethod(this, [=]() { callback(errRes); }, Qt::QueuedConnection);
        }
        return;
    }

    auto client = m_manager->client(parsed.first);
    if (!client) {
        McpResult errRes;
        errRes.isError = true;
        errRes.errorString = QStringLiteral("Client not found for server: ") + parsed.first;
        if (callback) {
            QMetaObject::invokeMethod(this, [=]() { callback(errRes); }, Qt::QueuedConnection);
        }
        return;
    }

    client->callToolAsync(parsed.second, arguments, callback, onProgress);
}

} // namespace mcp_qt
