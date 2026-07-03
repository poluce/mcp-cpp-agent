#pragma once
#include <string>
#include <variant>
#include <nlohmann/json.hpp>

namespace mcp {

using json = nlohmann::json;

/**
 * @brief Represents a JSON-RPC 2.0 message ID, which can be an integer, a string, or null/none.
 */
using RequestId = std::variant<std::monostate, int64_t, std::string>;

struct McpMessage {
    std::string jsonrpc = "2.0";
};

struct McpRequest : public McpMessage {
    RequestId id;
    std::string method;
    json params;
};

struct McpResponse : public McpMessage {
    RequestId id;
    json result;
    json error; // Contains "code", "message", and optional "data" if present
};

struct McpNotification : public McpMessage {
    std::string method;
    json params;
};

// JSON-RPC 2.0 ID serialization helpers
inline void to_json(json& j, const RequestId& id) {
    std::visit([&j](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            j = nullptr;
        } else {
            j = arg;
        }
    }, id);
}

inline void from_json(const json& j, RequestId& id) {
    if (j.is_null()) {
        id = std::monostate{};
    } else if (j.is_number_integer()) {
        id = j.get<int64_t>();
    } else if (j.is_string()) {
        id = j.get<std::string>();
    } else {
        id = std::monostate{};
    }
}

} // namespace mcp
