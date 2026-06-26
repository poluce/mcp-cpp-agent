#include "tests/common.h"

void test_json_rpc() {
    // Scenario 1: JSON Parsing error (Robustness check)
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool crashed = false;
        try {
            transport->pushServerMessage("{invalid json: error");
            transport->pushServerMessage("");
            transport->pushServerMessage("   ");
            transport->pushServerMessage("[]");
        } catch (...) {
            crashed = true;
        }
        TM_ASSERT_FALSE(crashed, "Scenario 1: Incoming malformed JSON or empty message crashed the client.");
    }

    // Scenario 2: Unknown / Mismatched Response ID (Fault isolation)
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool crashed = false;
        try {
            mcp::json unknownResp = {
                {"jsonrpc", "2.0"},
                {"id", 99999},
                {"result", {{"status", "ignored"}}}
            };
            transport->pushServerMessage(unknownResp.dump());

            mcp::json invalidIdResp = {
                {"jsonrpc", "2.0"},
                {"id", mcp::json::array({1, 2})},
                {"result", {{"status", "ignored"}}}
            };
            transport->pushServerMessage(invalidIdResp.dump());
        } catch (...) {
            crashed = true;
        }
        TM_ASSERT_FALSE(crashed, "Scenario 2: Client crashed on unknown or malformed response id.");
    }
}
