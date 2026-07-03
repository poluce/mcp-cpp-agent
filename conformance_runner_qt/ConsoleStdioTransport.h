#pragma once
#include <thread>
#include <atomic>
#include <mcp_core/IMcpTransport.h>

namespace mcp {

/**
 * @brief Transport implementation reading/writing from/to the current process's standard input and output.
 * 
 * Essential for integration with the official Model Context Protocol Conformance Tool.
 */
class ConsoleStdioTransport : public IMcpTransport {
public:
    ConsoleStdioTransport();
    ~ConsoleStdioTransport() override;

    bool send(const std::string& message) override;
    void setOnMessage(std::function<void(const std::string&)> callback) override;
    void setOnClose(std::function<void()> callback) override;
    void setOnError(std::function<void(const std::string&)> callback) override;
    bool start() override;
    void close() override;

private:
    void readLoop();

    std::function<void(const std::string&)> m_onMessage;
    std::function<void()> m_onClose;
    std::function<void(const std::string&)> m_onError;

    std::thread m_readThread;
    std::atomic<bool> m_running{false};
};

} // namespace mcp
