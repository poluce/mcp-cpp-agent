#include "tests/common.h"

void test_json_rpc() {
    std::cout << "[JSON-RPC Test] Running JSON-RPC parsing & ID safety scenario tests...\n";

    // ----------------------------------------------------
    // Scenario 1: JSON Parsing error (Robustness check)
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        try {
            transport->pushServerMessage("{invalid json: error");
            transport->pushServerMessage("");
            transport->pushServerMessage("   ");
            transport->pushServerMessage("[]");
        } catch (...) {
            assert(false && "Scenario 1 Failed: Incoming malformed JSON or empty message crashed the client.");
        }
        std::cout << "  [✓] Scenario 1: Malformed JSON packet parsing defensiveness\n";
    }

    // ----------------------------------------------------
    // Scenario 3: Unknown / Mismatched Response ID (Fault isolation)
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

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
            assert(false && "Scenario 3 Failed: Client crashed on unknown or malformed response id.");
        }
        std::cout << "  [✓] Scenario 3: Ignore unknown or type-mismatched response IDs (Crash prevention)\n";
    }
}
