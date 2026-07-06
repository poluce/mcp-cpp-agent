#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <functional>

namespace mcp_agent {

struct LlmDecision {
    bool isToolCall{false};
    QString toolCallId; // 🌟 保存大模型生成的唯一 tool_call_id
    QString toolName;
    QJsonObject toolArguments;
    QString thought;
    QString reasoningContent;
    QString finalAnswer;
};

struct LlmMessage {
    QString role; // "system", "user", "assistant", "tool"
    QString content;
    QString reasoningContent;
    QString toolCallId; // 供 OpenAI 协议关联使用
    QString name;       // 标识 tool 名字
    
    // 🌟 新增字段，用于在多轮对话中向 LLM 服务端准确还原 tool_calls 状态以避开 HTTP 400 Bad Request 错误
    QString toolName;
    QJsonObject toolArguments;
};

class ILlmBackend {
public:
    virtual ~ILlmBackend() = default;

    /**
     * @brief 请求大模型决策（异步回调）
     * 
     * @param history 对话历史
     * @param availableTools 从 MCP 服务器获取的可用工具 Schema 列表
     * @param callback 回调函数
     */
    virtual void requestDecision(
        const QList<LlmMessage>& history,
        const QJsonArray& availableTools,
        std::function<void(bool success, LlmDecision decision, QString err)> callback
    ) = 0;
};

} // namespace mcp_agent
