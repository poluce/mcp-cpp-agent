#pragma once

#include <functional>
#include <string>

namespace mcp_qt {

struct QtSseEvent {
    std::string eventName{"message"};
    std::string data;
    std::string lastEventId;
};

class QtSseParser {
public:
    using EventCallback = std::function<void(const QtSseEvent&)>;
    using RetryCallback = std::function<void(int)>;

    void setEventCallback(EventCallback callback);
    void setRetryCallback(RetryCallback callback);
    void pushChunk(const std::string& chunk);
    void reset();

private:
    void flushEventBlock(const std::string& block);

    std::string m_buffer;
    EventCallback m_eventCallback;
    RetryCallback m_retryCallback;
};

} // namespace mcp_qt
