#pragma once

#include "RunnerConfig.h"

#include <set>
#include <string>

namespace mcp_conformance {

int runScenario(const RunnerConfig& config);
std::set<std::string> registeredScenarioNames();

} // namespace mcp_conformance
