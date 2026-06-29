#include "ScenarioContext.h"

namespace mcp_conformance {

std::string contextString(const RunnerConfig& config, const std::string& key, const std::string& fallback) {
    if (!config.context.is_object() || !config.context.contains(key) || !config.context[key].is_string()) {
        return fallback;
    }
    return config.context[key].get<std::string>();
}

nlohmann::json contextObject(const RunnerConfig& config, const std::string& key) {
    if (!config.context.is_object() || !config.context.contains(key)) {
        return nlohmann::json::object();
    }
    return config.context[key];
}

bool contextHasName(const RunnerConfig& config, const std::string& expectedName) {
    return config.context.is_object()
        && config.context.contains("name")
        && config.context["name"].is_string()
        && config.context["name"].get<std::string>() == expectedName;
}

} // namespace mcp_conformance
