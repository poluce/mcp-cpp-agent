#include "tests/common.h"

void test_prompts() {
    // Scenario 1: Prompts listing, get, missing argument, and type validation
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

        // 1.1: Paginated prompt listing
        bool promptListSuccess = false;
        std::string promptNextCursor;
        session->listPrompts("", [&](const mcp::json& result, const std::string& nextCursor, const mcp::json& error) {
            if (error.empty() && result.contains("prompts") && result["prompts"].size() == 1 && nextCursor == "prompt_page_2") {
                promptListSuccess = true;
                promptNextCursor = nextCursor;
            }
        });
        mcp::json pList1Resp = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"result", {
                {"prompts", mcp::json::array({{{"name", "code_review"}, {"description", "review"}}})},
                {"nextCursor", "prompt_page_2"}
            }}
        };
        transport->pushServerMessage(pList1Resp.dump());
        TM_ASSERT_TRUE(promptListSuccess, "Scenario 1.1: prompt list first page failed.");

        // 1.2: prompts/get Missing argument error
        bool argMissingSuccess = false;
        session->getPrompt("code_review", mcp::json::object(), [&](const mcp::json&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32602) {
                argMissingSuccess = true;
            }
        });
        mcp::json errMissingResp = {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"error", {{"code", -32602}, {"message", "Missing required argument: code"}}}
        };
        transport->pushServerMessage(errMissingResp.dump());
        TM_ASSERT_TRUE(argMissingSuccess, "Scenario 1.2: missing argument should return -32602.");

        // 1.3: prompts/get Argument type mismatch error
        bool typeMismatchSuccess = false;
        session->getPrompt("code_review", {{"code", 12345}}, [&](const mcp::json&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32602) {
                typeMismatchSuccess = true;
            }
        });
        mcp::json errTypeResp = {
            {"jsonrpc", "2.0"},
            {"id", 4},
            {"error", {{"code", -32602}, {"message", "Argument must be string"}}}
        };
        transport->pushServerMessage(errTypeResp.dump());
        TM_ASSERT_TRUE(typeMismatchSuccess, "Scenario 1.3: type mismatch should return -32602.");
    }

    // Scenario 2: Rich prompt contents (text, image, embedded resource)
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

        // 2.1: Multi-media prompt result (text, image, resource contents)
        bool richPromptSuccess = false;
        session->getPrompt("rich_prompt", mcp::json::object(), [&](const mcp::json& result, const mcp::json& error) {
            if (error.empty() && result.contains("messages")) {
                auto contentArr = result["messages"][0]["content"];
                if (contentArr.size() == 3) {
                    if (contentArr[0]["type"] == "text" &&
                        contentArr[1]["type"] == "image" &&
                        contentArr[2]["type"] == "resource") {
                        richPromptSuccess = true;
                    }
                }
            }
        });
        mcp::json richPromptResp = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"result", {
                {"description", "rich"},
                {"messages", mcp::json::array({{
                    {"role", "assistant"},
                    {"content", mcp::json::array({
                        {{"type", "text"}, {"text", "analysis"}},
                        {{"type", "image"}, {"data", "Base64..."}, {"mimeType", "image/jpeg"}},
                        {{"type", "resource"}, {"resource", {{"uri", "file:///logs/system.log"}, {"text", "[Embedded Logs]"}}}}
                    })}
                }})}
            }}
        };
        transport->pushServerMessage(richPromptResp.dump());
        TM_ASSERT_TRUE(richPromptSuccess, "Scenario 2.1: rich media prompt content parsing failed.");
    }
}
