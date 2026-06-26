#include "tests/common.h"

void test_with_anysearch_mcp() {
    // Fill anysearch stub with actual simulated search query E2E test cases
    auto transport = std::make_shared<MockTransport>();
    auto session = std::make_shared<mcp::McpClientSession>(transport);
    session->init();
    session->start();

    // 1. Initial Handshake
    bool initSuccess = false;
    session->initialize("anysearch-test-client", "1.0.0", [&](bool success, const mcp::json&){
        initSuccess = success;
    });
    
    mcp::json initResp = {
        {"jsonrpc", "2.0"}, {"id", 1},
        {"result", {{"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION}, {"capabilities", mcp::json::object()}, {"serverInfo", mcp::json::object()}}}
    };
    transport->pushServerMessage(initResp.dump());
    TM_ASSERT_TRUE(initSuccess, "AnySearch: Handshake should succeed.");

    // 2. Call simulated AnySearch search tool
    bool callSuccess = false;
    std::string searchResult;
    session->callTool("search", {{"query", "Qt 6.11"}}, [&](const mcp::json& result, const mcp::json& error) {
        if (error.empty() && result.contains("content")) {
            callSuccess = true;
            searchResult = result["content"][0]["text"].get<std::string>();
        }
    });

    mcp::json mockSearchResp = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"result", {
            {"content", {
                {{"type", "text"}, {"text", "Found: Qt 6.11 released with new QML modules."}}
            }}
        }}
    };
    transport->pushServerMessage(mockSearchResp.dump());

    TM_ASSERT_TRUE(callSuccess, "AnySearch: Tool execution callback should succeed.");
    TM_ASSERT_STR_CONTAINS(searchResult, "QML modules", "AnySearch: Search result should contain query relevance data.");
}
