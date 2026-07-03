#include <mcp_qt_client/McpServerManager.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

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
    for (auto it = serversObj.begin(); it != serversObj.end(); ++it) {
        QString serverName = it.key();
        if (!it.value().isObject()) continue;
        QJsonObject serverCfg = it.value().toObject();

        if (serverCfg.value(QStringLiteral("disabled")).toBool(false) == true) {
            continue;
        }

        QMap<QString, QString> processEnv;
        if (serverCfg.contains(QStringLiteral("env")) && serverCfg.value(QStringLiteral("env")).isObject()) {
            QJsonObject envs = serverCfg.value(QStringLiteral("env")).toObject();
            for (auto envIt = envs.begin(); envIt != envs.end(); ++envIt) {
                QString envVal;
                if (envIt.value().isString()) {
                    envVal = envIt.value().toString();
                } else if (envIt.value().isDouble()) {
                    envVal = QString::number(envIt.value().toDouble());
                } else if (envIt.value().isBool()) {
                    envVal = envIt.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
                }
                processEnv.insert(envIt.key(), envVal);
            }
        }

        McpQtClientBuilder builder;
        if (!processEnv.isEmpty()) {
            builder.setEnvironment(processEnv);
        }
        bool hasTransport = false;

        // 2. 根据是 url 还是 command 建立 transport
        if (serverCfg.contains(QStringLiteral("url"))) {
            QString url = serverCfg.value(QStringLiteral("url")).toString();
            builder.setTransportHttp(url);
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
    connect(client.get(), &McpQtClient::connected, this, [this, serverName]() {
        emit clientConnected(serverName);
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

} // namespace mcp_qt
