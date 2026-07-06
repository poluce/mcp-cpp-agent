#include "mcp_qt_transport/QtStatelessHttpTransport.h"
#include <nlohmann/json.hpp>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>

namespace mcp_qt {

QtStatelessHttpTransport::QtStatelessHttpTransport(const QString& endpointUrl, QObject* parent)
    : QObject(parent), m_endpointUrl(endpointUrl), m_nam(new QNetworkAccessManager(this))
{
    m_headers.insert("Content-Type", "application/json");
}

QtStatelessHttpTransport::~QtStatelessHttpTransport() {
    close();
}

bool QtStatelessHttpTransport::start() {
    if (m_isRunning) return true;
    m_isRunning = true;
    return true;
}

void QtStatelessHttpTransport::close() {
    if (!m_isRunning) return;
    m_isRunning = false;
    m_sseListenerActive = false;

    if (m_sseReply) {
        disconnect(m_sseReply, nullptr, this, nullptr);
        m_sseReply->abort();
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    }

    if (m_onClose) {
        m_onClose();
    }
}

QString QtStatelessHttpTransport::currentBearerToken() const {
    if (!m_tokenProvider) return {};
    return QString::fromStdString(m_tokenProvider());
}

void QtStatelessHttpTransport::applyCommonHeaders(QNetworkRequest& request, bool isGet) {
    // 自定义 headers
    for (auto it = m_headers.constBegin(); it != m_headers.constEnd(); ++it) {
        request.setRawHeader(it.key(), it.value());
    }

    // Accept: 声明支持 JSON 和 SSE
    request.setRawHeader("Accept", "application/json, text/event-stream");

    // MCP 协议版本
    request.setRawHeader("MCP-Protocol-Version", QByteArray::fromStdString(m_protocolVersion));

    // Session ID
    if (!m_sessionId.isEmpty()) {
        request.setRawHeader("MCP-Session-Id", m_sessionId.toUtf8());
    }

    // Authorization
    QString token = currentBearerToken();
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }
}

bool QtStatelessHttpTransport::send(const std::string& message) {
    if (!m_isRunning || !m_nam) return false;

    // 检测是否是 initialized 通知
    bool isInitializedNotification = false;
    try {
        auto json = nlohmann::json::parse(message, nullptr, false);
        if (!json.is_discarded() && json.contains("method")) {
            std::string method = json["method"].get<std::string>();
            isInitializedNotification = (method == "notifications/initialized");
        }
    } catch (...) {}

    // 缓存请求数据用于重试（非重试状态下）
    if (!m_isRetrying) {
        m_lastRequestData = QByteArray::fromStdString(message);
    }

    QNetworkRequest request(m_endpointUrl);
    applyCommonHeaders(request);

    QByteArray data = QByteArray::fromStdString(message);
    QNetworkReply* reply = m_nam->post(request, data);

    connect(reply, &QNetworkReply::finished, this, [this, reply, isInitializedNotification]() {
        onReplyFinished(reply);

        // initialized 通知后启动 GET SSE 监听
        if (isInitializedNotification && m_isRunning) {
            startSseListener();
        }
    });

    return true;
}

void QtStatelessHttpTransport::onReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();
    if (!m_isRunning) return;

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // 提取 Session ID
    QByteArray sessionId = reply->rawHeader("MCP-Session-Id");
    if (!sessionId.isEmpty()) {
        m_sessionId = QString::fromUtf8(sessionId);
    }

    // 401 处理 - OAuth 重试
    if (statusCode == 401 || statusCode == 403) {
        QString wwwAuth = QString::fromUtf8(reply->rawHeader("WWW-Authenticate"));
        m_authRetryCount++;

        if (m_authRetryCount <= kMaxAuthRetries && m_authRetryHandler) {
            if (m_authRetryHandler(wwwAuth.toStdString())) {
                // OAuth 成功，重试原始请求
                if (!m_lastRequestData.isEmpty()) {
                    m_isRetrying = true;
                    send(m_lastRequestData.toStdString());
                    m_isRetrying = false;
                }
                return;
            }
        }

        if (m_onError) {
            m_onError(QString("Auth failed: %1").arg(reply->errorString()).toStdString());
        }
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("HTTP POST failed: %1").arg(reply->errorString());
        QByteArray errorBody = reply->readAll();
        if (!errorBody.isEmpty()) {
            errorMsg += QStringLiteral("\nResponse Body: ") + QString::fromUtf8(errorBody);
        }
        if (m_onError) {
            m_onError(errorMsg.toStdString());
        }
        return;
    }

    // 重置 auth 重试计数
    m_authRetryCount = 0;

    QByteArray responseData = reply->readAll();
    if (responseData.isEmpty() || !m_onMessage) return;

    // 根据 Content-Type 被动适配
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    if (contentType.contains("text/event-stream")) {
        // SSE 格式 - 提取 data: 行
        QString response = QString::fromUtf8(responseData);
        QStringList lines = response.split('\n');
        for (const QString& line : lines) {
            if (line.startsWith("data: ")) {
                QString jsonStr = line.mid(6).trimmed();
                if (!jsonStr.isEmpty()) {
                    m_onMessage(jsonStr.toStdString());
                    return;
                }
            }
        }
        // 没有找到 data: 行，发送原始响应
        m_onMessage(responseData.toStdString());
    } else {
        // JSON 格式
        m_onMessage(responseData.toStdString());
    }
}

void QtStatelessHttpTransport::startSseListener() {
    if (!m_isRunning || m_sseListenerActive) return;
    m_sseListenerActive = true;

    QNetworkRequest request(m_endpointUrl);
    applyCommonHeaders(request, true);

    m_sseReply = m_nam->get(request);

    connect(m_sseReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_sseReply || !m_isRunning) return;
        handleSseResponse(m_sseReply);
    });

    connect(m_sseReply, &QNetworkReply::finished, this, [this]() {
        if (!m_sseReply || !m_isRunning) return;
        // SSE 流关闭
        m_sseListenerActive = false;
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    });

    connect(m_sseReply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError error) {
        if (!m_sseReply || !m_isRunning) return;
        // 405 表示服务端不支持 GET SSE，静默忽略
        if (error == QNetworkReply::ContentAccessDenied ||
            error == QNetworkReply::ContentOperationNotPermittedError) {
            m_sseListenerActive = false;
            return;
        }
        if (m_onError) {
            m_onError(QString("SSE listener error: %1").arg(m_sseReply->errorString()).toStdString());
        }
    });
}

void QtStatelessHttpTransport::handleSseResponse(QNetworkReply* reply) {
    QByteArray chunk = reply->readAll();
    m_sseBuffer.append(chunk);

    // 解析 SSE 事件
    while (true) {
        int pos = m_sseBuffer.indexOf("\n\n");
        if (pos == -1) break;

        QByteArray event = m_sseBuffer.left(pos);
        m_sseBuffer = m_sseBuffer.mid(pos + 2);

        processSseData(event);
    }
}

void QtStatelessHttpTransport::processSseData(const QByteArray& data) {
    QString str = QString::fromUtf8(data);
    QStringList lines = str.split('\n');

    for (const QString& line : lines) {
        if (line.startsWith("data: ")) {
            QString jsonStr = line.mid(6).trimmed();
            if (!jsonStr.isEmpty() && m_onMessage) {
                m_onMessage(jsonStr.toStdString());
            }
        }
    }
}

void QtStatelessHttpTransport::setOnMessage(std::function<void(const std::string&)> callback) {
    m_onMessage = std::move(callback);
}

void QtStatelessHttpTransport::setOnClose(std::function<void()> callback) {
    m_onClose = std::move(callback);
}

void QtStatelessHttpTransport::setOnError(std::function<void(const std::string&)> callback) {
    m_onError = std::move(callback);
}

void QtStatelessHttpTransport::setProtocolVersion(const std::string& version) {
    m_protocolVersion = version;
}

void QtStatelessHttpTransport::setCustomHeaders(const QMap<QByteArray, QByteArray>& headers) {
    m_headers = headers;
    if (!m_headers.contains("Content-Type")) {
        m_headers.insert("Content-Type", "application/json");
    }
}

void QtStatelessHttpTransport::setProxy(const QNetworkProxy& proxy) {
    if (m_nam) {
        m_nam->setProxy(proxy);
    }
}

void QtStatelessHttpTransport::setTokenProvider(TokenProvider provider) {
    m_tokenProvider = std::move(provider);
}

void QtStatelessHttpTransport::setAuthRetryHandler(AuthRetryHandler handler) {
    m_authRetryHandler = std::move(handler);
}

} // namespace mcp_qt
