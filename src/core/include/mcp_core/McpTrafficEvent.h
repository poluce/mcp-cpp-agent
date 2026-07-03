#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace mcp {

enum class McpTrafficDirection {
    Outbound,
    Inbound
};

enum class McpTrafficKind {
    Request,
    Response,
    Notification,
    Unknown
};

struct McpTrafficEvent {
    McpTrafficDirection direction{McpTrafficDirection::Outbound};
    McpTrafficKind kind{McpTrafficKind::Unknown};
    nlohmann::json payload;
    std::string raw;
};

} // namespace mcp
