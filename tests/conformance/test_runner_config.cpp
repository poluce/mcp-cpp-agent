#include "tests/common.h"
#include "conformance_runner/RunnerConfig.h"
#include "conformance_runner/ScenarioContext.h"

void test_runner_config_parses_http_url_and_env_scenario() {
    mcp_conformance::RunnerConfig config;
    const char* argv[] = {"mcp_client_conformance.exe", "https://example.test/mcp"};

    bool ok = mcp_conformance::parseRunnerConfig(
        2,
        argv,
        "tools_call",
        R"({"name":"tools_call","arguments":{"a":1}})",
        &config
    );

    TM_ASSERT_TRUE(ok, "Runner config should parse");
    TM_ASSERT_EQ(config.serverUrl, std::string("https://example.test/mcp"), "Server URL should match");
    TM_ASSERT_EQ(config.scenario, std::string("tools_call"), "Scenario should match");
}

void test_runner_config_fails_without_scenario() {
    mcp_conformance::RunnerConfig config;
    const char* argv[] = {"mcp_client_conformance.exe"};

    bool ok = mcp_conformance::parseRunnerConfig(
        1,
        argv,
        "",
        "",
        &config
    );

    TM_ASSERT_FALSE(ok, "Runner config should fail without scenario");
}

void test_runner_config_http_mode_flag() {
    mcp_conformance::RunnerConfig config;
    const char* argv[] = {"mcp_client_conformance.exe", "http://localhost:8080/mcp"};

    bool ok = mcp_conformance::parseRunnerConfig(
        2,
        argv,
        "initialize",
        "{}",
        &config
    );

    TM_ASSERT_TRUE(ok, "Should parse http URL");
    TM_ASSERT_TRUE(config.httpMode, "httpMode should be true for http:// URL");
}

void test_runner_config_keeps_auth_context_fields() {
    mcp_conformance::RunnerConfig config;
    const char* argv[] = {"mcp_client_conformance.exe", "https://example.test/mcp"};

    bool ok = mcp_conformance::parseRunnerConfig(
        2,
        argv,
        "auth/client-credentials-basic",
        R"({"name":"auth/client-credentials-basic","client_id":"abc","client_secret":"def"})",
        &config
    );

    TM_ASSERT_TRUE(ok, "Runner config should parse auth context");
    TM_ASSERT_EQ(config.context["client_id"].get<std::string>(), std::string("abc"), "client_id should match");
    TM_ASSERT_EQ(config.context["client_secret"].get<std::string>(), std::string("def"), "client_secret should match");
}

void test_context_helpers() {
    mcp_conformance::RunnerConfig config;
    const char* argv[] = {"mcp_client_conformance.exe", "https://example.test/mcp"};

    bool ok = mcp_conformance::parseRunnerConfig(
        2,
        argv,
        "auth/client-credentials-basic",
        R"({"name":"auth/client-credentials-basic","client_id":"abc","extra":{"nested":true}})",
        &config
    );

    TM_ASSERT_TRUE(ok, "Should parse");

    TM_ASSERT_EQ(mcp_conformance::contextString(config, "client_id"), std::string("abc"), "contextString should work");
    TM_ASSERT_EQ(mcp_conformance::contextString(config, "nonexistent", "fallback"), std::string("fallback"), "contextString fallback");
    TM_ASSERT_TRUE(mcp_conformance::contextHasName(config, "auth/client-credentials-basic"), "contextHasName should match");

    auto extra = mcp_conformance::contextObject(config, "extra");
    TM_ASSERT_TRUE(extra.is_object(), "extra should be object");
    TM_ASSERT_EQ(extra["nested"].get<bool>(), true, "nested value should match");
}
