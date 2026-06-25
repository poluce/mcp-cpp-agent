#include "tests/common.h"
#include "mcp_core/ConsoleStdioTransport.h"

void test_stdio_transport() {
    std::cout << "[Stdio Transport Test] Running ConsoleStdioTransport lifecycle tests...\n";
    
    auto transport = std::make_shared<mcp::ConsoleStdioTransport>();
    
    // 验证可以设置各种回调而不会崩溃
    transport->setOnMessage([](const std::string&){});
    transport->setOnClose([](){});
    transport->setOnError([](const std::string&){});
    
    // 由于 ConsoleStdioTransport 会无限在后台 readLoop 中 getline cin 阻塞，
    // 我们仅验证其成员变量和方法接口能正常初始化及调用。
    std::cout << "  [✓] Scenario 1: Verify ConsoleStdioTransport basic interface methods\n";
}
