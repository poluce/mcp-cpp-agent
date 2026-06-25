#include "AgentController.h"
#include "mcp_qt/QtStdioTransport.h"
#include "mcp_qt/QtHttpTransport.h"

AgentController::AgentController(ToolManager* toolManager, QObject* parent)
    : QObject(parent)
    , m_toolManager(toolManager) {}

AgentController::~AgentController() {
    if (m_client) {
        m_client->close();
    }
}

void AgentController::connectToStdioServer(const QString& program, const QStringList& arguments) {
    emit statusChanged("正在连接 (Stdio)...");
    emit logMessage("Starting process: " + program + " " + arguments.join(" "));

    if (m_client) {
        m_client->close();
        m_client->deleteLater();
        m_client = nullptr;
    }

    auto transport = std::make_shared<mcp::QtStdioTransport>(program, arguments);
    m_client = new mcp::QtMcpClient(transport, this);

    connect(m_client, &mcp::QtMcpClient::connectionOpened, this, &AgentController::onConnectionOpened);
    connect(m_client, &mcp::QtMcpClient::initialized, this, &AgentController::onInitialized);
    connect(m_client, &mcp::QtMcpClient::toolsListed, this, &AgentController::onToolsListed);
    connect(m_client, &mcp::QtMcpClient::toolCalled, this, &AgentController::onToolCalled);
    connect(m_client, &mcp::QtMcpClient::errorOccurred, this, &AgentController::onErrorOccurred);
    connect(m_client, &mcp::QtMcpClient::disconnected, this, &AgentController::onDisconnected);

    m_client->start();
}

void AgentController::connectToHttpServer(const QString& sseUrl) {
    emit statusChanged("正在连接 (HTTP/SSE)...");
    emit logMessage("Connecting to SSE Endpoint: " + sseUrl);

    if (m_client) {
        m_client->close();
        m_client->deleteLater();
        m_client = nullptr;
    }

    auto transport = std::make_shared<mcp::QtHttpTransport>(QUrl(sseUrl));
    m_client = new mcp::QtMcpClient(transport, this);

    connect(m_client, &mcp::QtMcpClient::connectionOpened, this, &AgentController::onConnectionOpened);
    connect(m_client, &mcp::QtMcpClient::initialized, this, &AgentController::onInitialized);
    connect(m_client, &mcp::QtMcpClient::toolsListed, this, &AgentController::onToolsListed);
    connect(m_client, &mcp::QtMcpClient::toolCalled, this, &AgentController::onToolCalled);
    connect(m_client, &mcp::QtMcpClient::errorOccurred, this, &AgentController::onErrorOccurred);
    connect(m_client, &mcp::QtMcpClient::disconnected, this, &AgentController::onDisconnected);

    m_client->start();
}

void AgentController::onConnectionOpened() {
    emit statusChanged("已建连 (正在握手)");
    emit logMessage("Transport channel connected. Initializing MCP...");
    m_client->initializeClient("mcp-cpp-agent-client", "1.0.0");
}

void AgentController::onInitialized(bool success, const QString& info) {
    if (success) {
        emit statusChanged("已初始化");
        emit logMessage("MCP Handshake succeeded. Server info: " + info);
        emit logMessage("Listing tools...");
        m_client->listTools();
    } else {
        emit statusChanged("握手失败");
        emit logMessage("MCP Handshake failed: " + info);
    }
}

void AgentController::onToolsListed(const QList<mcp::McpTool>& tools, const QString& error) {
    if (!error.isEmpty()) {
        emit logMessage("Failed to list tools: " + error);
        return;
    }

    m_toolManager->updateTools(tools);
    emit logMessage(QString("Successfully discovered %1 tools:").arg(tools.size()));
    for (const auto& tool : tools) {
        emit logMessage(QString(" - [%1]: %2").arg(QString::fromStdString(tool.name), QString::fromStdString(tool.description)));
    }
}

void AgentController::onToolCalled(const QString& toolName, const QString& resultJson, const QString& error) {
    if (!error.isEmpty()) {
        emit logMessage(QString("Tool [%1] failed: %2").arg(toolName, error));
    } else {
        emit logMessage(QString("Tool [%1] execution result: %2").arg(toolName, resultJson));
    }
    emit toolExecutionResult(resultJson);
}

void AgentController::onErrorOccurred(const QString& errorMessage) {
    emit logMessage("ERROR: " + errorMessage);
}

void AgentController::onDisconnected() {
    emit statusChanged("已断开连接");
    emit logMessage("Connection closed.");
}
