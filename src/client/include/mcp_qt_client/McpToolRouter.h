#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QFuture>
#include <QPromise>
#include <QPair>
#include <mcp_qt_client/McpQtClient.h>

namespace mcp_qt {

class McpServerManager;

/**
 * @brief 工具命名空间与路由层 (McpToolRouter)
 * 
 * 职责：
 * 1. 自动为各服务器导出的工具名称前加上 `serverName + "_"` 的命名空间前缀（例如：`github_search`）。
 * 2. 提供获取合并后所有工具定义的方法，方便喂给 LLM。
 * 3. 路由分发：当收到 LLM 调用带有命名空间的前缀的工具名时，裁切掉前缀找到对应的 Client，并触发异步调用。
 */
class McpToolRouter : public QObject {
    Q_OBJECT
public:
    explicit McpToolRouter(McpServerManager* manager, QObject* parent = nullptr);
    ~McpToolRouter() override = default;

    // 获取所有服务器的工具，并自动加上命名空间前缀
    // nameSpacedToolName 的生成规则为： serverName + "_" + toolName
    // 支持指定 LLM 格式输出（OpenAI, Anthropic, Gemini）
    // 注意：返回值已被整体格式化为 LLM 专有结构，调用方可直接赋值给 API 请求的 tools 字段，无需额外包裹。
    QJsonArray exportAllToolsToLlmFormat(McpQtClient::LlmFormat format = McpQtClient::LlmFormat::OpenAI) const;

    // 获取所有服务器的工具（标准 MCP Schema 格式，仅含 name/description/inputSchema）
    // 适用于业务方需要自行组装 LLM 专有结构的场景
    QJsonArray exportAllToolsAsMcpSchema() const;

    // 路由并调用命名空间工具 (返回 QFuture)
    QFuture<McpResult> callToolFuture(const QString& nameSpacedToolName, const QJsonObject& arguments);
    
    // 路由并调用命名空间工具 (使用异步 callback，支持 Progress 回调)
    void callToolAsync(const QString& nameSpacedToolName, const QJsonObject& arguments,
                       std::function<void(McpResult)> callback,
                       McpQtClient::ProgressCallback onProgress = nullptr);

    // 解析 namespaced 工具名，返回 {serverName, originalToolName}
    // 如果解析失败或找不到服务器，返回空 QPair
    QPair<QString, QString> parseToolName(const QString& nameSpacedToolName) const;

private:
    McpServerManager* m_manager{nullptr};
};

} // namespace mcp_qt
