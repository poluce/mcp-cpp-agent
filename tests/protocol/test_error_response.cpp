#include "tests/common.h"

void test_error_response() {
    // Scenario 1: Server-side unknown method request (Auto reply Method not found)
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool methodNotFoundSent = false;
        transport->onSendCallback = [&](const std::string& msg) {
            try {
                mcp::json j = mcp::json::parse(msg);
                if (j.contains("error") && j["error"]["code"] == -32601) {
                    methodNotFoundSent = true;
                }
            } catch (...) {}
        };

        mcp::json unknownReq = {
            {"jsonrpc", "2.0"},
            {"id", 888},
            {"method", "custom/unsupportedMethod"},
            {"params", mcp::json::object()}
        };
        transport->pushServerMessage(unknownReq.dump());

        TM_ASSERT_TRUE(methodNotFoundSent, "Scenario 1: Client did not reply with Method not found (-32601) error.");
    }

    // Scenario 2: Schema validation failed / Invalid params (类型错误与参数校验失败)
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

        bool schemaErrorTriggered = false;
        session->callTool("calculate_add", {{"a", "invalid_number_type"}, {"b", 5}}, [&](const mcp::json&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32602) {
                schemaErrorTriggered = true;
            }
        });

        mcp::json schemaErrResp = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"error", {{"code", -32602}, {"message", "Invalid params: a and b must be numbers"}}}
        };
        transport->pushServerMessage(schemaErrResp.dump());

        TM_ASSERT_TRUE(schemaErrorTriggered, "Scenario 2: Invalid parameter type should return -32602.");
    }
}
