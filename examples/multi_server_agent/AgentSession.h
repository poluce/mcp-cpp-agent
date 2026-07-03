#pragma once

#include "examples/multi_server_agent/DiagnosticReporter.h"
#include "examples/multi_server_agent/HeuristicToolSelector.h"
#include "mcp_qt_client/McpServerManager.h"
#include "mcp_qt_client/McpToolRouter.h"

#include <QObject>
#include <QJsonObject>

struct AgentRunOptions {
    QString configPath;
    QString task;
    QString serverFilter;
    int timeoutMs{30000};
};

class AgentSession : public QObject {
    Q_OBJECT

public:
    AgentSession(mcp_qt::McpServerManager* manager,
                 HeuristicToolSelector* selector,
                 DiagnosticReporter* reporter,
                 QObject* parent = nullptr);

    void start(const AgentRunOptions& options);
    void runAgainstCurrentClients(const QString& task, const QString& serverFilter, int timeoutMs);

signals:
    void finished(int exitCode);

private:
    void beginRunAgainstCurrentClients(const QString& task, const QString& serverFilter);
    QJsonObject buildSafeArguments(const ToolCandidateScore& candidate, const QString& task, QString* error) const;
    void finishWithError(const QString& stage, const QString& message, const QString& suggestion);
    void finishSuccessfully(const QString& message);

    mcp_qt::McpServerManager* m_manager{nullptr};
    HeuristicToolSelector* m_selector{nullptr};
    DiagnosticReporter* m_reporter{nullptr};
    mcp_qt::McpToolRouter m_router;
    int m_timeoutMs{30000};
    bool m_finished{false};
    bool m_runStarted{false};
};
