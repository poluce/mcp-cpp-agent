#include "RunnerConfig.h"

#include <mcp_core/HttpSseTransport.h>
#include <mcp_core/McpClientSession.h>

namespace mcp_conformance {

static std::shared_ptr<mcp::McpClientSession> makeAuthSession(const RunnerConfig& config) {
    auto transport = std::make_shared<mcp::HttpSseTransport>(config.serverUrl);
    auto session = std::make_shared<mcp::McpClientSession>(transport);
    session->init();
    if (!session->start()) return nullptr;
    return session;
}

int runAuthFlow(const RunnerConfig& config) {
    auto session = makeAuthSession(config);
    if (!session) return 1;

    nlohmann::json serverInfo;
    if (!session->initializeSync("mcp-conformance-client-cpp", "1.0.0", &serverInfo)) return 1;

    nlohmann::json err;
    session->listToolsSync(std::chrono::milliseconds(10000), &err);
    if (!err.empty()) return 1;

    // 带 tool call 的 auth 场景：listTools + callTool("test-tool")
    nlohmann::json callErr;
    session->callToolSync("test-tool", nlohmann::json::object(), &callErr, std::chrono::milliseconds(10000));
    return callErr.empty() ? 0 : 1;
}

// TS 参考: client-credentials-* 只做 connect + listTools，不调 test-tool
int runClientCredentialsFlow(const RunnerConfig& config) {
    auto session = makeAuthSession(config);
    if (!session) return 1;

    nlohmann::json serverInfo;
    if (!session->initializeSync("mcp-conformance-client-cpp", "1.0.0", &serverInfo)) return 1;

    nlohmann::json err;
    session->listToolsSync(std::chrono::milliseconds(10000), &err);
    return err.empty() ? 0 : 1;
}

} // namespace mcp_conformance
