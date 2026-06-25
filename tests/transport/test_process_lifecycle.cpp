#include "tests/common.h"

void test_process_lifecycle() {
    std::cout << "[Process Lifecycle Test] Running session connection crash & interruption tests...\n";

    // ----------------------------------------------------
    // Scenario 6: Server crash / Connection Interrupted (连接中断清理)
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

        bool connectionErrorTriggered = false;
        session->listTools([&](const std::vector<mcp::McpTool>&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32603) {
                if (error.contains("message") && error["message"].get<std::string>().find("Connection interrupted") != std::string::npos) {
                    connectionErrorTriggered = true;
                }
            }
        });

        // 模拟 Transport 断开 / Server 崩溃
        transport->close();

        assert(connectionErrorTriggered && "Scenario 6 Failed: Connection interruption should trigger callback cleanup.");
        std::cout << "  [✓] Scenario 6: Cleanup pending requests on server crash or connection interruption\n";
    }
}
