#include "mcp_core/McpClientSession.h"

namespace mcp {

McpClientSession::McpClientSession(std::shared_ptr<IMcpTransport> transport)
    : m_transport(std::move(transport)) {}

McpClientSession::~McpClientSession() {
    close();
}

void McpClientSession::init() {
    auto self = shared_from_this();
    m_transport->setOnMessage([self](const std::string& msg) {
        self->handleIncomingMessage(msg);
    });
}

bool McpClientSession::start() {
    return m_transport->start();
}

void McpClientSession::close() {
    if (m_transport) {
        m_transport->close();
    }
}

int64_t McpClientSession::sendRequest(const std::string& method, const json& params, ResponseCallback callback) {
    int64_t id;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        id = m_nextId++;
        m_pendingRequests[id] = callback;
    }

    json requestMsg = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", params}
    };

    m_transport->send(requestMsg.dump());
    return id;
}

void McpClientSession::sendNotification(const std::string& method, const json& params) {
    json notificationMsg = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };
    m_transport->send(notificationMsg.dump());
}

void McpClientSession::registerNotificationHandler(const std::string& method, NotificationCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_notificationHandlers[method] = callback;
}

void McpClientSession::handleIncomingMessage(const std::string& rawMessage) {
    json j;
    try {
        j = json::parse(rawMessage);
    } catch (...) {
        return; 
    }

    if (!j.is_object()) return;

    if (j.contains("id")) {
        if (j.contains("result") || j.contains("error")) {
            handleResponse(j);
        } else if (j.contains("method")) {
            handleRequestFromServer(j);
        }
    } else if (j.contains("method")) {
        handleNotification(j);
    }
}

void McpClientSession::handleResponse(const json& responseJson) {
    int64_t id = 0;
    if (responseJson["id"].is_number_integer()) {
        id = responseJson["id"].get<int64_t>();
    } else {
        return; 
    }

    ResponseCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_pendingRequests.find(id);
        if (it != m_pendingRequests.end()) {
            cb = std::move(it->second);
            m_pendingRequests.erase(it);
        }
    }

    if (cb) {
        json result = responseJson.contains("result") ? responseJson["result"] : json::object();
        json error = responseJson.contains("error") ? responseJson["error"] : json::object();
        cb(result, error);
    }
}

void McpClientSession::handleNotification(const json& notificationJson) {
    std::string method = notificationJson["method"].get<std::string>();
    json params = notificationJson.contains("params") ? notificationJson["params"] : json::object();

    NotificationCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_notificationHandlers.find(method);
        if (it != m_notificationHandlers.end()) {
            cb = it->second;
        }
    }

    if (cb) {
        cb(params);
    }
}

void McpClientSession::handleRequestFromServer(const json& requestJson) {
    int64_t id = requestJson["id"].get<int64_t>();
    std::string method = requestJson["method"].get<std::string>();
    
    json errorResponse = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", -32601},
            {"message", "Method not found: " + method}
        }}
    };
    m_transport->send(errorResponse.dump());
}

void McpClientSession::initialize(const std::string& clientName, const std::string& clientVersion,
                                  std::function<void(bool success, const json& serverInfo)> callback) {
    json params = {
        {"protocolVersion", MCP_PROTOCOL_VERSION},
        {"capabilities", {
            {"roots", {{"listChanged", false}}},
            {"sampling", json::object()}
        }},
        {"clientInfo", {
            {"name", clientName},
            {"version", clientVersion}
        }}
    };

    auto self = shared_from_this();
    sendRequest("initialize", params, [self, callback](const json& result, const json& error) {
        if (!error.empty()) {
            callback(false, error);
        } else {
            self->sendNotification("notifications/initialized", json::object());
            callback(true, result);
        }
    });
}

void McpClientSession::shutdown(std::function<void(bool success)> callback) {
    sendRequest("shutdown", json::object(), [callback](const json& result, const json& error) {
        if (!error.empty()) {
            callback(false);
        } else {
            callback(true);
        }
    });
}

void McpClientSession::listTools(std::function<void(const std::vector<McpTool>& tools, const json& error)> callback) {
    sendRequest("tools/list", json::object(), [callback](const json& result, const json& error) {
        if (!error.empty()) {
            callback({}, error);
        } else {
            std::vector<McpTool> toolsList;
            if (result.contains("tools") && result["tools"].is_array()) {
                for (const auto& item : result["tools"]) {
                    toolsList.push_back(item.get<McpTool>());
                }
            }
            callback(toolsList, json::object());
        }
    });
}

void McpClientSession::callTool(const std::string& name, const json& arguments,
                              std::function<void(const json& content, const json& error)> callback) {
    json params = {
        {"name", name},
        {"arguments", arguments}
    };

    sendRequest("tools/call", params, [callback](const json& result, const json& error) {
        if (!error.empty()) {
            callback(json::object(), error);
        } else {
            callback(result, json::object());
        }
    });
}

void McpClientSession::listResources(std::function<void(const json& result, const json& error)> callback) {
    sendRequest("resources/list", json::object(), [callback](const json& result, const json& error) {
        callback(result, error);
    });
}

void McpClientSession::readResource(const std::string& uri, std::function<void(const json& result, const json& error)> callback) {
    json params = {
        {"uri", uri}
    };
    sendRequest("resources/read", params, [callback](const json& result, const json& error) {
        callback(result, error);
    });
}

void McpClientSession::listPrompts(std::function<void(const json& result, const json& error)> callback) {
    sendRequest("prompts/list", json::object(), [callback](const json& result, const json& error) {
        callback(result, error);
    });
}

void McpClientSession::getPrompt(const std::string& name, const json& arguments, std::function<void(const json& result, const json& error)> callback) {
    json params = {
        {"name", name},
        {"arguments", arguments}
    };
    sendRequest("prompts/get", params, [callback](const json& result, const json& error) {
        callback(result, error);
    });
}

} // namespace mcp
