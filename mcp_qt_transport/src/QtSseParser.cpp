#include "QtSseParser.h"

#include <sstream>

namespace mcp_qt {

void QtSseParser::setEventCallback(EventCallback callback) { m_eventCallback = std::move(callback); }
void QtSseParser::setRetryCallback(RetryCallback callback) { m_retryCallback = std::move(callback); }
void QtSseParser::reset() { m_buffer.clear(); }

void QtSseParser::pushChunk(const std::string& chunk) {
    m_buffer += chunk;
    while (true) {
        std::size_t posCrlf = m_buffer.find("\r\n\r\n");
        std::size_t posLf = m_buffer.find("\n\n");

        if (posCrlf == std::string::npos && posLf == std::string::npos) {
            break;
        }

        if (posCrlf != std::string::npos && (posLf == std::string::npos || posCrlf < posLf)) {
            const std::string block = m_buffer.substr(0, posCrlf);
            m_buffer.erase(0, posCrlf + 4);
            flushEventBlock(block);
        } else {
            const std::string block = m_buffer.substr(0, posLf);
            m_buffer.erase(0, posLf + 2);
            flushEventBlock(block);
        }
    }
}

void QtSseParser::flushEventBlock(const std::string& block) {
    QtSseEvent event;
    std::istringstream stream(block);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("event:", 0) == 0) {
            event.eventName = line.substr(6);
            if (!event.eventName.empty() && event.eventName.front() == ' ') {
                event.eventName.erase(0, 1);
            }
        } else if (line.rfind("data:", 0) == 0) {
            std::string part = line.substr(5);
            if (!part.empty() && part.front() == ' ') {
                part.erase(0, 1);
            }
            if (!event.data.empty()) {
                event.data += "\n";
            }
            event.data += part;
        } else if (line.rfind("id:", 0) == 0) {
            event.lastEventId = line.substr(3);
            if (!event.lastEventId.empty() && event.lastEventId.front() == ' ') {
                event.lastEventId.erase(0, 1);
            }
        } else if (line.rfind("retry:", 0) == 0 && m_retryCallback) {
            std::string value = line.substr(6);
            if (!value.empty() && value.front() == ' ') {
                value.erase(0, 1);
            }
            try {
                m_retryCallback(std::stoi(value));
            } catch (...) {
            }
        }
    }

    if (!event.data.empty() && m_eventCallback) {
        m_eventCallback(event);
    }
}

} // namespace mcp_qt
