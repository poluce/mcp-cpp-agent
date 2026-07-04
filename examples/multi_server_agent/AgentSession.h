#pragma once

#include "examples/multi_server_agent/DiagnosticReporter.h"
#include "ILlmBackend.h"
#include "LlmAgentExecutor.h"
#include "mcp_qt_client/McpServerManager.h"
#include "mcp_qt_client/McpToolRouter.h"

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
    AgentSession(mcp_qt::McpServerManager* manager,
                 std::shared_ptr<mcp_agent::ILlmBackend> llmBackend,
                 DiagnosticReporter* reporter,
                 QObject* parent = nullptr);

    void start(const AgentRunOptions& options);
    void continueConversation(const QString& task, const QString& serverFilter = "");
    mcp_agent::LlmAgentExecutor* executor() const { return m_executor; }

signals:
    void finished(int exitCode);

private:
    void runTask(const QString& task);
    void finishWithError(const QString& stage, const QString& message, const QString& suggestion);
    void finishSuccessfully(const QString& message);

    mcp_qt::McpServerManager* m_manager{nullptr};
    std::shared_ptr<mcp_agent::ILlmBackend> m_llmBackend;
    DiagnosticReporter* m_reporter{nullptr};
    mcp_qt::McpToolRouter m_router;

    mcp_agent::LlmAgentExecutor* m_executor{nullptr};

    int m_timeoutMs{30000};
    bool m_finished{false};
    bool m_taskStarted{false};
    QTimer* m_watchdogTimer{nullptr};
};
