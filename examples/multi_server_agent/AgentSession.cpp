#include "examples/multi_server_agent/AgentSession.h"

#include <QJsonArray>
#include <QTimer>
#include <QJsonDocument>
#include <QDateTime>
#include <QPointer>
#include <iostream>

AgentSession::AgentSession(mcp_qt::McpHost* host,
                           std::shared_ptr<mcp_agent::ILlmBackend> llmBackend,
                           QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_llmBackend(llmBackend)
{
    m_executor = new mcp_agent::LlmAgentExecutor(m_llmBackend, this);

    // 工具调度：McpHost 自动解析 namespaced 名称并路由到对应 client
    m_executor->setToolDispatcher([this](const QString& toolName, const QJsonObject& args, std::function<void(mcp_qt::McpResult)> cb) {
        qInfo().noquote() << "[AgentSession] Tool call:" << toolName;
        m_host->callToolAsync(toolName, args, [this, toolName, cb](mcp_qt::McpResult result) {
            if (result.isError && m_host->reporter()) {
                m_host->reporter()->addError("tool/call", toolName + ": " + result.errorString);
            }
            cb(result);
        });
    });
}

// ============================================================================
// start(): 启动任务
// ============================================================================
void AgentSession::start(const AgentRunOptions& options) {
    m_executor->setDiagnosticContext(options.apiUrl, options.apiKey, options.modelName);
    m_timeoutMs = options.timeoutMs;
    m_finished = false;
    m_taskStarted = false;

    // 因为 McpHost 已经在外部保证了准备就绪，所以直接启动 ReAct
    QTimer::singleShot(0, this, [this, task = options.task]() {
        if (!m_taskStarted) runTask(task);
    });
}

// ============================================================================
// runTask(): 直接从缓存读取工具，启动 ReAct（纯同步，零异步等待）
// ============================================================================
void AgentSession::runTask(const QString& task) {
    if (m_taskStarted) return;
    m_taskStarted = true;

    qInfo().noquote() << "[AgentSession] runTask:" << task;

    QJsonArray tools = m_host->exportAllToolsToLlm();

    if (tools.isEmpty()) {
        finishWithError("tool/discovery", "No tools available", "Ensure MCP servers are online");
        return;
    }

    qInfo().noquote() << "[AgentSession] 启动 ReAct:" << tools.size() << "个工具";
    if (m_host->reporter()) m_host->reporter()->addInfo("tool/discovery", QStringLiteral("%1 tools loaded").arg(tools.size()));

    QPointer<AgentSession> safeThis(this);
    m_executor->run(task, tools, [safeThis](bool ok, QString answer) {
        if (!safeThis) return;
        ok ? safeThis->finishSuccessfully(answer) : safeThis->finishWithError("react/loop", answer, "Check ReAct step");
    });
}

// ============================================================================
// continueConversation(): 多轮对话（直接读缓存）
// ============================================================================
void AgentSession::continueConversation(const QString& task, const QString&) {
    m_finished = false;
    if (m_watchdogTimer) m_watchdogTimer->start(m_timeoutMs * 6);

    QJsonArray tools = m_host->exportAllToolsToLlm();
    QPointer<AgentSession> safeThis(this);
    m_executor->continueRun(task, tools, [safeThis](bool ok, QString answer) {
        if (!safeThis) return;
        ok ? safeThis->finishSuccessfully(answer) : safeThis->finishWithError("react/loop", answer, "Check ReAct step");
    });
}

// ============================================================================
void AgentSession::finishWithError(const QString& stage, const QString& msg, const QString& sug) {
    if (m_finished) return;
    m_finished = true;
    if (m_watchdogTimer) m_watchdogTimer->stop();
    qWarning().noquote() << "[AgentSession] Error:" << msg;
    if (m_host->reporter()) m_host->reporter()->addError(stage, msg, sug);
    emit finished(1);
}

void AgentSession::finishSuccessfully(const QString& msg) {
    if (m_finished) return;
    m_finished = true;
    if (m_watchdogTimer) m_watchdogTimer->stop();
    qInfo().noquote() << "[AgentSession] 完成:" << msg;
    if (m_host->reporter()) {
        m_host->reporter()->addExecutionLogLine(msg);
        m_host->reporter()->addInfo("result/render", msg);
    }
    emit finished(0);
}
