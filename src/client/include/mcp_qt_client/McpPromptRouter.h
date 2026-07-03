#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QFuture>
#include <QPair>
#include <mcp_qt_client/McpQtClient.h>

namespace mcp_qt {

class McpServerManager;

class McpPromptRouter : public QObject {
    Q_OBJECT
public:
    explicit McpPromptRouter(McpServerManager* manager, QObject* parent = nullptr);
    ~McpPromptRouter() override = default;

signals:
    void promptsChanged();

public:
    // 获取所有服务器的 Prompts，自动加上 serverName_ 前缀
    QJsonArray fetchAllPrompts(int timeoutMs = 10000) const;

    // 解析 namespaced prompt名
    QPair<QString, QString> parsePromptName(const QString& nameSpacedPromptName) const;

    // 获取特定 Prompt
    QJsonObject getPrompt(const QString& nameSpacedPromptName, const QJsonObject& arguments, int timeoutMs = 10000);
    void getPromptAsync(const QString& nameSpacedPromptName, const QJsonObject& arguments,
                        std::function<void(const QJsonObject& result, const QString& error)> callback);

private:
    McpServerManager* m_manager{nullptr};
};

} // namespace mcp_qt
