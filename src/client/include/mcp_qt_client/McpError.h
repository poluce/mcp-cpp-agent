#pragma once
#include <QString>
#include <QJsonObject>
#include <QMetaType>

namespace mcp_qt {

/**
 * @brief 强类型错误上下文对象
 * 封装了底层传输错误和 JSON-RPC 协议层错误，供业务层进行精确重试或降级。
 */
struct McpError {
    int code{0};           ///< JSON-RPC 错误码，或自定义的系统级错误码 (如 401, 500)
    QString message;       ///< 错误摘要信息
    QJsonObject data;      ///< 附加的详细数据 (对于 JSON-RPC 错误可能是扩展的堆栈或详情)

    QString toString() const {
        if (code != 0) {
            return QStringLiteral("[%1] %2").arg(code).arg(message);
        }
        return message;
    }
};

} // namespace mcp_qt

Q_DECLARE_METATYPE(mcp_qt::McpError)
