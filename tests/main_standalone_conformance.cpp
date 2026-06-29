#include "tests/common.h"
#include <cstdlib>

void test_runner_config_parses_http_url_and_env_scenario();
void test_runner_config_fails_without_scenario();
void test_runner_config_http_mode_flag();
void test_runner_config_keeps_auth_context_fields();
void test_context_helpers();
void test_registry_contains_official_core_scenarios();
void test_registry_throws_for_unknown_scenario();
void test_runner_usage_text_is_not_machine_specific();

int main() {
    TmTestRunner::instance().startTestSuite("Conformance Runner Unit Tests");

    TmTestRunner::instance().startTestSuite("RunnerConfig Tests");
    TM_RUN_TEST(test_runner_config_parses_http_url_and_env_scenario);
    TM_RUN_TEST(test_runner_config_fails_without_scenario);
    TM_RUN_TEST(test_runner_config_http_mode_flag);
    TM_RUN_TEST(test_runner_config_keeps_auth_context_fields);
    TM_RUN_TEST(test_context_helpers);

    TmTestRunner::instance().startTestSuite("ScenarioRegistry Tests");
    TM_RUN_TEST(test_registry_contains_official_core_scenarios);
    TM_RUN_TEST(test_registry_throws_for_unknown_scenario);

    TmTestRunner::instance().startTestSuite("Process Tests");
    TM_RUN_TEST(test_runner_usage_text_is_not_machine_specific);

    TmTestRunner::instance().printSummary();

    if (TmTestRunner::instance().hasFailed()) {
        std::cerr << "\nSome tests FAILED!\n";
        std::exit(1);
    } else {
        std::cout << "\nAll Conformance Runner Tests PASSED!\n";
    }
    return 0;
}
