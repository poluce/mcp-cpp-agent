#pragma once

namespace mcp {

/**
 * @brief 重连与指数退避策略配置
 */
struct McpReconnectPolicy {
    bool enabled{true};             ///< 是否启用自动重连
    int initialDelayMs{250};        ///< 初始重试延迟（毫秒）
    int maxDelayMs{5000};           ///< 最大重试延迟（毫秒）
    double multiplier{2.0};         ///< 指数避退乘数
    int maxAttempts{-1};            ///< 最大重试次数（-1 表示无限制）

    /**
     * @brief 计算给定尝试次数下的指数退避延迟
     * @param attempt 当前尝试次数（1-indexed，即第一次重试为 1）
     * @return 实际需要等待的延迟（毫秒）
     */
    int getDelayMs(int attempt) const {
        if (attempt <= 0) return initialDelayMs;
        double delay = initialDelayMs;
        for (int i = 1; i < attempt; ++i) {
            delay *= multiplier;
            if (delay >= maxDelayMs) return maxDelayMs;
        }
        return static_cast<int>(delay);
    }
};

} // namespace mcp
