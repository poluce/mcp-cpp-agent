#include "mcp_qt/QtMcpClient.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace mcp {

QtMcpClient::QtMcpClient(std::shared_ptr<IMcpTransport> transport, QObject* parent)
    : QObject(parent)
    , m_transport(std::move(transport))
{
    // Register types for use in Signals/Slots
    qRegisterMetaType<mcp::McpTool>("mcp::McpTool");
    qRegisterMetaType<QList<mcp::McpTool>>("QList<mcp::McpTool>");

    m_session = std::make_shared<McpClientSession>(m_transport);
    m_session->init();

    // Bind transport-level events to emit signals
    m_transport->setOnClose([this]() {
        emit disconnected();
    });

    m_transport->setOnError([this](const std::string& err) {
        emit errorOccurred(QString::fromStdString(err));
    });
}

QtMcpClient::~QtMcpClient() {
    close();
}

void QtMcpClient::start() {
    if (m_session->start()) {
        emit connectionOpened();
    } else {
        emit errorOccurred("Failed to start transport connection");
    }
}

void QtMcpClient::close() {
    m_session->close();
}

void QtMcpClient::initializeClient(const QString& clientName, const QString& clientVersion) {
    m_session->initialize(clientName.toStdString(), clientVersion.toStdString(),
                          [this](bool success, const json& serverInfo) {
        QString infoStr = QString::fromStdString(serverInfo.dump());
        emit initialized(success, infoStr);
    });
}

void QtMcpClient::listTools() {
    m_session->listTools([this](const std::vector<McpTool>& tools, const json& error) {
        QString errStr;
        if (!error.empty()) {
            errStr = QString::fromStdString(error.dump());
        }

        QList<McpTool> qTools;
        for (const auto& t : tools) {
            qTools.append(t);
        }
        emit toolsListed(qTools, errStr);
    });
}

void QtMcpClient::callTool(const QString& name, const QString& argumentsJson) {
    json args = json::object();
    if (!argumentsJson.isEmpty()) {
        try {
            args = json::parse(argumentsJson.toStdString());
        } catch (const std::exception& e) {
            emit toolCalled(name, "", "Arguments JSON parse error: " + QString::fromStdString(e.what()));
            return;
        }
    }

    m_session->callTool(name.toStdString(), args, [this, name](const json& result, const json& error) {
        QString resStr = QString::fromStdString(result.dump());
        QString errStr;
        if (!error.empty()) {
            errStr = QString::fromStdString(error.dump());
        }
        emit toolCalled(name, resStr, errStr);
    });
}

} // namespace mcp
