#include "tests/common.h"

void test_tools() {
    std::cout << "[Tools Test] Running tools/list & tools/call scenario tests...\n";

    // ----------------------------------------------------
    // Scenario 1: Tool Name Format Validation
    // ----------------------------------------------------
    {
        assert(mcp::isValidToolName("calculate_add"));
        assert(mcp::isValidToolName("get-system-info"));
        assert(mcp::isValidToolName("test.tool"));
        assert(mcp::isValidToolName("12345"));
        
        assert(!mcp::isValidToolName("")); // Empty
        assert(!mcp::isValidToolName("tools list")); // Space
        assert(!mcp::isValidToolName("calculate*add")); // Asterisk
        assert(!mcp::isValidToolName("tool/call")); // Slash
        assert(!mcp::isValidToolName(std::string(129, 'a'))); // Length > 128
        std::cout << "  [✓] Scenario 1: Tool Name constraints and regex matching\n";
    }

    // ----------------------------------------------------
    // Scenario 2: Paginated listTools Discovery
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
        assert(page1Success && "Scenario 2 Failed: First page listTools with cursor failed.");

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
        assert(page2Success && "Scenario 2 Failed: Second page listTools with cursor failed.");
        std::cout << "  [✓] Scenario 2: Paginated tool listing and cursor passing\n";
    }

    // ----------------------------------------------------
    // Scenario 3: tools/call missing params, tool not found, and isError exception handling
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
        assert(paramErrorSuccess && "Scenario 3.1 Failed: parameter missing should return standard error.");

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
        assert(toolNotFoundSuccess && "Scenario 3.2 Failed: calling non-existent tool should return -32601.");

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
        assert(execErrorSuccess && "Scenario 3.3 Failed: execution failure isError mapping failed.");
        std::cout << "  [✓] Scenario 3: Standard tools/call exceptions and application-level isError mapping\n";
    }

    // ----------------------------------------------------
    // Scenario 4: Synchronous API blocking calls (同步阻塞式 API 校验)
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        std::thread serverThread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            mcp::json initResp = {
                {"jsonrpc", "2.0"}, {"id", 1},
                {"result", {{"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION}, {"capabilities", mcp::json::object()}, {"serverInfo", mcp::json::object()}}}
            };
            transport->pushServerMessage(initResp.dump());

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            mcp::json toolsResp = {
                {"jsonrpc", "2.0"}, {"id", 2},
                {"result", {
                    {"tools", mcp::json::array({
                        {{"name", "sync_tool"}, {"description", "sync"}, {"inputSchema", mcp::json::object()}}
                    })}
                }}
            };
            transport->pushServerMessage(toolsResp.dump());
        });

        bool initSuccess = session->initializeSync("sync-client", "1.0.0");
        assert(initSuccess && "Sync initialize failed.");

        auto tools = session->listToolsSync();
        assert(tools.size() == 1 && tools[0].name == "sync_tool" && "Sync listTools failed.");

        serverThread.join();
        std::cout << "  [✓] Scenario 4: Synchronous API blocking call and response validation\n";
    }
}
