#include "tests/common.h"
#include "conformance_runner/ScenarioRegistry.h"

void test_runner_usage_text_is_not_machine_specific() {
    const std::string usage = mcp_conformance::usageText();
    TM_ASSERT_STR_CONTAINS(usage, "MCP_CONFORMANCE_SCENARIO", "Usage should mention env var");
    TM_ASSERT_TRUE(usage.find("F:\\\\") == std::string::npos, "Usage text must not contain machine-specific absolute paths");
}
