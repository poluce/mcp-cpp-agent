#include "QtHttpSseWorker.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace mcp_qt {

// 从相对路径解析为完整 URL
static QString resolveUrl(const QString& base, const QString& relative) {
    if (relative.isEmpty()) return base;
    if (relative.contains("://")) return relative;

    int schemeEnd = base.indexOf("://");
    int hostStart = (schemeEnd != -1) ? schemeEnd + 3 : 0;

    if (relative[0] == '/') {
        int pathStart = base.indexOf('/', hostStart);
        if (pathStart != -1) {
            return base.left(pathStart) + relative;
        }
        return base + relative;
    }

    int pathStart = base.indexOf('/', hostStart);
    if (pathStart == -1) {
        return base + "/" + relative;
    }

    int lastSlash = base.lastIndexOf('/');
    if (lastSlash != -1 && lastSlash >= hostStart) {
        QString lastPart = base.mid(lastSlash + 1);
        if (!lastPart.isEmpty() && !lastPart.contains('.')) {
            return base + "/" + relative;
        }
        return base.left(lastSlash + 1) + relative;
    }
    return base + "/" + relative;
}

QtHttpSseWorker::QtHttpSseWorker(QString baseUrl, QObject* parent)
    : QObject(parent), m_baseUrl(std::move(baseUrl)), m_postUrl(m_baseUrl) {
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setTimerType(Qt::PreciseTimer);

    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        openSse();
    });

    m_healthCheckTimer = new QTimer(this);
    m_healthCheckTimer->setInterval(50);
    connect(m_healthCheckTimer, &QTimer::timeout, this, [this]() {
        if (!m_sseConnected || m_stopping) return;
        if (m_lastDataTime.isValid() && m_lastDataTime.elapsed() > 100) {
            // ONLY trigger hack if it has been at least 5000ms since the last hack
            if (!m_lastHackTime.isValid() || m_lastHackTime.elapsed() > 5000) {
                m_lastHackTime.start();
                if (m_sseReply) {
                    m_sseReply->abort();
                }
            }
        }
    });

    m_parser.setRetryCallback([this](int retryMs) {
        m_retryMs = retryMs;
    });

    // IMPORTANT: Capture Last-Event-ID for reconnection!
    m_parser.setIdCallback([this](const std::string& id) {
        m_lastEventId = QString::fromStdString(id);
    });

    m_parser.setEventCallback([this](const QtSseEvent& event) {
        handleSseEvent(event);
    });
}

void QtHttpSseWorker::setProtocolVersion(const QString& version) { m_protocolVersion = version; }
void QtHttpSseWorker::setTokenProvider(std::function<std::string()> provider) { m_tokenProvider = std::move(provider); }
void QtHttpSseWorker::setAuthRetryHandler(std::function<bool(const std::string&)> handler) { m_authRetryHandler = std::move(handler); }
void QtHttpSseWorker::setRequestConfig(const QtHttpRequestConfig& config) {
    m_requestConfig = config;
    if (m_network && m_requestConfig.proxy) {
        m_network->setProxy(*m_requestConfig.proxy);
    }
}

void QtHttpSseWorker::startStream() {
    m_stopping = false;
    m_postUrl = m_baseUrl;
    m_endpointResolved = true;
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        if (m_requestConfig.proxy) {
            m_network->setProxy(*m_requestConfig.proxy);
        }
    }
    openSse();
}

void QtHttpSseWorker::stopStream() {
    m_stopping = true;
    m_reconnectTimer->stop();
    m_healthCheckTimer->stop();
    if (m_sseReply) {
        m_sseReply->abort();
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    }
    emit transportClosed();
}

QString QtHttpSseWorker::currentBearerToken() const {
    if (!m_tokenProvider) {
        return {};
    }
    return QString::fromStdString(m_tokenProvider());
}

void QtHttpSseWorker::openSse() {
    if (m_stopping) {
        return;
    }
    if (m_sseReply) {
        m_sseReply->abort();
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl));
    for (auto it = m_requestConfig.defaultHeaders.constBegin(); it != m_requestConfig.defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key(), it.value());
    }
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    
    // Disable transparent retries by setting MaximumRedirectsAllowedAttribute to 0? No, this is for redirects.
    // QNAM natively aborts and emits error if a chunked response is abruptly closed.

    request.setRawHeader("MCP-Protocol-Version", m_protocolVersion.toUtf8());
    if (!m_sessionId.isEmpty()) {
        request.setRawHeader("MCP-Session-Id", m_sessionId.toUtf8());
    }
    if (!m_lastEventId.isEmpty()) {
        request.setRawHeader("Last-Event-ID", m_lastEventId.toUtf8());
    }
    const QString token = currentBearerToken();
    if (!token.isEmpty() && m_requestConfig.allowAuthorizationOverride) {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }

    m_sseReply = m_network->get(request);
    
    connect(m_sseReply, &QNetworkReply::metaDataChanged, this, [this]() {
        if (m_stopping || !m_sseReply) return;
        int statusCode = m_sseReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString wwwAuthHeader = m_sseReply->rawHeader("WWW-Authenticate");
        
        if (statusCode == 401 || statusCode == 403) {
            m_authRetryCount++;
            if (m_authRetryCount <= kMaxAuthRetries && m_authRetryHandler && m_authRetryHandler(wwwAuthHeader.toStdString())) {
                m_sseReply->abort();
                openSse();
                return;
            }
        }
        
        m_authRetryCount = 0;
        const auto sessionHeader = m_sseReply->rawHeader("MCP-Session-Id");
        if (!sessionHeader.isEmpty()) {
            m_sessionId = QString::fromUtf8(sessionHeader);
        }

        m_sseConnected = true;
        m_lastDataTime.start();
        m_healthCheckTimer->start();
    });

    connect(m_sseReply, &QNetworkReply::readyRead, this, &QtHttpSseWorker::handleSseReadyRead);
    connect(m_sseReply, &QNetworkReply::finished, this, &QtHttpSseWorker::handleSseFinished);
    connect(m_sseReply, &QNetworkReply::errorOccurred, this, &QtHttpSseWorker::handleSseError);
}

void QtHttpSseWorker::handleSseReadyRead() {
    if (m_stopping || !m_sseReply) return;
    
    QByteArray data = m_sseReply->readAll();
    if (!data.isEmpty()) {
        m_lastDataTime.start();
        m_parser.pushChunk(data.toStdString());
    }
}

void QtHttpSseWorker::handleSseFinished() {
    if (m_stopping) return;
    if (!m_sseConnected) return; // Prevent double handling
    m_sseConnected = false;
    m_healthCheckTimer->stop();
    scheduleReconnect();
}

void QtHttpSseWorker::handleSseError(QNetworkReply::NetworkError code) {
    if (m_stopping || !m_sseReply) return;
    
    if (code == QNetworkReply::RemoteHostClosedError) {
        // Handled naturally by finished
        return;
    }
    
    m_healthCheckTimer->stop();
    if (code != QNetworkReply::OperationCanceledError) {
        emit transportError(m_sseReply->errorString());
    }
}

void QtHttpSseWorker::handleSseEvent(const QtSseEvent& event) {
    if (event.eventName == "endpoint") {
        m_postUrl = resolveUrl(m_baseUrl, QString::fromStdString(event.data));
        m_endpointResolved = true;
        flushPendingMessages();
        return;
    }
    emit messageReceived(QString::fromStdString(event.data));
}

void QtHttpSseWorker::scheduleReconnect() {
    if (m_stopping) return;
    if (m_reconnectTimer->isActive()) return;
    
    // Exactly use m_retryMs without subtracting anything!
    int waitTime = m_retryMs;
    m_reconnectTimer->start(waitTime);
}

bool QtHttpSseWorker::postMessage(const QString& payload, int retryCount) {
    if (m_stopping) {
        return false;
    }
    if (!m_endpointResolved) {
        m_pendingMessages.append(payload);
        return true;
    }
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        if (m_requestConfig.proxy) {
            m_network->setProxy(*m_requestConfig.proxy);
        }
    }
    QNetworkRequest request;
    request.setUrl(QUrl(m_postUrl));
    for (auto it = m_requestConfig.defaultHeaders.constBegin(); it != m_requestConfig.defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key(), it.value());
    }
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("MCP-Protocol-Version", m_protocolVersion.toUtf8());
    if (!m_sessionId.isEmpty()) {
        request.setRawHeader("MCP-Session-Id", m_sessionId.toUtf8());
    }
    const QString token = currentBearerToken();
    if (!token.isEmpty() && m_requestConfig.allowAuthorizationOverride) {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }

    QNetworkReply* reply = m_network->post(request, payload.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, payload, retryCount]() {
        const auto sessionHeader = reply->rawHeader("MCP-Session-Id");
        bool justGotSession = !sessionHeader.isEmpty() && m_sessionId.isEmpty();
        if (!sessionHeader.isEmpty()) {
            m_sessionId = QString::fromUtf8(sessionHeader);
        }

        if (justGotSession) {
            m_reconnectTimer->stop();
            for (int i = 0; i < 50 && !m_sseConnected && !m_stopping; ++i) {
                if (!m_sseReply) openSse();
                QEventLoop loop;
                QTimer::singleShot(100, &loop, &QEventLoop::quit);
                loop.exec();
            }
        }

        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString wwwAuthHeader = reply->rawHeader("WWW-Authenticate");

        if (statusCode == 401 || statusCode == 403) {
            if (m_authRetryHandler && retryCount < 3 && m_authRetryHandler(wwwAuthHeader.toStdString())) {
                reply->deleteLater();
                postMessage(payload, retryCount + 1);
                return;
            }
            QString errMsg = QString("HTTP %1: Post message authentication failed").arg(statusCode);
            QByteArray authErrBody = reply->readAll();
            if (!authErrBody.isEmpty()) {
                errMsg += QStringLiteral("\nResponse Body: ") + QString::fromUtf8(authErrBody);
            }
            emit transportError(errMsg);
            reply->deleteLater();
            return;
        }

        const QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            if (contentType.contains("text/event-stream", Qt::CaseInsensitive)) {
                QtSseParser postParser;
                postParser.setRetryCallback([this](int retryMs) {
                    m_retryMs = retryMs;
                });
                postParser.setIdCallback([this](const std::string& id) {
                    m_lastEventId = QString::fromStdString(id);
                });
                postParser.setEventCallback([this](const QtSseEvent& event) {
                    if (!event.lastEventId.empty()) {
                        m_lastEventId = QString::fromStdString(event.lastEventId);
                    }
                    emit messageReceived(QString::fromStdString(event.data));
                });
                std::string sseData = QString::fromUtf8(body).toStdString();
                if (sseData.rfind("\n\n") == std::string::npos) {
                    sseData += "\n\n";
                }
                postParser.pushChunk(sseData);
            } else {
                emit messageReceived(QString::fromUtf8(body));
            }
        }
        reply->deleteLater();
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError) {
        QString errMsg = reply->errorString();
        QByteArray errorBody = reply->readAll();
        if (!errorBody.isEmpty()) {
            errMsg += QStringLiteral("\nResponse Body: ") + QString::fromUtf8(errorBody);
        }
        emit transportError(errMsg);
    });

    return true;
}

void QtHttpSseWorker::flushPendingMessages() {
    for (const auto& msg : m_pendingMessages) {
        postMessage(msg);
    }
    m_pendingMessages.clear();
}

} // namespace mcp_qt
