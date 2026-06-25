#pragma once
#include <QObject>
#include <QList>
#include <memory>
#include "mcp_core/McpClientSession.h"
#include "mcp_core/McpTool.h"

namespace mcp {

/**
 * @brief QObject wrapper around McpClientSession.
 * 
 * Bridges standard C++ callbacks to Qt's Signals & Slots mechanism, facilitating
 * integration with QML and Qt Widget UI layers.
 */
class QtMcpClient : public QObject {
    Q_OBJECT
public:
    explicit QtMcpClient(std::shared_ptr<IMcpTransport> transport, QObject* parent = nullptr);
    ~QtMcpClient() override;

    std::shared_ptr<McpClientSession> session() const { return m_session; }

signals:
    void connectionOpened();
    void initialized(bool success, const QString& info);
    void toolsListed(const QList<mcp::McpTool>& tools, const QString& error);
    void toolCalled(const QString& toolName, const QString& resultJson, const QString& error);
    void errorOccurred(const QString& errorMessage);
    void disconnected();

public slots:
    void start();
    void close();
    void initializeClient(const QString& clientName, const QString& clientVersion);
    void listTools();
    void callTool(const QString& name, const QString& argumentsJson);

private:
    std::shared_ptr<McpClientSession> m_session;
    std::shared_ptr<IMcpTransport> m_transport;
};

} // namespace mcp

Q_DECLARE_METATYPE(mcp::McpTool)
Q_DECLARE_METATYPE(QList<mcp::McpTool>)
