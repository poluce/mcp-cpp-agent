#include "tests/common.h"

void test_capabilities() {
    // Scenario 1: Server returns unsupported protocolVersion / Mismatch
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool initializedNotificationSent = false;
        transport->onSendCallback = [&](const std::string& msg) {
            mcp::json j = mcp::json::parse(msg);
            if (j.contains("method") && j["method"] == "notifications/initialized") {
                initializedNotificationSent = true;
            }
        };

        bool initSuccess = true; 
        session->initialize("test-client", "1.0.0", [&](bool success, const mcp::json&) {
            initSuccess = success;
        });

        // Simulate server returning mismatched version "2024-11-05" instead of "2025-11-25"
        mcp::json mockResponse = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"result", {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", mcp::json::object()},
                {"serverInfo", {{"name", "old-mock-server"}, {"version", "1.0.0"}}}
            }}
        };
        transport->pushServerMessage(mockResponse.dump());

        TM_ASSERT_FALSE(initSuccess, "Scenario 1: Handshake should fail on version mismatch.");
        TM_ASSERT_FALSE(initializedNotificationSent, "Scenario 1: initialized notification must not be sent on mismatch.");
        TM_ASSERT_EQ(session->state(), mcp::SessionState::Uninitialized, "Scenario 1: state should rollback to Uninitialized.");
    }
}
