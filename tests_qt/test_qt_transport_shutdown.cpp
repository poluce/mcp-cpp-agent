#include "mcp_qt_transport/QtHttpSseTransport.h"
#include "tests/common.h"
#include <QCoreApplication>

void test_qt_transport_constructs() {
    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>("https://example.test/mcp");
    TM_ASSERT_TRUE(static_cast<bool>(transport), "Qt transport should construct");
}

void test_qt_transport_shutdown_is_idempotent() {
    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>("https://example.test/mcp");
    int closeCount = 0;
    transport->setOnClose([&]() { ++closeCount; });

    TM_ASSERT_TRUE(transport->start(), "First start should succeed");
    transport->close();
    transport->close();

    QCoreApplication::processEvents();

    TM_ASSERT_EQ(closeCount, 1, "close should emit onClose exactly once");
}
