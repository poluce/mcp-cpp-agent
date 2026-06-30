#include "tests/common.h"

void test_traffic_logging() {
    auto transport = std::make_shared<MockTransport>();
    auto session = std::make_shared<mcp::McpClientSession>(transport);
    session->init();
    session->start();

    std::vector<mcp::McpTrafficEvent> events;
    session->setTrafficCallback([&](const mcp::McpTrafficEvent& ev) {
        events.push_back(ev);
    });

    // 1. 出站初始化请求
    session->initialize("test-traffic-client", "1.0.0", [](bool, const mcp::json&){});

    TM_ASSERT_EQ(events.size(), 1, "Should capture outbound initialize request");
    if (!events.empty()) {
        TM_ASSERT_TRUE(events[0].direction == mcp::McpTrafficDirection::Outbound, "Direction should be outbound");
        TM_ASSERT_TRUE(events[0].kind == mcp::McpTrafficKind::Request, "Kind should be request");
        TM_ASSERT_EQ(events[0].payload["method"], "initialize", "Method should be initialize");
        TM_ASSERT_TRUE(events[0].payload.contains("id"), "Request should have an id");
    }

    // 2. 入站初始化响应（会同步触发 session 内部发送 notifications/initialized 出站通知）
    int64_t reqId = events[0].payload["id"].get<int64_t>();
    mcp::json initResp = {
        {"jsonrpc", "2.0"},
        {"id", reqId},
        {"result", {
            {"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION},
            {"capabilities", mcp::json::object()},
            {"serverInfo", {{"name", "mock-server"}, {"version", "0.1.0"}}}
        }}
    };
    transport->pushServerMessage(initResp.dump());

    // 此时应该捕获到 3 个事件：1: initialize request, 2: initialize response, 3: notifications/initialized notify
    TM_ASSERT_EQ(events.size(), 3, "Should capture initialize response and auto notification");
    if (events.size() >= 3) {
        // 验证第 2 个事件 (Inbound Response)
        TM_ASSERT_TRUE(events[1].direction == mcp::McpTrafficDirection::Inbound, "events[1] Direction should be inbound");
        TM_ASSERT_TRUE(events[1].kind == mcp::McpTrafficKind::Response, "events[1] Kind should be response");
        TM_ASSERT_TRUE(events[1].payload.contains("result"), "events[1] Payload should contain result");
        TM_ASSERT_EQ(events[1].payload["id"], reqId, "events[1] Response id should match request id");

        // 验证第 3 个事件 (Auto Outbound Notification)
        TM_ASSERT_TRUE(events[2].direction == mcp::McpTrafficDirection::Outbound, "events[2] Direction should be outbound");
        TM_ASSERT_TRUE(events[2].kind == mcp::McpTrafficKind::Notification, "events[2] Kind should be notification");
        TM_ASSERT_EQ(events[2].payload["method"], "notifications/initialized", "events[2] Method should match");
    }

    // 3. 手动发送出站通知
    session->sendNotification("notifications/my_custom_notify", mcp::json::object());

    TM_ASSERT_EQ(events.size(), 4, "Should capture manual outbound notification");
    if (events.size() >= 4) {
        TM_ASSERT_TRUE(events[3].direction == mcp::McpTrafficDirection::Outbound, "events[3] Direction should be outbound");
        TM_ASSERT_TRUE(events[3].kind == mcp::McpTrafficKind::Notification, "events[3] Kind should be notification");
        TM_ASSERT_EQ(events[3].payload["method"], "notifications/my_custom_notify", "events[3] Method should match");
    }

    // 4. 接收服务端通知
    mcp::json srvNotify = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/resources/list_changed"}
    };
    transport->pushServerMessage(srvNotify.dump());

    TM_ASSERT_EQ(events.size(), 5, "Should capture inbound notification");
    if (events.size() >= 5) {
        TM_ASSERT_TRUE(events[4].direction == mcp::McpTrafficDirection::Inbound, "events[4] Direction should be inbound");
        TM_ASSERT_TRUE(events[4].kind == mcp::McpTrafficKind::Notification, "events[4] Kind should be notification");
        TM_ASSERT_EQ(events[4].payload["method"], "notifications/resources/list_changed", "events[4] Method should match");
    }
}
