#include "tests/common.h"

void runAllLocalTests() {
    std::cout << "==================================================\n";
    std::cout << "  C++ MCP SDK Modular Integration Test Suite\n";
    std::cout << "==================================================\n\n";

    // 1. Protocol Tests
    test_initialize();
    std::cout << "\n";
    test_json_rpc();
    std::cout << "\n";
    test_capabilities();
    std::cout << "\n";
    test_error_response();
    std::cout << "\n";

    // 2. Transport Tests
    test_stdio_transport();
    std::cout << "\n";
    test_http_transport();
    std::cout << "\n";
    test_process_lifecycle();
    std::cout << "\n";

    // 3. Feature Tests
    test_tools();
    std::cout << "\n";
    test_resources();
    std::cout << "\n";
    test_prompts();
    std::cout << "\n";
    test_notifications();
    std::cout << "\n";

    // 4. Integration Tests
    test_with_filesystem_server();
    std::cout << "\n";
    test_with_anysearch_mcp();
    std::cout << "\n";
    test_with_inspector_cases();
    std::cout << "\n";

    std::cout << "==================================================\n";
    std::cout << "  🎉 🎉 🎉 All Modular Self-Tests PASSED!\n";
    std::cout << "==================================================\n";
}
