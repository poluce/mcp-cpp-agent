#include "tests/common.h"
#include <cstdlib>

void runAllLocalTests() {
    TmTestRunner::instance().startTestSuite("C++ MCP SDK Modular Integration Test Suite");

    // 1. Protocol Tests
    TmTestRunner::instance().startTestSuite("Protocol Tests");
    TM_RUN_TEST(test_initialize);
    TM_RUN_TEST(test_json_rpc);
    TM_RUN_TEST(test_capabilities);
    TM_RUN_TEST(test_error_response);

    // 2. Transport Tests
    TmTestRunner::instance().startTestSuite("Transport Tests");
    TM_RUN_TEST(test_stdio_transport);
    TM_RUN_TEST(test_http_transport);
    TM_RUN_TEST(test_process_lifecycle);

    // 3. Feature Tests
    TmTestRunner::instance().startTestSuite("Feature Tests");
    TM_RUN_TEST(test_tools);
    TM_RUN_TEST(test_resources);
    TM_RUN_TEST(test_prompts);
    TM_RUN_TEST(test_notifications);

    // 4. Integration Tests
    TmTestRunner::instance().startTestSuite("Integration Tests");
    TM_RUN_TEST(test_with_filesystem_server);
    TM_RUN_TEST(test_with_anysearch_mcp);
    TM_RUN_TEST(test_with_inspector_cases);

    TmTestRunner::instance().printSummary();
    
    if (TmTestRunner::instance().hasFailed()) {
        std::cerr << "\n❌ Some tests FAILED!\n";
        std::exit(1);
    } else {
        std::cout << "\n🎉 🎉 🎉 All Modular Self-Tests PASSED!\n";
    }
}
