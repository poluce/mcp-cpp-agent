#include "tests/common.h"
#include "conformance_runner/ScenarioRegistry.h"

void test_registry_contains_official_core_scenarios() {
    const auto names = mcp_conformance::registeredScenarioNames();

    TM_ASSERT_TRUE(names.count("initialize") == 1, "initialize must be registered");
    TM_ASSERT_TRUE(names.count("tools_call") == 1, "tools_call must be registered");
    TM_ASSERT_TRUE(names.count("sse-retry") == 1, "sse-retry must be registered");
    TM_ASSERT_TRUE(names.count("elicitation-sep1034-client-defaults") == 1, "elicitation defaults must be registered");
    TM_ASSERT_TRUE(names.count("auth/basic-cimd") == 1, "auth/basic-cimd must be registered");
    TM_ASSERT_TRUE(names.count("auth/pre-registration") == 1, "auth/pre-registration must be registered");
}

void test_registry_throws_for_unknown_scenario() {
    mcp_conformance::RunnerConfig config;
    config.scenario = "nonexistent-scenario";

    bool threw = false;
    try {
        mcp_conformance::runScenario(config);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    TM_ASSERT_TRUE(threw, "runScenario should throw for unknown scenario");
}
