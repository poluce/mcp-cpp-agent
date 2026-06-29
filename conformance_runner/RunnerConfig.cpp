#include "RunnerConfig.h"

namespace mcp_conformance {

bool parseRunnerConfig(
    int argc,
    const char* const* argv,
    const std::string& scenarioEnv,
    const std::string& contextEnv,
    RunnerConfig* outConfig
) {
    if (!outConfig) return false;

    RunnerConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.rfind("http://", 0) == 0 || arg.rfind("https://", 0) == 0) {
            cfg.serverUrl = arg;
            cfg.httpMode = true;
        }
    }

    cfg.scenario = scenarioEnv;
    if (!contextEnv.empty()) {
        cfg.context = nlohmann::json::parse(contextEnv, nullptr, false);
        if (cfg.context.is_discarded()) {
            cfg.context = nlohmann::json::object();
        }
    }

    if (cfg.scenario.empty()) {
        return false;
    }

    *outConfig = std::move(cfg);
    return true;
}

std::string usageText() {
    return "Usage: mcp_client_conformance <server-url>\n"
           "The MCP_CONFORMANCE_SCENARIO env var must be set by the official conformance runner.";
}

} // namespace mcp_conformance
