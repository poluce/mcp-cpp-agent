#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace mcp_qt {

class McpParser {
public:
    /**
     * @brief 从 tool 响应的 content 数组中提取并解码第一个 image 类型的 Base64 数据
     * @param contentArray MCP 响应中的 content 数组
     * @return 解码后的图片原始字节数据，如果未找到或解码失败则返回空 QByteArray
     */
    static QByteArray extractImageBase64(const QJsonArray& contentArray) {
        for (const QJsonValue& val : contentArray) {
            if (val.isObject()) {
                QJsonObject item = val.toObject();
                if (item.value("type").toString() == QStringLiteral("image")) {
                    QString dataStr = item.value("data").toString();
                    if (!dataStr.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                        auto decodeResult = QByteArray::fromBase64Encoding(
                            dataStr.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
                        if (decodeResult.decodingStatus == QByteArray::Base64DecodingStatus::Ok) {
                            return decodeResult.decoded;
                        }
#else
                        return QByteArray::fromBase64(dataStr.toLatin1());
#endif
                    }
                }
            }
        }
        return QByteArray();
    }

    /**
     * @brief 从 tool 响应的完整 result 或者是 McpResult.data 中提取并解码 image 类型的 Base64 数据
     * @param resultObj 包含 "content" 键的工具响应对象
     * @return 解码后的图片原始字节数据，如果未找到或解码失败则返回空 QByteArray
     */
    static QByteArray extractImageBase64(const QJsonObject& resultObj) {
        if (resultObj.contains(QStringLiteral("content")) && resultObj.value(QStringLiteral("content")).isArray()) {
            return extractImageBase64(resultObj.value(QStringLiteral("content")).toArray());
        }
        return QByteArray();
    }
};

} // namespace mcp_qt
