#include "mcp_qt/QtHttpTransport.h"
#include <QNetworkRequest>
#include <QDebug>

namespace mcp {

QtHttpTransport::QtHttpTransport(const QUrl& sseUrl, QObject* parent)
    : QObject(parent)
    , m_sseUrl(sseUrl)
    , m_nam(new QNetworkAccessManager(this))
    , m_sseReply(nullptr)
{
    // Default fallback endpoint if not dynamically configured by SSE endpoint events
    m_postUrl = sseUrl.resolved(QUrl("message"));
    connect(m_nam, &QNetworkAccessManager::finished, this, &QtHttpTransport::handlePostFinished);
}

QtHttpTransport::~QtHttpTransport() {
    close();
}

bool QtHttpTransport::send(const std::string& message) {
    if (!m_postUrl.isValid()) {
        return false;
    }
    QNetworkRequest request(m_postUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QByteArray postData = QByteArray::fromStdString(message);
    m_nam->post(request, postData);
    return true;
}

void QtHttpTransport::setOnMessage(std::function<void(const std::string&)> callback) {
    m_onMessage = std::move(callback);
}

void QtHttpTransport::setOnClose(std::function<void()> callback) {
    m_onClose = std::move(callback);
}

void QtHttpTransport::setOnError(std::function<void(const std::string&)> callback) {
    m_onError = std::move(callback);
}

bool QtHttpTransport::start() {
    if (m_sseReply) {
        return false;
    }
    
    QNetworkRequest request(m_sseUrl);
    request.setRawHeader("Accept", "text/event-stream");
    
    m_sseReply = m_nam->get(request);
    connect(m_sseReply, &QNetworkReply::readyRead, this, &QtHttpTransport::handleSseReadyRead);
    connect(m_sseReply, &QNetworkReply::finished, this, &QtHttpTransport::handleSseFinished);
    
    return true;
}

void QtHttpTransport::close() {
    if (m_sseReply) {
        m_sseReply->abort();
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    }
}

void QtHttpTransport::handleSseReadyRead() {
    if (!m_sseReply) return;
    
    m_sseBuffer.append(m_sseReply->readAll());
    
    // Parse SSE block separated by double newlines \n\n or \r\n\r\n
    int blockEnd;
    while ((blockEnd = m_sseBuffer.indexOf("\n\n")) != -1) {
        QByteArray block = m_sseBuffer.left(blockEnd);
        m_sseBuffer.remove(0, blockEnd + 2);
        
        QString eventType = "message";
        QString dataContent;
        
        // Normalize line endings and split
        block.replace("\r", "");
        QList<QByteArray> lines = block.split('\n');
        for (const QByteArray& line : lines) {
            if (line.startsWith("event:")) {
                eventType = QString::fromUtf8(line.mid(6).trimmed());
            } else if (line.startsWith("data:")) {
                dataContent = QString::fromUtf8(line.mid(5).trimmed());
            }
        }
        
        if (!dataContent.isEmpty()) {
            if (eventType == "endpoint") {
                // MCP specification: server sends SSE "endpoint" event specifying target message URL
                m_postUrl = m_sseUrl.resolved(QUrl(dataContent));
            } else if (m_onMessage) {
                m_onMessage(dataContent.toStdString());
            }
        }
    }
}

void QtHttpTransport::handleSseFinished() {
    if (m_sseReply) {
        if (m_sseReply->error() != QNetworkReply::NoError && m_sseReply->error() != QNetworkReply::OperationCanceledError) {
            if (m_onError) {
                m_onError("SSE network error: " + m_sseReply->errorString().toStdString());
            }
        }
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    }
    if (m_onClose) {
        m_onClose();
    }
}

void QtHttpTransport::handlePostFinished(QNetworkReply* reply) {
    if (reply) {
        if (reply->error() != QNetworkReply::NoError) {
            if (m_onError) {
                m_onError("HTTP POST error: " + reply->errorString().toStdString());
            }
        }
        reply->deleteLater();
    }
}

} // namespace mcp
