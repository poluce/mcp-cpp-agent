#include "tests/common.h"

void test_notifications() {
    std::cout << "[Notifications Test] Running server-to-client notifications scenario tests...\n";

    // ----------------------------------------------------
    // Scenario 1: Listen to notifications/resources/updated notification
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();
        
        session->initialize("test", "1", [](bool, const mcp::json&){});
        mcp::json initResp = {
            {"jsonrpc", "2.0"}, {"id", 1},
            {"result", {{"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION}, {"capabilities", mcp::json::object()}, {"serverInfo", mcp::json::object()}}}
        };
        transport->pushServerMessage(initResp.dump());

        bool notificationReceived = false;
        session->registerNotificationHandler("notifications/resources/updated", [&](const mcp::json& params) {
            if (params.contains("uri") && params["uri"] == "file:///logs/system.log") {
                notificationReceived = true;
            }
        });

        // Server pushes resources update notification
        mcp::json notifyMsg = {
            {"jsonrpc", "2.0"},
            {"method", "notifications/resources/updated"},
            {"params", {{"uri", "file:///logs/system.log"}}}
        };
        transport->pushServerMessage(notifyMsg.dump());
        assert(notificationReceived && "Resources Scenario 3.2 Failed: subscription notification not received.");
        std::cout << "  [✓] Scenario 1: Resource updated notifications handled successfully\n";
    }

    // ----------------------------------------------------
    // Scenario 2: prompts/listChanged Notification
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();
        
        session->initialize("test", "1", [](bool, const mcp::json&){});
        mcp::json initResp = {
            {"jsonrpc", "2.0"}, {"id", 1},
            {"result", {{"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION}, {"capabilities", mcp::json::object()}, {"serverInfo", mcp::json::object()}}}
        };
        transport->pushServerMessage(initResp.dump());

        // prompts/listChanged Notification
        bool promptsChangedReceived = false;
        session->registerNotificationHandler("notifications/prompts/list-changed", [&](const mcp::json&) {
            promptsChangedReceived = true;
        });

        mcp::json notifyMsg = {
            {"jsonrpc", "2.0"},
            {"method", "notifications/prompts/list-changed"},
            {"params", mcp::json::object()}
        };
        transport->pushServerMessage(notifyMsg.dump());
        assert(promptsChangedReceived && "Prompts Scenario 5.2 Failed: prompts list-changed notification failed.");
        std::cout << "  [✓] Scenario 2: Prompt list-changed notifications handled successfully\n";
    }
}
