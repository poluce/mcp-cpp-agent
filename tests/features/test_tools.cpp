#include "tests/common.h"

void test_tools() {
    // Scenario 1: Tool Name Format Validation
    {
        TM_ASSERT_TRUE(mcp::isValidToolName("calculate_add"), "Tool Name Validation: calculate_add");
        TM_ASSERT_TRUE(mcp::isValidToolName("get-system-info"), "Tool Name Validation: get-system-info");
        TM_ASSERT_TRUE(mcp::isValidToolName("test.tool"), "Tool Name Validation: test.tool");
        TM_ASSERT_TRUE(mcp::isValidToolName("12345"), "Tool Name Validation: 12345");
        
        TM_ASSERT_FALSE(mcp::isValidToolName(""), "Tool Name Validation: Empty name");
        TM_ASSERT_FALSE(mcp::isValidToolName("tools list"), "Tool Name Validation: Spaces");
        TM_ASSERT_FALSE(mcp::isValidToolName("calculate*add"), "Tool Name Validation: Asterisk");
        TM_ASSERT_FALSE(mcp::isValidToolName("tool/call"), "Tool Name Validation: Slash");
        TM_ASSERT_FALSE(mcp::isValidToolName(std::string(129, 'a')), "Tool Name Validation: Too long");
    }

    // Scenario 2: Paginated listTools Discovery
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

        // First page listTools (no cursor)
        bool page1Success = false;
        std::string page1NextCursor;
        session->listTools("", [&](const std::vector<mcp::McpTool>& tools, const std::string& nextCursor, const mcp::json& error) {
            if (error.empty() && tools.size() == 2 && nextCursor == "page_2") {
                if (tools[0].name == "calculate_add" && tools[1].name == "get_system_time") {
                    page1Success = true;
                    page1NextCursor = nextCursor;
                }
            }
        });

        mcp::json firstPageResp = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"result", {
                {"tools", mcp::json::array({
                    {{"name", "calculate_add"}, {"description", "add"}, {"inputSchema", mcp::json::object()}},
                    {{"name", "get_system_time"}, {"description", "time"}, {"inputSchema", {{"type", "object"}, {"properties", mcp::json::object()}}}}
                })},
                {"nextCursor", "page_2"}
            }}
        };
        transport->pushServerMessage(firstPageResp.dump());
        TM_ASSERT_TRUE(page1Success, "Scenario 2: First page listTools with cursor failed.");

        // Second page listTools (with page1NextCursor)
        bool page2Success = false;
        session->listTools(page1NextCursor, [&](const std::vector<mcp::McpTool>& tools, const std::string& nextCursor, const mcp::json& error) {
            if (error.empty() && tools.size() == 2 && nextCursor.empty()) {
                if (tools[0].name == "get_system_info" && tools[1].name == "trigger_exception") {
                    page2Success = true;
                }
            }
        });

        mcp::json secondPageResp = {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"result", {
                {"tools", mcp::json::array({
                    {{"name", "get_system_info"}, {"description", "info"}, {"inputSchema", mcp::json::object()}},
                    {{"name", "trigger_exception"}, {"description", "exc"}, {"inputSchema", mcp::json::object()}}
                })}
            }}
        };
        transport->pushServerMessage(secondPageResp.dump());
        TM_ASSERT_TRUE(page2Success, "Scenario 2: Second page listTools with cursor failed.");
    }

    // Scenario 3: tools/call missing params, tool not found, and isError exception handling
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

        // 3.1: Parameter missing standard error
        bool paramErrorSuccess = false;
        session->callTool("calculate_add", mcp::json::object(), [&](const mcp::json&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32602) {
                paramErrorSuccess = true;
            }
        });
        mcp::json errParamResp = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"error", {{"code", -32602}, {"message", "Missing required arguments: a or b"}}}
        };
        transport->pushServerMessage(errParamResp.dump());
        TM_ASSERT_TRUE(paramErrorSuccess, "Scenario 3.1: parameter missing should return standard error.");

        // 3.2: Tool not found standard error
        bool toolNotFoundSuccess = false;
        session->callTool("unknown_tool", mcp::json::object(), [&](const mcp::json&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32601) {
                toolNotFoundSuccess = true;
            }
        });
        mcp::json errNotFoundResp = {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"error", {{"code", -32601}, {"message", "Tool not found"}}}
        };
        transport->pushServerMessage(errNotFoundResp.dump());
        TM_ASSERT_TRUE(toolNotFoundSuccess, "Scenario 3.2: calling non-existent tool should return -32601.");

        // 3.3: Execution isError returns true (Application level error)
        bool execErrorSuccess = false;
        session->callTool("trigger_exception", mcp::json::object(), [&](const mcp::json& result, const mcp::json& error) {
            if (error.empty() && result.contains("isError") && result["isError"] == true) {
                execErrorSuccess = true;
            }
        });
        mcp::json isErrorResp = {
            {"jsonrpc", "2.0"},
            {"id", 4},
            {"result", {
                {"content", mcp::json::array({{{"type", "text"}, {"text", "db failed"}}})},
                {"isError", true}
            }}
        };
        transport->pushServerMessage(isErrorResp.dump());
        TM_ASSERT_TRUE(execErrorSuccess, "Scenario 3.3: execution failure isError mapping failed.");
    }

    // Scenario 4: Synchronous API blocking calls (Non-sleep reactive synchronization)
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        // Dynamically respond based on incoming requests to avoid race conditions and sleeps
        transport->onSendCallback = [&](const std::string& msg) {
            mcp::json j = mcp::json::parse(msg);
            if (!j.contains("id") || j["id"].is_null()) {
                return;
            }
            int64_t id = j["id"].get<int64_t>();
            std::string method = j["method"].get<std::string>();

            if (method == "initialize") {
                mcp::json resp = {
                    {"jsonrpc", "2.0"}, {"id", id},
                    {"result", {{"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION}, {"capabilities", mcp::json::object()}, {"serverInfo", mcp::json::object()}}}
                };
                transport->pushServerMessageAsync(resp.dump(), 2);
            } else if (method == "tools/list") {
                mcp::json resp = {
                    {"jsonrpc", "2.0"}, {"id", id},
                    {"result", {
                        {"tools", mcp::json::array({
                            {{"name", "sync_tool"}, {"description", "sync"}, {"inputSchema", mcp::json::object()}}
                        })}
                    }}
                };
                transport->pushServerMessageAsync(resp.dump(), 2);
            }
        };

        bool initSuccess = session->initializeSync("sync-client", "1.0.0");
        TM_ASSERT_TRUE(initSuccess, "Scenario 4: Sync initialize failed.");

        auto tools = session->listToolsSync();
        TM_ASSERT_EQ(tools.size(), 1, "Scenario 4: Sync listTools failed on tool count.");
        if (tools.size() == 1) {
            TM_ASSERT_EQ(tools[0].name, "sync_tool", "Scenario 4: Sync listTools failed on tool name.");
        }
    }

    // Scenario 5: Raw String API & Logging validation (Non-sleep reactive synchronization)
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool logReceived = false;
        session->setLogCallback([&](mcp::LogLevel level, const std::string& msg) {
            if (level == mcp::LogLevel::Debug && msg.find("sendRequest") != std::string::npos) {
                logReceived = true;
            }
        });

        transport->onSendCallback = [&](const std::string& msg) {
            mcp::json j = mcp::json::parse(msg);
            if (!j.contains("id") || j["id"].is_null()) {
                return;
            }
            int64_t id = j["id"].get<int64_t>();
            std::string method = j["method"].get<std::string>();

            if (method == "initialize") {
                mcp::json resp = {
                    {"jsonrpc", "2.0"}, {"id", id},
                    {"result", {{"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION}, {"capabilities", mcp::json::object()}, {"serverInfo", mcp::json::object()}}}
                };
                transport->pushServerMessageAsync(resp.dump(), 2);
            } else if (method == "tools/call") {
                mcp::json resp = {
                    {"jsonrpc", "2.0"}, {"id", id},
                    {"result", {
                        {"content", mcp::json::array({{{"type", "text"}, {"text", "raw success"}}})}
                    }}
                };
                transport->pushServerMessageAsync(resp.dump(), 2);
            }
        };

        bool initSuccess = session->initializeSync("raw-client", "1.0.0");
        TM_ASSERT_TRUE(initSuccess, "Scenario 5: Initialize Sync should succeed.");

        std::string errOut;
        std::string result = session->callToolSyncRaw("calculate_add", "{\"a\":10,\"b\":20}", &errOut);
        
        TM_ASSERT_TRUE(errOut.empty(), "Scenario 5: Raw tool call should return empty error string.");
        TM_ASSERT_STR_CONTAINS(result, "raw success", "Scenario 5: Raw tool call should return correct response.");
        TM_ASSERT_TRUE(logReceived, "Scenario 5: LogCallback should be triggered on sendRequest.");
    }
}
