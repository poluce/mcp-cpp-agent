#pragma once

#include <QJsonObject>
#include <QMap>
#include <QList>
#include <QString>
#include <functional>
#include <mutex>

namespace mcp_qt {

/**
 * @brief 资源更新通知路由器
 *
 * 维护 URI -> callback 列表的映射，实现 resources/updated 通知的精确路由。
 * 这是一个独立的轻量级辅助类，与传输层和会话层解耦。
 *
 * 策略：Notify-Only（方案1）——仅将原始通知 payload 传给订阅者，不触发额外的 readResource() 调用。
 * 调用方可根据需要自行决定是否读取资源内容。
 */
class McpResourceSubscriptionRouter {
public:
    using Callback = std::function<void(const QString& uri, const QJsonObject& params)>;

    McpResourceSubscriptionRouter() = default;

    /**
     * @brief 注册一个 URI 的更新回调
     * @param uri  要监听的资源 URI
     * @param cb   收到更新时调用的回调（在触发线程上调用，调用方负责线程安全）
     * @return 用于后续撤销的 token ID（每次注册递增）
     */
    int subscribe(const QString& uri, Callback cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int token = ++m_nextToken;
        m_subscribers[uri].append({token, std::move(cb)});
        return token;
    }

    /**
     * @brief 撤销指定 token 的回调
     * @param uri    资源 URI
     * @param token  subscribe() 返回的 token ID
     * @return 是否找到并删除了该订阅（false 表示 token 已过期或 URI 不匹配）
     */
    bool unsubscribe(const QString& uri, int token) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_subscribers.find(uri);
        if (it == m_subscribers.end()) return false;
        auto& list = it.value();
        for (int i = 0; i < list.size(); ++i) {
            if (list[i].token == token) {
                list.removeAt(i);
                if (list.isEmpty()) {
                    m_subscribers.erase(it);
                }
                return true;
            }
        }
        return false;
    }

    /**
     * @brief 撤销某个 URI 下的所有订阅
     */
    void unsubscribeAll(const QString& uri) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscribers.remove(uri);
    }

    /**
     * @brief 派发一个 URI 的更新通知到所有订阅者
     * @param uri     更新的资源 URI
     * @param params  通知原始 params（包含 uri 及其他字段）
     * @return 实际派发的回调数量
     */
    int dispatch(const QString& uri, const QJsonObject& params) {
        QList<Entry> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_subscribers.find(uri);
            if (it == m_subscribers.end()) return 0;
            // 在锁内拷贝快照，锁外执行用户回调，避免回调里退订时发生自锁。
            snapshot = it.value();
        }

        int count = 0;
        for (const auto& entry : snapshot) {
            if (entry.callback) {
                entry.callback(uri, params);
                ++count;
            }
        }
        return count;
    }

    /// 检查某个 URI 是否有至少一个活跃订阅
    bool hasSubscribers(const QString& uri) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_subscribers.find(uri);
        return it != m_subscribers.end() && !it.value().isEmpty();
    }

    /// 返回已订阅的对象 URI 集合
    QList<QString> subscribedUris() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_subscribers.keys();
    }

    /// 清除所有订阅
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscribers.clear();
    }

private:
    struct Entry {
        int token;
        Callback callback;
    };

    mutable std::mutex m_mutex;
    QMap<QString, QList<Entry>> m_subscribers;
    int m_nextToken{0};
};

} // namespace mcp_qt
