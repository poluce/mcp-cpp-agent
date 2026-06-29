#include "mcp_qt_transport/QtHttpSseTransport.h"
#include "mcp_core/McpClientSession.h"
#include "tests/common.h"

void test_qt_transport_keeps_core_session_api_shape() {
    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>("https://example.test/mcp");
    auto session = std::make_shared<mcp::McpClientSession>(transport);
    session->init();

    TM_ASSERT_TRUE(session->state() == mcp::SessionState::Uninitialized, "Core session should accept Qt transport");
}
