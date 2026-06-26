#include "tests/common.h"

void test_resources() {
    // Scenario 1: Paginated listResources Discovery
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

        // First page listResources (no cursor)
        bool page1Success = false;
        std::string page1NextCursor;
        session->listResources("", [&](const mcp::json& result, const std::string& nextCursor, const mcp::json& error) {
            if (error.empty() && result.contains("resources") && result["resources"].is_array() && result["resources"].size() == 2 && nextCursor == "res_page_2") {
                page1Success = true;
                page1NextCursor = nextCursor;
            }
        });

        mcp::json page1Resp = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"result", {
                {"resources", mcp::json::array({
                    {{"uri", "file:///logs/system.log"}, {"name", "sysLog"}, {"mimeType", "text/plain"}},
                    {{"uri", "file:///configs/app.json"}, {"name", "appJson"}, {"mimeType", "application/json"}}
                })},
                {"nextCursor", "res_page_2"}
            }}
        };
        transport->pushServerMessage(page1Resp.dump());
        TM_ASSERT_TRUE(page1Success, "Scenario 1: first page resources listing failed.");

        // Second page listResources (with cursor)
        bool page2Success = false;
        session->listResources(page1NextCursor, [&](const mcp::json& result, const std::string& nextCursor, const mcp::json& error) {
            if (error.empty() && result.contains("resources") && result["resources"].size() == 1 && nextCursor.empty()) {
                page2Success = true;
            }
        });

        mcp::json page2Resp = {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"result", {
                {"resources", mcp::json::array({
                    {{"uri", "file:///assets/logo.png"}, {"name", "logo"}, {"mimeType", "image/png"}}
                })}
            }}
        };
        transport->pushServerMessage(page2Resp.dump());
        TM_ASSERT_TRUE(page2Success, "Scenario 1: second page resources listing failed.");
    }

    // Scenario 2: resources/read Permission Denied, Large File, and Binary data with MIME type
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

        // 2.1: Permission Denied error code -32000
        bool permissionDeniedSuccess = false;
        session->readResource("file:///configs/admin.json", [&](const mcp::json&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32000) {
                permissionDeniedSuccess = true;
            }
        });
        mcp::json errPermissionResp = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"error", {{"code", -32000}, {"message", "Permission Denied"}}}
        };
        transport->pushServerMessage(errPermissionResp.dump());
        TM_ASSERT_TRUE(permissionDeniedSuccess, "Scenario 2.1: admin config should return permission denied.");

        // 2.2: Large file mock check (2MB)
        bool hugeFileSuccess = false;
        session->readResource("file:///logs/huge.log", [&](const mcp::json& result, const mcp::json& error) {
            if (error.empty() && result.contains("contents") && result["contents"].is_array()) {
                std::string text = result["contents"][0]["text"].get<std::string>();
                if (text.length() == 2 * 1024 * 1024) {
                    hugeFileSuccess = true;
                }
            }
        });
        mcp::json hugeFileResp = {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"result", {
                {"contents", mcp::json::array({{
                    {"uri", "file:///logs/huge.log"},
                    {"mimeType", "text/plain"},
                    {"text", std::string(2 * 1024 * 1024, 'H')}
                }})}
            }}
        };
        transport->pushServerMessage(hugeFileResp.dump());
        TM_ASSERT_TRUE(hugeFileSuccess, "Scenario 2.2: large resource content parsing failed.");

        // 2.3: Binary base64 data and MIME check
        bool binaryFileSuccess = false;
        session->readResource("file:///assets/logo.png", [&](const mcp::json& result, const mcp::json& error) {
            if (error.empty() && result.contains("contents")) {
                auto contentItem = result["contents"][0];
                if (contentItem.contains("blob") && contentItem["mimeType"] == "image/png") {
                    binaryFileSuccess = true;
                }
            }
        });
        mcp::json binaryResp = {
            {"jsonrpc", "2.0"},
            {"id", 4},
            {"result", {
                {"contents", mcp::json::array({{
                    {"uri", "file:///assets/logo.png"},
                    {"mimeType", "image/png"},
                    {"blob", "iVBORw0KGgoAAAANSUhEUgAAAAUA"}
                }})}
            }}
        };
        transport->pushServerMessage(binaryResp.dump());
        TM_ASSERT_TRUE(binaryFileSuccess, "Scenario 2.3: binary data or MIME mapping failed.");
    }

    // Scenario 3: Resource Subscriptions (Subscribe / Unsubscribe)
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

        // 3.1: Subscribe to resource
        bool subscribeSuccess = false;
        session->subscribeResource("file:///logs/system.log", [&](bool success, const mcp::json& error) {
            if (success && error.empty()) {
                subscribeSuccess = true;
            }
        });
        mcp::json subResp = {{"jsonrpc", "2.0"}, {"id", 2}, {"result", mcp::json::object()}};
        transport->pushServerMessage(subResp.dump());
        TM_ASSERT_TRUE(subscribeSuccess, "Scenario 3.1: resource subscription failed.");

        // 3.2: Unsubscribe from resource
        bool unsubscribeSuccess = false;
        session->unsubscribeResource("file:///logs/system.log", [&](bool success, const mcp::json& error) {
            if (success && error.empty()) {
                unsubscribeSuccess = true;
            }
        });
        mcp::json unsubResp = {{"jsonrpc", "2.0"}, {"id", 3}, {"result", mcp::json::object()}};
        transport->pushServerMessage(unsubResp.dump());
        TM_ASSERT_TRUE(unsubscribeSuccess, "Scenario 3.2: resource unsubscription failed.");
    }
}
