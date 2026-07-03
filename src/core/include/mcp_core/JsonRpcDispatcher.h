#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "McpMessage.h"

namespace mcp {

using json = nlohmann::json;

/**
 * @brief Dispatcher for JSON-RPC 2.0 messages.
 * 
 * Registers handlers for specific methods and dispatches incoming messages to them.
 */
class JsonRpcDispatcher {
public:
    using RequestHandler = std::function<json(const json& params, const RequestId& id)>;
    using NotificationHandler = std::function<void(const json& params)>;

    JsonRpcDispatcher() = default;

    /**
     * @brief Register a handler for a JSON-RPC request method.
     */
    void registerRequestHandler(const std::string& method, RequestHandler handler);

    /**
     * @brief Register a handler for a JSON-RPC notification method.
     */
    void registerNotificationHandler(const std::string& method, NotificationHandler handler);

    /**
     * @brief Parse and process a raw message.
     * @return Serialized JSON-RPC response if the message was a request, empty string otherwise.
     */
    std::string dispatch(const std::string& rawMessage);

    /**
     * @brief Utility helper to create a JSON-RPC error response.
     */
    static std::string createErrorResponse(const RequestId& id, int code, const std::string& message, const json& data = nullptr);

    /**
     * @brief Utility helper to create a JSON-RPC success response.
     */
    static std::string createSuccessResponse(const RequestId& id, const json& result);

private:
    std::unordered_map<std::string, RequestHandler> m_requestHandlers;
    std::unordered_map<std::string, NotificationHandler> m_notificationHandlers;
};

} // namespace mcp
