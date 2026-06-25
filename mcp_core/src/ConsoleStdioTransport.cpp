#include "mcp_core/ConsoleStdioTransport.h"
#include <iostream>

namespace mcp {

ConsoleStdioTransport::ConsoleStdioTransport() {}

ConsoleStdioTransport::~ConsoleStdioTransport() {
    close();
}

bool ConsoleStdioTransport::send(const std::string& message) {
    std::cout << message << "\n" << std::flush;
    return true;
}

void ConsoleStdioTransport::setOnMessage(std::function<void(const std::string&)> callback) {
    m_onMessage = std::move(callback);
}

void ConsoleStdioTransport::setOnClose(std::function<void()> callback) {
    m_onClose = std::move(callback);
}

void ConsoleStdioTransport::setOnError(std::function<void(const std::string&)> callback) {
    m_onError = std::move(callback);
}

bool ConsoleStdioTransport::start() {
    if (m_running) return false;
    m_running = true;
    m_readThread = std::thread(&ConsoleStdioTransport::readLoop, this);
    return true;
}

void ConsoleStdioTransport::close() {
    if (m_running) {
        m_running = false;
        if (m_readThread.joinable()) {
            m_readThread.detach();
        }
        if (m_onClose) {
            m_onClose();
        }
    }
}

void ConsoleStdioTransport::readLoop() {
    std::string line;
    while (m_running && std::getline(std::cin, line)) {
        if (!line.empty() && m_onMessage) {
            m_onMessage(line);
        }
    }
    close();
}

} // namespace mcp
