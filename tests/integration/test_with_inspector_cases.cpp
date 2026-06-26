#include "tests/common.h"

void test_with_inspector_cases() {
    // Fill inspector stub with actual simulated schema tolerance tests
    auto transport = std::make_shared<MockTransport>();
    auto session = std::make_shared<mcp::McpClientSession>(transport);
    session->init();
    session->start();

    // 1. Initial Handshake
    bool initSuccess = false;
    session->initialize("inspector-test-client", "1.0.0", [&](bool success, const mcp::json&){
        initSuccess = success;
    });
    
    mcp::json initResp = {
        {"jsonrpc", "2.0"}, {"id", 1},
        {"result", {{"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION}, {"capabilities", mcp::json::object()}, {"serverInfo", mcp::json::object()}}}
    };
    transport->pushServerMessage(initResp.dump());
    TM_ASSERT_TRUE(initSuccess, "Inspector: Handshake should succeed.");

    // 2. Simulate Inspector pushing a Tools list with mismatched/invalid tool definitions (Robustness verification)
    bool listSuccess = false;
    std::vector<mcp::McpTool> discoveredTools;
    session->listTools([&](const std::vector<mcp::McpTool>& tools, const mcp::json& error) {
        if (error.empty()) {
            listSuccess = true;
            discoveredTools = tools;
        }
    });

    // One valid tool, and one tool with invalid type in schemas
    mcp::json badToolsResp = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"result", {
            {"tools", {
                {
                    {"name", "valid_tool"},
                    {"description", "Normal tool"},
                    {"inputSchema", {{"type", "object"}}}
                },
                {
                    {"name", 9999}, // Invalid type (should be string)
                    {"description", "Bad tool"},
                    {"inputSchema", {{"type", "object"}}}
                }
            }}
        }}
    };
    
    try {
        transport->pushServerMessage(badToolsResp.dump());
    } catch (...) {
        // Tolerated by catcher
    }

    TM_ASSERT_TRUE(listSuccess, "Inspector: SDK should handle parse errors gracefully during listTools response parsing.");
    TM_ASSERT_TRUE(discoveredTools.empty(), "Inspector: Bad tool list format should return empty/error rather than crashing.");
}
