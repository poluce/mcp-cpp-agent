#include <mcp_qt_client/McpServerManager.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QPointer>

namespace mcp_qt {

McpServerManager::McpServerManager(QObject* parent)
    : QObject(parent) {}

McpServerManager::~McpServerManager() {
    closeAll(1000);
}

bool McpServerManager::loadConfigFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open config file:" << filePath;
        return false;
    }
    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        qWarning() << "Failed to parse JSON config:" << parseError.errorString();
        return false;
    }
    if (!doc.isObject()) {
        qWarning() << "Config JSON is not an object";
        return false;
    }
    return loadConfig(doc.object());
}

bool McpServerManager::loadConfig(const QJsonObject& configObject) {
    QJsonObject serversObj;
    if (configObject.contains(QStringLiteral("mcpServers")) && configObject.value(QStringLiteral("mcpServers")).isObject()) {
        serversObj = configObject.value(QStringLiteral("mcpServers")).toObject();
    } else {
        // 如果直接是服务器列表
        serversObj = configObject;
    }

    QSet<QString> loadedServers;
    for (auto it = serversObj.constBegin(); it != serversObj.constEnd(); ++it) {
        QString serverName = it.key();
        if (!it.value().isObject()) continue;
        QJsonObject serverCfg = it.value().toObject();

        if (serverCfg.value(QStringLiteral("disabled")).toBool(false)) {
            continue;
        }

        QMap<QString, QString> processEnv;

        // 注意：系统代理已由 QtProcessStdioTransport 自动探测注入，此处仅处理用户自定义 env
        if (serverCfg.contains(QStringLiteral("env")) && serverCfg.value(QStringLiteral("env")).isObject()) {
            QJsonObject envs = serverCfg.value(QStringLiteral("env")).toObject();
            for (auto envIt = envs.constBegin(); envIt != envs.constEnd(); ++envIt) {
                QString envVal;
                if (envIt.value().isBool()) {
                    envVal = envIt.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
                } else {
                    envVal = envIt.value().toVariant().toString();
                }
                processEnv.insert(envIt.key(), envVal); // 用户自定义 env 覆盖自动提取的代理
            }
        }

        McpQtClientBuilder builder;
        if (!processEnv.isEmpty()) {
            builder.setEnvironment(processEnv);
        }

        QMap<QString, QString> httpHeaders;
        if (serverCfg.contains(QStringLiteral("headers")) && serverCfg.value(QStringLiteral("headers")).isObject()) {
            QJsonObject hdrs = serverCfg.value(QStringLiteral("headers")).toObject();
            for (auto hdrIt = hdrs.constBegin(); hdrIt != hdrs.constEnd(); ++hdrIt) {
                if (hdrIt.value().isString()) {
                    httpHeaders.insert(hdrIt.key(), hdrIt.value().toString());
                }
            }
        }
        if (!httpHeaders.isEmpty()) {
            builder.setHttpHeaders(httpHeaders);
        }

        bool hasTransport = false;

        // 2. 根据是 url 还是 command 建立 transport
        if (serverCfg.contains(QStringLiteral("url"))) {
            QString url = serverCfg.value(QStringLiteral("url")).toString();
            QString type = serverCfg.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("stateless_http")) {
                builder.setTransportStatelessHttp(url);
            } else {
                builder.setTransportHttp(url);
            }
            hasTransport = true;
        } else if (serverCfg.contains(QStringLiteral("command"))) {
            QString command = serverCfg.value(QStringLiteral("command")).toString();
            QStringList args;
            if (serverCfg.contains(QStringLiteral("args")) && serverCfg.value(QStringLiteral("args")).isArray()) {
                QJsonArray argsArr = serverCfg.value(QStringLiteral("args")).toArray();
                for (const auto& argVal : argsArr) {
                    args.append(argVal.toString());
                }
            }
            builder.setTransportStdio(command, args);
            hasTransport = true;
        }

        if (!hasTransport) {
            qWarning() << "Server config for" << serverName << "does not contain url or command";
            continue;
        }

        // 可以设置 Client 名字和重连策略等
        builder.setClientInfo(serverName, QStringLiteral("1.0.0"));
        
        // 3. 构建并异步连接
        auto clientPtr = builder.buildAndConnectAsync();
        if (clientPtr) {
            loadedServers.insert(serverName);
            registerClient(serverName, clientPtr);
        }
    }

    QStringList currentServers = serverNames();
    for (const QString& existing : currentServers) {
        if (!loadedServers.contains(existing)) {
            unregisterClient(existing);
        }
    }

    return true;
}

void McpServerManager::registerClient(const QString& serverName, std::shared_ptr<McpQtClient> client) {
    if (!client) return;
    unregisterClient(serverName);

    m_clients.insert(serverName, client);
    setupClientSignals(serverName, client);
}

void McpServerManager::unregisterClient(const QString& serverName) {
    auto it = m_clients.find(serverName);
    if (it != m_clients.end()) {
        auto client = *it;
        if (client) {
            client->disconnect(this);
            client->close();
        }
        m_clients.erase(it);
    }
}

std::shared_ptr<McpQtClient> McpServerManager::client(const QString& serverName) const {
    return m_clients.value(serverName, nullptr);
}

QHash<QString, std::shared_ptr<McpQtClient>> McpServerManager::clients() const {
    return m_clients;
}

QStringList McpServerManager::serverNames() const {
    return m_clients.keys();
}

void McpServerManager::closeAll(int timeoutMs) {
    for (const auto& client : m_clients) {
        if (client) {
            client->close(timeoutMs);
        }
    }
    m_clients.clear();
}

void McpServerManager::setupClientSignals(const QString& serverName, const std::shared_ptr<McpQtClient>& client) {
    connect(client.get(), &McpQtClient::connected, this, [this, serverName, client]() {
        emit clientConnected(serverName);

        // 连接即预热：自动拉取全部工具并缓存
        if (client->cachedTools().empty()) {
            m_pendingFetchCount.fetch_add(1);
            QPointer<McpServerManager> safeThis(this);
            client->fetchAllToolsAsync([safeThis, serverName, weakClient = std::weak_ptr<McpQtClient>(client)](const std::vector<McpQtTool>& tools) {
                if (!safeThis) return; // Manager 已销毁，安全退出
                int remaining = safeThis->m_pendingFetchCount.fetch_sub(1) - 1;
                qInfo().noquote() << "[McpServerManager]" << serverName << "预热完成:" << tools.size() << "个工具";
                emit safeThis->clientToolsReady(serverName, static_cast<int>(tools.size()));
                if (remaining <= 0) {
                    emit safeThis->allToolsReady();
                }
            });
        } else {
            emit clientToolsReady(serverName, static_cast<int>(client->cachedTools().size()));
        }
    });
    connect(client.get(), &McpQtClient::disconnected, this, [this, serverName]() {
        emit clientDisconnected(serverName);
    });
    connect(client.get(), &McpQtClient::errorOccurred, this, [this, serverName](const QString& message) {
        emit clientErrorOccurred(serverName, message);
    });
    connect(client.get(), &McpQtClient::toolsChanged, this, [this, serverName](const std::vector<mcp_qt::McpQtTool>& newTools) {
        emit clientToolsChanged(serverName, newTools);
    });
    connect(client.get(), &McpQtClient::promptsChanged, this, [this, serverName]() {
        emit clientPromptsChanged(serverName);
    });
}

bool McpServerManager::isAllToolsReady() const {
    return m_pendingFetchCount.load() <= 0;
}

void McpServerManager::startHeartbeat(int intervalMs) {
    if (!m_heartbeatTimer) {
        m_heartbeatTimer = new QTimer(this);
        connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
            for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
                const auto& name = it.key();
                const auto& c = it.value();
                if (!c || !c->isConnected()) continue;
                // 异步 ping，失败时 client 的 auto-reconnect 机制会自动触发
                c->pingAsync([name](bool success, const QString& error) {
                    if (!success) {
                        qWarning().noquote() << "[McpServerManager] Heartbeat ping failed for" << name << ":" << error;
                    }
                });
            }
        });
    }
    m_heartbeatTimer->start(intervalMs);
}

void McpServerManager::stopHeartbeat() {
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }
}

} // namespace mcp_qt
