#pragma once

#include "RunnerConfig.h"

namespace mcp_conformance {

std::string contextString(const RunnerConfig& config, const std::string& key, const std::string& fallback = "");
nlohmann::json contextObject(const RunnerConfig& config, const std::string& key);
bool contextHasName(const RunnerConfig& config, const std::string& expectedName);

} // namespace mcp_conformance
