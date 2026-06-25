#include "tests/common.h"

void test_initialize() {
    std::cout << "[Initialize Test] Running initialize scenario tests...\n";

    // ----------------------------------------------------
    // Scenario 1: tools/list before initialize (Should be intercepted)
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool interceptCorrect = false;
        session->listTools([&](const std::vector<mcp::McpTool>&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32002) {
                interceptCorrect = true;
            }
        });

        assert(interceptCorrect && "Scenario 1 Failed: Sending tools/list before initialization should be intercepted locally.");
        assert(transport->lastSentMessage.empty() && "Scenario 1 Failed: Stdio packet should not be sent out on interception.");
        std::cout << "  [✓] Scenario 1: Intercept business request before initialization\n";
    }

    // ----------------------------------------------------
    // Scenario 2: Normal initialize
    // ----------------------------------------------------
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

        bool initSuccess = false;
        session->initialize("test-client", "1.0.0", [&](bool success, const mcp::json&) {
            initSuccess = success;
        });

        mcp::json mockResponse = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"result", {
                {"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION},
                {"capabilities", mcp::json::object()},
                {"serverInfo", {{"name", "mock-server"}, {"version", "1.0.0"}}}
            }}
        };
        transport->pushServerMessage(mockResponse.dump());

        assert(initSuccess && "Scenario 2 Failed: Normal initialize handshake failed.");
        assert(initializedNotificationSent && "Scenario 2 Failed: initialized notification was not sent.");
        assert(session->state() == mcp::SessionState::Initialized && "Scenario 2 Failed: state was not updated to Initialized.");
        std::cout << "  [✓] Scenario 2: Normal initialize handshake and notification\n";
    }

    // ----------------------------------------------------
    // Scenario 3: Duplicate initialize (Should be blocked)
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        session->initialize("test-client", "1.0.0", [](bool, const mcp::json&){});

        bool repeatIntercept = false;
        session->initialize("test-client", "1.0.0", [&](bool success, const mcp::json& error) {
            if (!success && error.contains("code") && error["code"] == -32600) {
                repeatIntercept = true;
            }
        });

        assert(repeatIntercept && "Scenario 3 Failed: Duplicate initialization calls should be intercepted.");
        std::cout << "  [✓] Scenario 3: Prevent duplicate initialize calls\n";
    }
}
