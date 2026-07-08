#pragma once

#include "ILlmBackend.h"
#include "LlmAgentExecutor.h"
#include "mcp_qt_client/McpHost.h"

#include <QObject>
#include <QJsonObject>
#include <memory>

struct AgentRunOptions {
    QString configPath;
    QString task;
    QString serverFilter;
    int timeoutMs{30000};

    // LLM 配置
    bool useRealLlm{false};
    QString apiUrl;
    QString apiKey;
    QString modelName;
};

class AgentSession : public QObject {
    Q_OBJECT

public:
    AgentSession(mcp_qt::McpHost* host,
                 std::shared_ptr<mcp_agent::ILlmBackend> llmBackend,
                 QObject* parent = nullptr);

    void start(const AgentRunOptions& options);
    void continueConversation(const QString& task, const QString& serverFilter = "");
    mcp_agent::LlmAgentExecutor* executor() const { return m_executor; }

signals:
    void finished(int exitCode);

private:
    void runTask(const QString& task);
    void finishWithError(const QString& stage, const QString& message, const QString& suggestion = QString());
    void finishSuccessfully(const QString& message);

    mcp_qt::McpHost* m_host{nullptr};
    std::shared_ptr<mcp_agent::ILlmBackend> m_llmBackend;

    mcp_agent::LlmAgentExecutor* m_executor{nullptr};

    int m_timeoutMs{30000};
    bool m_finished{false};
    bool m_taskStarted{false};
    QTimer* m_watchdogTimer{nullptr};
};
