#include "examples/multi_server_agent/AgentSession.h"

#include <QJsonArray>
#include <QTimer>
#include <QJsonDocument>
#include <QDateTime>
#include <algorithm>
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
    // 创建 ReAct 环路执行引擎
    m_executor = new mcp_agent::LlmAgentExecutor(m_llmBackend, this);

    // 绑定大模型决策后的工具调度回调
    m_executor->setToolDispatcher([this](const QString& namespacedToolName, const QJsonObject& args, std::function<void(mcp_qt::McpResult)> callback) {
        qInfo().noquote() << "[AgentSession] Dispatching tool call via SDK:" << namespacedToolName 
                          << QJsonDocument(args).toJson(QJsonDocument::Compact);

        if (m_reporter) {
            m_reporter->addObservation(QStringLiteral("tool/call"), 
                                       QStringLiteral("Calling %1").arg(namespacedToolName));
        }

        // McpToolRouter 原生支持以 namespacedName (serverName_toolName) 进行异步分发调用
        m_router.callToolAsync(namespacedToolName, args, [this, namespacedToolName, args, callback](mcp_qt::McpResult result) {
            if (result.isError) {
                QString detailedObsErr = QString(
                    "无法派发本轮 MCP 工具调用: %1\n"
                    "【排查状态信息】\n"
                    " - 发生时间: %2\n"
                    " - 当前流程阶段: [TOOL_DISPATCH - 工具分发调用]\n"
                    " - 尝试调用工具: %3\n"
                    " - 工具输入参数: %4\n"
                    " - 当前可用服务端: %5\n"
                    " - 诊断排查建议: 请确认该工具名是否输入正确；如使用 Stdio 连接，请确保配置文件 examples_config.json 中的 server command 可正常启动且无输出阻断。"
                ).arg(result.errorString)
                 .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"))
                 .arg(namespacedToolName)
                 .arg(QJsonDocument(args).toJson(QJsonDocument::Compact))
                 .arg(m_manager ? m_manager->serverNames().join(", ") : "None");
                
                result.errorString = detailedObsErr;
            }
            callback(result);
        });
    });
}

void AgentSession::start(const AgentRunOptions& options) {
    m_executor->setDiagnosticContext(options.apiUrl, options.apiKey, options.modelName);
    m_timeoutMs = options.timeoutMs;
    m_finished = false;
    m_runStarted = false;
    
    qInfo().noquote() << "[AgentSession] start() called, config=" << options.configPath;

    // 监听连接成功的事件并打印
    QObject::connect(m_manager, &mcp_qt::McpServerManager::clientConnected, this, [this, options](const QString& serverName) {
        qInfo().noquote() << "[AgentSession] 成功连通并激活服务端:" << serverName;
        if (m_reporter) {
            m_reporter->addExecutionLogLine(QStringLiteral("Server connected: %1").arg(serverName));
        }
        if (!m_runStarted) {
            // 如果所配的全部服务器都已提前连上，可以直接起跑
            bool allConnected = true;
            QStringList allNames = m_manager->serverNames();
            for (const auto& name : allNames) {
                auto client = m_manager->client(name);
                if (!client || !client->isConnected()) {
                    allConnected = false;
                    break;
                }
            }
            if (allConnected && !allNames.isEmpty()) {
                qInfo().noquote() << "[AgentSession] 所有配置的服务器已全部提前连接就绪，立即起跑任务！";
                beginRunAgainstCurrentClients(options.task, options.serverFilter);
            }
        }
    });

    QObject::connect(m_manager, &mcp_qt::McpServerManager::clientErrorOccurred, this, [this](const QString& serverName, const QString& error) {
        qInfo().noquote() << "[AgentSession] 服务端报错事件: " << serverName << "err=" << error;
        if (m_reporter) {
            m_reporter->addProblem(QStringLiteral("server/connect"),
                                   QStringLiteral("Server '") + serverName + QStringLiteral("' reported error: ") + error,
                                   QStringLiteral("Check backend logs"));
        }
    });

    bool loadOk = m_manager->loadConfigFile(options.configPath);
    qInfo().noquote() << "[AgentSession] loadConfigFile returned" << (loadOk ? "true" : "false");
    qInfo().noquote() << "[AgentSession] 当前已注册待连接的服务器列表:" << m_manager->serverNames().join(", ");

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

    // 🌟 定时心跳轮询进度打印器：解决“连接等待期间15秒日志一片死寂、非实时”的痛点！
    QTimer* progressTimer = new QTimer(this);
    QObject::connect(progressTimer, &QTimer::timeout, this, [this, progressTimer]() {
        if (m_runStarted || m_finished) {
            progressTimer->stop();
            progressTimer->deleteLater();
            return;
        }
        QStringList statusList;
        QStringList allNames = m_manager->serverNames();
        for (const auto& name : allNames) {
            auto client = m_manager->client(name);
            bool conn = client && client->isConnected();
            statusList.append(QString("%1: %2").arg(name, conn ? QStringLiteral("🟢在线") : QStringLiteral("⌛连接中")));
        }
        qInfo().noquote() << "[AgentSession] 服务端网络握手进度 ->" << statusList.join(" | ");
    });
    progressTimer->start(1500); // 每1.5秒刷新打印一次

    // 超时哨兵：等待 maximum timeoutMs
    QTimer::singleShot(options.timeoutMs, this, [this, options]() {
        qInfo().noquote() << "[AgentSession] connection wait timeout elapsed";
        if (!m_runStarted) {
            qInfo().noquote() << "[AgentSession] 连接等待超时，但为保任务继续，带上当前已在线的服务器强制起跑！";
            if (m_reporter) {
                m_reporter->addExecutionLogLine(QStringLiteral("Connection wait elapsed; continuing with currently registered clients"));
            }
            beginRunAgainstCurrentClients(options.task, options.serverFilter);
        }
    });

    // 全局看门狗
    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setSingleShot(true);
    connect(m_watchdogTimer, &QTimer::timeout, this, [this]() {
        qInfo().noquote() << "[AgentSession] overall watchdog timeout";
        if (!m_finished) {
            finishWithError(QStringLiteral("lifecycle"),
                            QStringLiteral("Overall agent ReAct run timed out before completion"),
                            QStringLiteral("Investigate network delay or tool blocking"));
        }
    });
    m_watchdogTimer->start(options.timeoutMs * 6);
}

void AgentSession::runAgainstCurrentClients(const QString& task, const QString& serverFilter, int) {
    beginRunAgainstCurrentClients(task, serverFilter);
}

void AgentSession::beginRunAgainstCurrentClients(const QString& task, const QString& serverFilter) {
    if (m_runStarted) return;
    m_runStarted = true;
    
    qInfo().noquote() << "[AgentSession] beginRunAgainstCurrentClients";

    QStringList targetServers = m_manager ? m_manager->serverNames() : QStringList{};
    if (!serverFilter.isEmpty()) {
        targetServers = QStringList{serverFilter};
    }

    if (m_reporter) {
        m_reporter->addExecutionLogLine(QStringLiteral("Evaluating %1 server(s) for task: %2").arg(targetServers.size()).arg(task));
    }

    // 1. 从所有连接到的 MCP 服务端拉取并缓存工具定义，组装大模型专用的 availableTools Json 数组
    QJsonArray availableTools;
    for (const QString& serverName : targetServers) {
        auto client = m_manager ? m_manager->client(serverName) : nullptr;
        if (!client) {
            qInfo().noquote() << "[AgentSession] no client for" << serverName;
            continue;
        }

        // 🌟 核心修复：如果客户端根本没有连接成功（处于离线状态），强行跳过，防止同步拉取工具导致主线程卡死！
        if (!client->isConnected()) {
            qInfo().noquote() << "[AgentSession]" << serverName << "当前处于离线/握手未就绪状态，跳过工具拉取以防主线程卡死。";
            continue;
        }

        qInfo().noquote() << "[AgentSession] checking cachedTools for" << serverName;
        auto tools = client->cachedTools();
        if (tools.empty()) {
            qInfo().noquote() << "[AgentSession] cachedTools empty, fetching from remote MCP for" << serverName;
            if (m_reporter) {
                m_reporter->addExecutionLogLine(QStringLiteral("Server %1 has no cached tools; fetching from MCP").arg(serverName));
            }
            tools = client->fetchAllTools(m_timeoutMs);
            qInfo().noquote() << "[AgentSession] fetchAllTools returned" << tools.size() << "tools for" << serverName;
        }

        if (tools.empty()) {
            if (m_reporter) {
                m_reporter->addExecutionLogLine(QStringLiteral("Server %1 has no tools").arg(serverName));
            }
            continue;
        }

        if (m_reporter) {
            m_reporter->addObservation(QStringLiteral("tool/discovery"),
                                       QStringLiteral("Loaded %1 tools from %2").arg(tools.size()).arg(serverName));
        }

        for (const auto& t : tools) {
            QJsonObject tObj;
            // 组合为 namespaced 命名：如 mock-search-server_search_items
            tObj["name"] = serverName + "_" + t.name;
            tObj["description"] = t.description;
            tObj["inputSchema"] = t.inputSchema;
            availableTools.append(tObj);
        }
    }

    if (availableTools.isEmpty()) {
        finishWithError(QStringLiteral("tool/discovery"),
                        QStringLiteral("No discovered tools were available from the selected servers"),
                        QStringLiteral("Ensure MCP server endpoints are online"));
        return;
    }

    // 2. 启动 ReAct 引擎
    m_executor->run(task, availableTools, [this](bool success, QString finalAnswer) {
        if (!success) {
            finishWithError(QStringLiteral("react/loop"), 
                            finalAnswer, 
                            QStringLiteral("Analyze ReAct step failure or incorrect arguments"));
        } else {
            finishSuccessfully(finalAnswer);
        }
    });
}

void AgentSession::continueConversation(const QString& task, const QString& serverFilter) {
    m_finished = false;

    qInfo().noquote() << "[AgentSession] continueConversation:" << task;

    QStringList targetServers = m_manager ? m_manager->serverNames() : QStringList{};
    if (!serverFilter.isEmpty()) {
        targetServers = QStringList{serverFilter};
    }

    QJsonArray availableTools;
    for (const QString& serverName : targetServers) {
        auto client = m_manager ? m_manager->client(serverName) : nullptr;
        if (!client || !client->isConnected()) {
            continue;
        }

        auto tools = client->cachedTools();
        for (const auto& t : tools) {
            QJsonObject tObj;
            tObj["name"] = serverName + "_" + t.name;
            tObj["description"] = t.description;
            tObj["inputSchema"] = t.inputSchema;
            availableTools.append(tObj);
        }
    }

    if (m_watchdogTimer) {
        m_watchdogTimer->start(m_timeoutMs * 6);
    }

    m_executor->continueRun(task, availableTools, [this](bool success, QString finalAnswer) {
        if (!success) {
            finishWithError(QStringLiteral("react/loop"), 
                            finalAnswer, 
                            QStringLiteral("Analyze ReAct step failure or incorrect arguments"));
        } else {
            finishSuccessfully(finalAnswer);
        }
    });
}

void AgentSession::finishWithError(const QString& stage, const QString& message, const QString& suggestion) {
    if (m_finished) {
        return;
    }
    m_finished = true;
    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
    }
    qInfo().noquote() << "[AgentSession] finishWithError stage=" << stage << "msg=" << message;
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
    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
    }
    qInfo().noquote() << "[AgentSession] finishSuccessfully message=" << message;
    if (m_reporter) {
        m_reporter->addExecutionLogLine(message);
        m_reporter->addObservation(QStringLiteral("result/render"), message);
    }
    emit finished(0);
}
