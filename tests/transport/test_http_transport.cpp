#include "tests/common.h"

void test_http_transport() {
    // Reconstruct test_http_transport to verify HTTP/SSE Transport event contracts
    auto transport = std::make_shared<MockTransport>();
    auto session = std::make_shared<mcp::McpClientSession>(transport);
    session->init();
    session->start();

    // 1. Initialize Sync Handshake simulated
    bool initSuccess = false;
    session->initialize("test", "1.0", [&](bool success, const mcp::json&){
        initSuccess = success;
    });
    
    mcp::json initResp = {
        {"jsonrpc", "2.0"}, {"id", 1},
        {"result", {{"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION}, {"capabilities", mcp::json::object()}, {"serverInfo", mcp::json::object()}}}
    };
    transport->pushServerMessage(initResp.dump());
    
    TM_ASSERT_TRUE(initSuccess, "HTTP/SSE Simulated initialize handshake should succeed.");

    // 2. Simulate transport close (e.g. HTTP SSE disconnection)
    transport->close();
    TM_ASSERT_EQ(session->state(), mcp::SessionState::Shutdown, "HTTP/SSE Simulated Disconnection should transition state to Shutdown.");
}
