#include "examples/multi_server_agent/AgentSession.h"

#include <QJsonArray>
#include <QTimer>
#include <QJsonDocument>
#include <QDateTime>
#include <QPointer>
#include <iostream>

AgentSession::AgentSession(mcp_qt::McpServerManager* manager,
                           std::shared_ptr<mcp_agent::ILlmBackend> llmBackend,
                           DiagnosticReporter* reporter,
                           QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_llmBackend(llmBackend)
    , m_reporter(reporter)
    , m_router(manager)
{
    m_executor = new mcp_agent::LlmAgentExecutor(m_llmBackend, this);

    // 工具调度：McpToolRouter 自动解析 namespaced 名称并路由到对应 client
    m_executor->setToolDispatcher([this](const QString& toolName, const QJsonObject& args, std::function<void(mcp_qt::McpResult)> cb) {
        qInfo().noquote() << "[AgentSession] Tool call:" << toolName;
        m_router.callToolAsync(toolName, args, [this, toolName, cb](mcp_qt::McpResult result) {
            if (result.isError && m_reporter) {
                m_reporter->addProblem("tool/call", toolName + ": " + result.errorString);
            }
            cb(result);
        });
    });
}

// ============================================================================
// start(): 加载配置 → 等待预热 → 运行任务
// ============================================================================
void AgentSession::start(const AgentRunOptions& options) {
    m_executor->setDiagnosticContext(options.apiUrl, options.apiKey, options.modelName);
    m_timeoutMs = options.timeoutMs;
    m_finished = false;
    m_taskStarted = false;

    // 看门狗（根据要求已禁用全局超时限制）
    // m_watchdogTimer = new QTimer(this);
    // m_watchdogTimer->setSingleShot(true);
    // connect(m_watchdogTimer, &QTimer::timeout, this, [this]() {
    //     if (!m_finished) finishWithError("lifecycle", "Agent run timed out", "Check network or tool blocking");
    // });
    // m_watchdogTimer->start(options.timeoutMs * 6);

    // 不再这里加载配置。由 AgentMainWindow 提前加载。
    // 如果所有服务器都已经连接且工具就绪，直接返回就绪，否则等待信号
    bool anyNotConnected = false;
    for (const auto& name : m_manager->serverNames()) {
        auto c = m_manager->client(name);
        if (c && !c->isConnected()) {
            anyNotConnected = true;
            break;
        }
    }

    if (!anyNotConnected && m_manager->isAllToolsReady()) {
        QTimer::singleShot(0, this, [this, task = options.task]() {
            if (!m_taskStarted) runTask(task);
        });
    }

    qInfo().noquote() << "[AgentSession] 已注册服务器:" << m_manager->serverNames().join(", ");

    // 连接进度心跳
    QTimer* heartbeat = new QTimer(this);
    connect(heartbeat, &QTimer::timeout, this, [this, heartbeat]() {
        if (m_taskStarted || m_finished) { heartbeat->stop(); heartbeat->deleteLater(); return; }
        QStringList status;
        for (const auto& name : m_manager->serverNames()) {
            auto c = m_manager->client(name);
            status << QString("%1:%2").arg(name, (c && c->isConnected()) ? "🟢" : "⌛");
        }
        qInfo().noquote() << "[AgentSession] 连接 ->" << status.join(" | ");
    });
    heartbeat->start(1500);

    // 策略 1: 所有服务器预热完成 → 如果全部在线则立即起跑
    connect(m_manager, &mcp_qt::McpServerManager::allToolsReady, this, [this, task = options.task]() {
        if (m_taskStarted) return;
        bool allOnline = true;
        for (const auto& name : m_manager->serverNames()) {
            auto c = m_manager->client(name);
            if (!c || !c->isConnected()) { allOnline = false; break; }
        }
        if (allOnline) runTask(task);
    });

    // 策略 2: 单个服务器连接成功 → 如果全部已连上也起跑（兜底，防止 allToolsReady 信号丢失）
    connect(m_manager, &mcp_qt::McpServerManager::clientConnected, this, [this, task = options.task](const QString&) {
        if (m_taskStarted) return;
        bool allOnline = true;
        for (const auto& name : m_manager->serverNames()) {
            auto c = m_manager->client(name);
            if (!c || !c->isConnected()) { allOnline = false; break; }
        }
        if (allOnline && m_manager->isAllToolsReady()) runTask(task);
    });

    // 策略 3: 超时强制起跑（带当前已有的缓存）
    QTimer::singleShot(options.timeoutMs, this, [this, task = options.task]() {
        if (!m_taskStarted) {
            qWarning().noquote() << "[AgentSession] 连接超时，带当前缓存起跑";
            runTask(task);
        }
    });
}

// ============================================================================
// runTask(): 直接从缓存读取工具，启动 ReAct（纯同步，零异步等待）
// ============================================================================
void AgentSession::runTask(const QString& task) {
    if (m_taskStarted) return;
    m_taskStarted = true;

    qInfo().noquote() << "[AgentSession] runTask:" << task;

    QJsonArray tools = m_router.exportAllToolsToLlmFormat();

    if (tools.isEmpty()) {
        finishWithError("tool/discovery", "No tools available", "Ensure MCP servers are online");
        return;
    }

    qInfo().noquote() << "[AgentSession] 启动 ReAct:" << tools.size() << "个工具";
    if (m_reporter) m_reporter->addObservation("tool/discovery", QStringLiteral("%1 tools loaded").arg(tools.size()));

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

    QJsonArray tools = m_router.exportAllToolsToLlmFormat();
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
    if (m_reporter) m_reporter->addProblem(stage, msg, sug);
    emit finished(1);
}

void AgentSession::finishSuccessfully(const QString& msg) {
    if (m_finished) return;
    m_finished = true;
    if (m_watchdogTimer) m_watchdogTimer->stop();
    qInfo().noquote() << "[AgentSession] 完成:" << msg;
    if (m_reporter) {
        m_reporter->addExecutionLogLine(msg);
        m_reporter->addObservation("result/render", msg);
    }
    emit finished(0);
}
