#include "tests/common.h"
#include "mcp_core/McpReconnectPolicy.h"

// 验证 McpReconnectPolicy 指数避退算法及其在 Core 层的退避表现

void test_reconnect_policy_delay_calculation() {
    mcp::McpReconnectPolicy policy;
    policy.enabled = true;
    policy.initialDelayMs = 100;
    policy.maxDelayMs = 1000;
    policy.multiplier = 2.0;
    policy.maxAttempts = 5;

    // 验证 getDelayMs 核心算法
    TM_ASSERT_EQ(policy.getDelayMs(0), 100, "0 attempt should return initialDelayMs");
    TM_ASSERT_EQ(policy.getDelayMs(1), 100, "1st attempt: 100ms");
    TM_ASSERT_EQ(policy.getDelayMs(2), 200, "2nd attempt: 200ms");
    TM_ASSERT_EQ(policy.getDelayMs(3), 400, "3rd attempt: 400ms");
    TM_ASSERT_EQ(policy.getDelayMs(4), 800, "4th attempt: 800ms");
    TM_ASSERT_EQ(policy.getDelayMs(5), 1000, "5th attempt: capped at maxDelayMs (1000ms)");
    TM_ASSERT_EQ(policy.getDelayMs(6), 1000, "Out of bounds attempt: capped at maxDelayMs");
}

void test_recovery() {
    TM_RUN_TEST(test_reconnect_policy_delay_calculation);
}
