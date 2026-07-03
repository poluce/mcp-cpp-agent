#include "examples/multi_server_agent/AgentSession.h"

#include <QJsonArray>
#include <QTimer>

#include <algorithm>
#include <iostream>

AgentSession::AgentSession(mcp_qt::McpServerManager* manager,
                           HeuristicToolSelector* selector,
                           DiagnosticReporter* reporter,
                           QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_selector(selector)
    , m_reporter(reporter)
    , m_router(manager) {}

void AgentSession::start(const AgentRunOptions& options) {
    m_timeoutMs = options.timeoutMs;
    m_finished = false;
    m_runStarted = false;
    std::cerr << "[AgentSession] start() called, config=" << options.configPath.toStdString() << "\n";

    QObject::connect(m_manager, &mcp_qt::McpServerManager::clientConnected, this, [this, options](const QString& serverName) {
        std::cerr << "[AgentSession] clientConnected: " << serverName.toStdString() << "\n";
        if (m_reporter) {
            m_reporter->addExecutionLogLine(QStringLiteral("Server connected: %1").arg(serverName));
        }
        if (!m_runStarted) {
            beginRunAgainstCurrentClients(options.task, options.serverFilter);
        }
    });

    QObject::connect(m_manager, &mcp_qt::McpServerManager::clientErrorOccurred, this, [this](const QString& serverName, const QString& error) {
        std::cerr << "[AgentSession] clientErrorOccurred: " << serverName.toStdString() << " err=" << error.toStdString() << "\n";
        if (m_reporter) {
            m_reporter->addProblem(QStringLiteral("server/connect"),
                                   QStringLiteral("Server '%1' reported error: %2").arg(serverName, error),
                                   QStringLiteral("Expose a clearer aggregate readiness API after config load"));
        }
    });

    bool loadOk = m_manager->loadConfigFile(options.configPath);
    std::cerr << "[AgentSession] loadConfigFile returned " << (loadOk ? "true" : "false") << "\n";
    std::cerr << "[AgentSession] registered servers: " << m_manager->serverNames().join(",").toStdString() << "\n";

    if (!loadOk) {
        finishWithError(QStringLiteral("config/load"),
                        QStringLiteral("Failed to load config file"),
                        QStringLiteral("Check the path and JSON structure"));
        return;
    }

    if (m_reporter) {
        m_reporter->addExecutionLogLine(QStringLiteral("Loaded config file %1").arg(options.configPath));
        m_reporter->addExecutionLogLine(QStringLiteral("Waiting for server connection events"));
    }

    QTimer::singleShot(options.timeoutMs, this, [this, options]() {
        std::cerr << "[AgentSession] connection wait timeout elapsed\n";
        if (!m_runStarted) {
            if (m_reporter) {
                m_reporter->addExecutionLogLine(QStringLiteral("Connection wait elapsed; continuing with currently registered clients"));
            }
            beginRunAgainstCurrentClients(options.task, options.serverFilter);
        }
    });

    QTimer::singleShot(options.timeoutMs * 4, this, [this]() {
        std::cerr << "[AgentSession] overall watchdog timeout\n";
        if (!m_finished) {
            finishWithError(QStringLiteral("lifecycle"),
                            QStringLiteral("Overall agent run timed out before completion"),
                            QStringLiteral("Real stdio flows still have blocking behavior; investigate transport/session readiness and call timeout paths"));
        }
    });
}

void AgentSession::runAgainstCurrentClients(const QString& task, const QString& serverFilter, int) {
    beginRunAgainstCurrentClients(task, serverFilter);
}

void AgentSession::beginRunAgainstCurrentClients(const QString& task, const QString& serverFilter) {
    m_runStarted = true;
    std::cerr << "[AgentSession] beginRunAgainstCurrentClients\n";

    QStringList targetServers = m_manager ? m_manager->serverNames() : QStringList{};
    if (!serverFilter.isEmpty()) {
        targetServers = QStringList{serverFilter};
    }

    if (m_reporter) {
        m_reporter->addExecutionLogLine(QStringLiteral("Evaluating %1 server(s) for task: %2").arg(targetServers.size()).arg(task));
    }

    std::vector<ToolCandidateScore> allCandidates;
    for (const QString& serverName : targetServers) {
        auto client = m_manager ? m_manager->client(serverName) : nullptr;
        if (!client) {
            std::cerr << "[AgentSession] no client for " << serverName.toStdString() << "\n";
            if (m_reporter) {
                m_reporter->addExecutionLogLine(QStringLiteral("Skipping missing client for server %1").arg(serverName));
            }
            continue;
        }

        std::cerr << "[AgentSession] checking cachedTools for " << serverName.toStdString() << "\n";
        auto tools = client->cachedTools();
        if (tools.empty()) {
            std::cerr << "[AgentSession] cachedTools empty, fetching for " << serverName.toStdString() << "\n";
            if (m_reporter) {
                m_reporter->addExecutionLogLine(QStringLiteral("Server %1 has no cached tools; fetching from MCP").arg(serverName));
            }
            tools = client->fetchAllTools(m_timeoutMs);
            std::cerr << "[AgentSession] fetchAllTools returned " << tools.size() << " tools\n";
        }
        if (tools.empty()) {
            if (m_reporter) {
                m_reporter->addExecutionLogLine(QStringLiteral("Server %1 has no cached tools").arg(serverName));
            }
            continue;
        }

        if (m_reporter) {
            m_reporter->addObservation(QStringLiteral("tool/discovery"),
                                       QStringLiteral("Loaded %1 tools from %2").arg(tools.size()).arg(serverName));
        }

        auto ranked = m_selector->rankTools(task, serverName, tools);
        allCandidates.insert(allCandidates.end(), ranked.candidates.begin(), ranked.candidates.end());
    }

    if (allCandidates.empty()) {
        finishWithError(QStringLiteral("tool/discovery"),
                        QStringLiteral("No discovered tools were available from the selected servers"),
                        QStringLiteral("Expose a clearer readiness signal before routing"));
        return;
    }

    std::sort(allCandidates.begin(), allCandidates.end(), [](const ToolCandidateScore& a, const ToolCandidateScore& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.namespacedToolName < b.namespacedToolName;
    });

    const int previewCount = std::min<int>(3, static_cast<int>(allCandidates.size()));
    for (int i = 0; i < previewCount; ++i) {
        const auto& candidate = allCandidates[static_cast<size_t>(i)];
        if (m_reporter) {
            m_reporter->addExecutionLogLine(
                QStringLiteral("Candidate %1: %2 (score=%3, reasons=%4)")
                    .arg(i + 1)
                    .arg(candidate.namespacedToolName)
                    .arg(candidate.score)
                    .arg(candidate.reasons.join(QStringLiteral("; "))));
        }
    }

    const ToolCandidateScore best = allCandidates.front();
    if (best.score <= 0) {
        finishWithError(QStringLiteral("tool/selection"),
                        QStringLiteral("No suitable tool scored above zero"),
                        QStringLiteral("Provide richer tool metadata for agent-facing discovery"));
        return;
    }

    QString argError;
    const QJsonObject args = buildSafeArguments(best, task, &argError);
    if (!argError.isEmpty()) {
        finishWithError(QStringLiteral("tool/selection"),
                        argError,
                        QStringLiteral("Add higher-level helpers for safe schema-to-argument mapping"));
        return;
    }

    if (m_reporter) {
        m_reporter->addExecutionLogLine(QStringLiteral("Selected tool %1 with score %2").arg(best.namespacedToolName).arg(best.score));
        m_reporter->addObservation(QStringLiteral("tool/call"),
                                   QStringLiteral("Calling %1").arg(best.namespacedToolName));
    }

    std::cerr << "[AgentSession] calling tool " << best.namespacedToolName.toStdString() << "\n";
    m_router.callToolAsync(best.namespacedToolName, args, [this, best](mcp_qt::McpResult result) {
        std::cerr << "[AgentSession] tool call returned, isError=" << result.isError << "\n";
        if (result.isError) {
            finishWithError(QStringLiteral("tool/call"),
                            QStringLiteral("Tool call failed: %1").arg(result.errorString),
                            QStringLiteral("Surface structured invocation errors rather than empty results"));
            return;
        }
        finishSuccessfully(QStringLiteral("Tool call succeeded for %1").arg(best.namespacedToolName));
    });
}

QJsonObject AgentSession::buildSafeArguments(const ToolCandidateScore& candidate, const QString& task, QString* error) const {
    const QJsonObject properties = candidate.inputSchema.value(QStringLiteral("properties")).toObject();
    const QJsonArray required = candidate.inputSchema.value(QStringLiteral("required")).toArray();
    static const QStringList safeFields{
        QStringLiteral("query"),
        QStringLiteral("q"),
        QStringLiteral("keyword"),
        QStringLiteral("text"),
        QStringLiteral("prompt")
    };

    QJsonObject args;
    for (const QString& field : safeFields) {
        if (properties.contains(field)) {
            args[field] = task;
        }
    }

    for (const auto& requiredValue : required) {
        const QString field = requiredValue.toString();
        if (!args.contains(field)) {
            if (error) {
                *error = QStringLiteral("Cannot safely construct required argument '%1' for %2")
                    .arg(field, candidate.namespacedToolName);
            }
            return QJsonObject{};
        }
    }

    return args;
}

void AgentSession::finishWithError(const QString& stage, const QString& message, const QString& suggestion) {
    if (m_finished) {
        return;
    }
    m_finished = true;
    std::cerr << "[AgentSession] finishWithError stage=" << stage.toStdString() << " msg=" << message.toStdString() << "\n";
    if (m_reporter) {
        m_reporter->addProblem(stage, message, suggestion);
    }
    emit finished(1);
}

void AgentSession::finishSuccessfully(const QString& message) {
    if (m_finished) {
        return;
    }
    m_finished = true;
    if (m_reporter) {
        m_reporter->addExecutionLogLine(message);
        m_reporter->addObservation(QStringLiteral("result/render"), message);
    }
    emit finished(0);
}
