#pragma once

#include <mcp_core/IMcpTransport.h>
#include <QObject>
#include <QProcess>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mcp_qt {

class QtProcessStdioTransport : public QObject, public mcp::IMcpTransport {
    Q_OBJECT
public:
    QtProcessStdioTransport(const std::string& command, const std::vector<std::string>& args, QObject* parent = nullptr);
    ~QtProcessStdioTransport() override;

    bool start() override;
    void close() override;
    bool send(const std::string& message) override;
    void setOnMessage(std::function<void(const std::string&)> callback) override;
    void setOnClose(std::function<void()> callback) override;
    void setOnError(std::function<void(const std::string&)> callback) override;
    void setProtocolVersion(const std::string& version) override;

private slots:
    void handleReadyReadStandardOutput();
    void handleReadyReadStandardError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);

private:
    std::string m_command;
    std::vector<std::string> m_args;
    QProcess* m_process;
    
    std::function<void(const std::string&)> m_onMessage;
    std::function<void()> m_onClose;
    std::function<void(const std::string&)> m_onError;
    
    std::string m_buffer;
    std::mutex m_mutex;
    bool m_started{false};

#ifdef _WIN32
    void* m_jobObject = nullptr;
#endif
};

} // namespace mcp_qt
