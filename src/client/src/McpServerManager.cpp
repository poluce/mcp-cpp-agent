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

bool McpServerManager::loadServers(std::shared_ptr<IMcpConfigLoader> loader) {
    if (!loader) return false;
    return loadServers(loader->load());
}

#include <QNetworkAccessManager>
#include <QNetworkReply>

bool McpServerManager::loadServers(const QList<McpServerConfig>& configs) {
    QSet<QString> loadedServers;

    for (const auto& cfg : configs) {
        if (cfg.disabled) continue;
        loadedServers.insert(cfg.serverName);

        if (!cfg.url.isEmpty()) {
            processHttpServerConfig(cfg);
        } else if (!cfg.command.isEmpty()) {
            McpQtClientBuilder builder;
            if (!cfg.env.isEmpty()) builder.setEnvironment(cfg.env);
            if (!cfg.headers.isEmpty()) builder.setHttpHeaders(cfg.headers);
            builder.setTransportStdio(cfg.command, cfg.args);
            builder.setClientInfo(cfg.serverName, QStringLiteral("1.0.0"));
            if (!cfg.nameSpace.isEmpty()) builder.setNamespace(cfg.nameSpace);
            
            auto clientPtr = builder.buildAndConnectAsync();
            if (clientPtr) registerClient(cfg.serverName, clientPtr);
        } else {
            qWarning() << "Server config for" << cfg.serverName << "does not contain url or command";
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

void McpServerManager::processHttpServerConfig(const McpServerConfig& cfg) {
    QPointer<McpServerManager> safeThis(this);
    auto buildAndRegister = [safeThis](const McpServerConfig& c) {
        if (!safeThis) return;
        McpQtClientBuilder builder;
        if (!c.env.isEmpty()) builder.setEnvironment(c.env);
        if (!c.headers.isEmpty()) builder.setHttpHeaders(c.headers);
        if (c.type == QStringLiteral("stateless_http")) {
            builder.setTransportStatelessHttp(c.url);
        } else {
            builder.setTransportHttp(c.url);
        }
        builder.setClientInfo(c.serverName, QStringLiteral("1.0.0"));
        if (!c.nameSpace.isEmpty()) builder.setNamespace(c.nameSpace);
        
        auto clientPtr = builder.buildAndConnectAsync();
        if (clientPtr) safeThis->registerClient(c.serverName, clientPtr);
    };

    if (cfg.type == QStringLiteral("stateless_http") || cfg.type == QStringLiteral("http")) {
        buildAndRegister(cfg);
    } else {
        // Auto negotiate via HEAD request
        m_serverStates.insert(cfg.serverName, McpServerState::Pending);
        QNetworkAccessManager* nam = new QNetworkAccessManager(this);
        QNetworkRequest req(QUrl(cfg.url));
        for (auto it = cfg.headers.constBegin(); it != cfg.headers.constEnd(); ++it) {
            req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }
        QNetworkReply* reply = nam->head(req);
        connect(reply, &QNetworkReply::finished, this, [safeThis, cfg, reply, nam, buildAndRegister]() {
            if (!safeThis) {
                reply->deleteLater();
                nam->deleteLater();
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                safeThis->updateServerState(cfg.serverName, McpServerState::Error);
                emit safeThis->clientErrorOccurred(cfg.serverName, mcp_qt::McpError{static_cast<int>(reply->error()), reply->errorString(), QJsonObject{}});
                if (safeThis->isAllToolsReady()) {
                    emit safeThis->allToolsReady();
                }
                reply->deleteLater();
                nam->deleteLater();
                return;
            }
            McpServerConfig newCfg = cfg;
            QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            if (contentType.contains("text/event-stream", Qt::CaseInsensitive)) {
                newCfg.type = QStringLiteral("http");
            } else {
                newCfg.type = QStringLiteral("stateless_http");
            }
            reply->deleteLater();
            nam->deleteLater();
            buildAndRegister(newCfg);
        });
    }
}


void McpServerManager::registerClient(const QString& serverName, std::shared_ptr<McpQtClient> client) {
    if (!client) return;
    unregisterClient(serverName);

    m_clients.insert(serverName, client);
    m_serverStates.insert(serverName, McpServerState::Pending);
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
        m_serverStates.remove(serverName);
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

void McpServerManager::updateServerState(const QString& serverName, McpServerState state) {
    if (m_serverStates.value(serverName) != state) {
        m_serverStates[serverName] = state;
        emit clientStateChanged(serverName, state);
    }
}

void McpServerManager::setupClientSignals(const QString& serverName, const std::shared_ptr<McpQtClient>& client) {
    connect(client.get(), &McpQtClient::connected, this, [this, serverName, client]() {
        updateServerState(serverName, McpServerState::Connecting);
        emit clientConnected(serverName);

        // 连接即预热：自动拉取全部工具并缓存
        if (client->cachedTools().empty()) {
            QPointer<McpServerManager> safeThis(this);
            client->fetchAllToolsAsync([safeThis, serverName, weakClient = std::weak_ptr<McpQtClient>(client)](const std::vector<McpQtTool>& tools) {
                if (!safeThis) return; // Manager 已销毁，安全退出
                qInfo().noquote() << "[McpServerManager]" << serverName << "预热完成:" << tools.size() << "个工具";
                emit safeThis->clientToolsReady(serverName, static_cast<int>(tools.size()));
                safeThis->updateServerState(serverName, McpServerState::Ready);
                if (safeThis->isAllToolsReady()) {
                    emit safeThis->allToolsReady();
                }
            });
        } else {
            emit clientToolsReady(serverName, static_cast<int>(client->cachedTools().size()));
            updateServerState(serverName, McpServerState::Ready);
            if (isAllToolsReady()) {
                emit allToolsReady();
            }
        }
    });
    connect(client.get(), &McpQtClient::disconnected, this, [this, serverName]() {
        updateServerState(serverName, McpServerState::Pending);
        emit clientDisconnected(serverName);
    });
    connect(client.get(), &McpQtClient::errorOccurred, this, [this, serverName](const mcp_qt::McpError& error) {
        updateServerState(serverName, McpServerState::Error);
        emit clientErrorOccurred(serverName, error);
        if (isAllToolsReady()) {
            emit allToolsReady();
        }
    });
    connect(client.get(), &McpQtClient::toolsChanged, this, [this, serverName](const std::vector<mcp_qt::McpQtTool>& newTools) {
        emit clientToolsChanged(serverName, newTools);
    });
    connect(client.get(), &McpQtClient::promptsChanged, this, [this, serverName]() {
        emit clientPromptsChanged(serverName);
    });
}

bool McpServerManager::isAllToolsReady() const {
    if (m_serverStates.isEmpty()) return false;
    for (auto it = m_serverStates.constBegin(); it != m_serverStates.constEnd(); ++it) {
        if (it.value() == McpServerState::Pending || it.value() == McpServerState::Connecting) {
            return false;
        }
    }
    return true;
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
