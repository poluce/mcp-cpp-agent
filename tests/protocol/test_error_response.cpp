#include "tests/common.h"

void test_error_response() {
    std::cout << "[Error Response Test] Running JSON-RPC standard error response tests...\n";

    // ----------------------------------------------------
    // Scenario 2: Server-side unknown method request (Auto reply Method not found)
    // ----------------------------------------------------
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

        assert(methodNotFoundSent && "Scenario 2 Failed: Client did not reply with Method not found (-32601) error.");
        std::cout << "  [✓] Scenario 2: Handle server-side unknown method requests\n";
    }

    // ----------------------------------------------------
    // Scenario 7: Schema validation failed / Invalid params (类型错误与参数校验失败)
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

        assert(schemaErrorTriggered && "Scenario 7 Failed: Invalid parameter type should return -32602.");
        std::cout << "  [✓] Scenario 7: Validate schema validation failure and invalid params\n";
    }
}
