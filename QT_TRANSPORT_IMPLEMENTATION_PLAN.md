# Qt HTTP/SSE Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Qt-based HTTP/HTTPS + SSE transport layer for remote MCP servers without polluting `mcp_core` with Qt dependencies, while keeping existing `stdio` support intact.

**Architecture:** Keep `mcp_core` responsible for protocol/session logic only. Add a new `mcp_qt_transport` module that implements `IMcpTransport` using `QtCore` + `QtNetwork`, with a dedicated worker thread, explicit SSE parsing, and auth hooks instead of built-in OAuth flow logic. Mark the existing pure C++ HTTP transport as legacy/experimental rather than the preferred remote transport.

**Tech Stack:** C++17, CMake, Qt6 Core, Qt6 Network, existing `mcp_core`, existing custom test harness, Node test server for subprocess tests

---

## File Structure

- Modify: `CMakeLists.txt`
  Purpose: Add an opt-in Qt transport target without making Qt mandatory for `mcp_core`.
- Modify: `mcp_core/CMakeLists.txt`
  Purpose: Keep current core target clean and independent from the new Qt adapter.
- Modify: `mcp_core/include/mcp_core/mcp_core.h`
  Purpose: Keep the umbrella header focused on core APIs only; do not include Qt transport here.
- Modify: `mcp_core/include/mcp_core/HttpSseTransport.h`
  Purpose: Mark the current implementation as legacy/experimental in comments.
- Modify: `README.md`
  Purpose: Document the new recommended transport split: `SubprocessStdioTransport` for local servers, `QtHttpSseTransport` for remote HTTP/HTTPS servers in Qt agents.
- Create: `mcp_qt_transport/CMakeLists.txt`
  Purpose: Define the new `mcp_qt_transport` static library and its tests.
- Create: `mcp_qt_transport/include/mcp_qt_transport/QtHttpSseTransport.h`
  Purpose: Public adapter implementing `mcp::IMcpTransport` with a pure-std external interface.
- Create: `mcp_qt_transport/src/QtHttpSseTransport.cpp`
  Purpose: Public wrapper, thread lifecycle, callback bridging, `IMcpTransport` implementation.
- Create: `mcp_qt_transport/src/QtHttpSseWorker.h`
  Purpose: Internal QObject worker contract running in a dedicated `QThread`.
- Create: `mcp_qt_transport/src/QtHttpSseWorker.cpp`
  Purpose: Own `QNetworkAccessManager`, POST/SSE logic, reconnection, session headers, auth retry flow.
- Create: `mcp_qt_transport/src/QtSseParser.h`
  Purpose: Small focused SSE parser with no MCP-specific logic.
- Create: `mcp_qt_transport/src/QtSseParser.cpp`
  Purpose: Incremental parsing for `event:`, `data:`, `id:`, `retry:`.
- Create: `tests_qt/CMakeLists.txt`
  Purpose: Build a Qt-specific test executable only when Qt transport is enabled.
- Create: `tests_qt/test_qt_sse_parser.cpp`
  Purpose: Unit-test SSE parsing behavior independent of networking.
- Create: `tests_qt/test_qt_http_transport.cpp`
  Purpose: Integration-test HTTPS/SSE lifecycle with a local Qt HTTP test server or a focused mock server.
- Create: `tests_qt/test_qt_transport_shutdown.cpp`
  Purpose: Verify worker-thread shutdown and no detached-thread regressions.

### Task 1: Add the Qt transport build split

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `mcp_core/CMakeLists.txt`
- Create: `mcp_qt_transport/CMakeLists.txt`

- [ ] **Step 1: Write the failing build expectation into the root CMake plan**

Add this block near the existing HTTP option in `CMakeLists.txt`:

```cmake
option(MCP_ENABLE_QT_TRANSPORT "Build Qt-based HTTP/SSE transport for Qt agents" OFF)

if(MCP_ENABLE_QT_TRANSPORT)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    add_subdirectory(mcp_qt_transport)
endif()
```

Expected outcome: configuring with `-DMCP_ENABLE_QT_TRANSPORT=ON` should no longer fail because the subdirectory exists.

- [ ] **Step 2: Keep `mcp_core` free of Qt linkage**

Verify `mcp_core/CMakeLists.txt` remains structurally like this:

```cmake
add_library(mcp_core STATIC
    include/mcp_core/ConsoleStdioTransport.h
    include/mcp_core/SubprocessStdioTransport.h
    include/mcp_core/HttpSseTransport.h
    include/mcp_core/mcp_core.h
    include/mcp_core/McpResource.h
    include/mcp_core/McpPrompt.h
    include/mcp_core/IMcpTransport.h
    include/mcp_core/McpMessage.h
    include/mcp_core/McpTool.h
    include/mcp_core/McpClientSession.h
    include/mcp_core/JsonRpcDispatcher.h
    include/mcp_core/McpOAuthClient.h
    src/ConsoleStdioTransport.cpp
    src/SubprocessStdioTransport.cpp
    src/JsonRpcDispatcher.cpp
    src/McpClientSession.cpp
)
```

Do not add any `Qt6::Core` or `Qt6::Network` linkage here.

- [ ] **Step 3: Create the new transport target**

Create `mcp_qt_transport/CMakeLists.txt` with this initial content:

```cmake
add_library(mcp_qt_transport STATIC
    include/mcp_qt_transport/QtHttpSseTransport.h
    src/QtHttpSseTransport.cpp
    src/QtHttpSseWorker.h
    src/QtHttpSseWorker.cpp
    src/QtSseParser.h
    src/QtSseParser.cpp
)

target_include_directories(mcp_qt_transport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/mcp_core/include
)

target_link_libraries(mcp_qt_transport PUBLIC
    mcp_core
    Qt6::Core
    Qt6::Network
)

target_compile_features(mcp_qt_transport PUBLIC cxx_std_17)
target_compile_options(mcp_qt_transport PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_subdirectory(${CMAKE_SOURCE_DIR}/tests_qt ${CMAKE_BINARY_DIR}/tests_qt)
```

- [ ] **Step 4: Run configure to verify the target graph is valid**

Run:

```powershell
cmake -B build -DMCP_ENABLE_HTTP=ON -DMCP_ENABLE_QT_TRANSPORT=ON
```

Expected: configure succeeds and reports Qt6 Core/Network found.

- [ ] **Step 5: Commit**

Run:

```powershell
git add CMakeLists.txt mcp_core/CMakeLists.txt mcp_qt_transport/CMakeLists.txt
git commit -m "build: add qt transport target split"
```

### Task 2: Create the public Qt transport adapter

**Files:**
- Create: `mcp_qt_transport/include/mcp_qt_transport/QtHttpSseTransport.h`
- Create: `mcp_qt_transport/src/QtHttpSseTransport.cpp`

- [ ] **Step 1: Write the failing transport-shape test**

Create a compile-focused test in `tests_qt/test_qt_transport_shutdown.cpp` that includes the public header:

```cpp
#include "mcp_qt_transport/QtHttpSseTransport.h"
#include "tests/common.h"

void test_qt_transport_constructs() {
    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>("https://example.test/mcp");
    TM_ASSERT_TRUE(static_cast<bool>(transport), "Qt transport should construct");
}
```

Expected: this test fails to compile until the header and namespace exist.

- [ ] **Step 2: Add the public header with a std-only surface**

Create `mcp_qt_transport/include/mcp_qt_transport/QtHttpSseTransport.h`:

```cpp
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "mcp_core/IMcpTransport.h"

namespace mcp_qt {

class QtHttpSseTransport final : public mcp::IMcpTransport {
public:
    using TokenProvider = std::function<std::string()>;
    using AuthRetryHandler = std::function<bool(const std::string&)>;

    explicit QtHttpSseTransport(const std::string& baseUrl);
    ~QtHttpSseTransport() override;

    bool send(const std::string& message) override;
    void setOnMessage(std::function<void(const std::string&)> callback) override;
    void setOnClose(std::function<void()> callback) override;
    void setOnError(std::function<void(const std::string&)> callback) override;
    bool start() override;
    void close() override;
    void setProtocolVersion(const std::string& version) override;

    void setTokenProvider(TokenProvider provider);
    void setAuthRetryHandler(AuthRetryHandler handler);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mcp_qt
```

- [ ] **Step 3: Add the public wrapper implementation**

Create `mcp_qt_transport/src/QtHttpSseTransport.cpp`:

```cpp
#include "mcp_qt_transport/QtHttpSseTransport.h"

#include "QtHttpSseWorker.h"

#include <QMetaObject>
#include <QThread>

namespace mcp_qt {

class QtHttpSseTransport::Impl {
public:
    explicit Impl(const std::string& baseUrl)
        : url(baseUrl) {}

    std::string url;
    std::string protocolVersion{"2025-11-25"};
    std::function<void(const std::string&)> onMessage;
    std::function<void()> onClose;
    std::function<void(const std::string&)> onError;
    TokenProvider tokenProvider;
    AuthRetryHandler authRetryHandler;
    QThread* thread{nullptr};
    QtHttpSseWorker* worker{nullptr};
    bool running{false};
};

QtHttpSseTransport::QtHttpSseTransport(const std::string& baseUrl)
    : m_impl(std::make_unique<Impl>(baseUrl)) {}

QtHttpSseTransport::~QtHttpSseTransport() {
    close();
}

bool QtHttpSseTransport::start() {
    if (m_impl->running) {
        return false;
    }
    m_impl->thread = new QThread();
    m_impl->worker = new QtHttpSseWorker(QString::fromStdString(m_impl->url));
    m_impl->worker->moveToThread(m_impl->thread);
    m_impl->worker->setProtocolVersion(QString::fromStdString(m_impl->protocolVersion));
    m_impl->worker->setTokenProvider(m_impl->tokenProvider);
    m_impl->worker->setAuthRetryHandler(m_impl->authRetryHandler);

    QObject::connect(m_impl->thread, &QThread::started, m_impl->worker, &QtHttpSseWorker::startStream);
    QObject::connect(m_impl->worker, &QtHttpSseWorker::messageReceived, [this](const QString& msg) {
        if (m_impl->onMessage) m_impl->onMessage(msg.toStdString());
    });
    QObject::connect(m_impl->worker, &QtHttpSseWorker::transportError, [this](const QString& err) {
        if (m_impl->onError) m_impl->onError(err.toStdString());
    });
    QObject::connect(m_impl->worker, &QtHttpSseWorker::transportClosed, [this]() {
        if (m_impl->onClose) m_impl->onClose();
    });
    QObject::connect(m_impl->thread, &QThread::finished, m_impl->worker, &QObject::deleteLater);

    m_impl->thread->start();
    m_impl->running = true;
    return true;
}

void QtHttpSseTransport::close() {
    if (!m_impl->running) {
        return;
    }
    QMetaObject::invokeMethod(m_impl->worker, &QtHttpSseWorker::stopStream, Qt::BlockingQueuedConnection);
    m_impl->thread->quit();
    m_impl->thread->wait();
    delete m_impl->thread;
    m_impl->thread = nullptr;
    m_impl->worker = nullptr;
    m_impl->running = false;
}

bool QtHttpSseTransport::send(const std::string& message) {
    if (!m_impl->running || !m_impl->worker) {
        return false;
    }
    bool accepted = false;
    QMetaObject::invokeMethod(
        m_impl->worker,
        [&]() { accepted = m_impl->worker->postMessage(QString::fromStdString(message)); },
        Qt::BlockingQueuedConnection
    );
    return accepted;
}

void QtHttpSseTransport::setOnMessage(std::function<void(const std::string&)> callback) { m_impl->onMessage = std::move(callback); }
void QtHttpSseTransport::setOnClose(std::function<void()> callback) { m_impl->onClose = std::move(callback); }
void QtHttpSseTransport::setOnError(std::function<void(const std::string&)> callback) { m_impl->onError = std::move(callback); }
void QtHttpSseTransport::setProtocolVersion(const std::string& version) { m_impl->protocolVersion = version; }
void QtHttpSseTransport::setTokenProvider(TokenProvider provider) { m_impl->tokenProvider = std::move(provider); }
void QtHttpSseTransport::setAuthRetryHandler(AuthRetryHandler handler) { m_impl->authRetryHandler = std::move(handler); }

} // namespace mcp_qt
```

- [ ] **Step 4: Run the compile-focused test**

Run:

```powershell
cmake --build build --target tests_qt --config Release
```

Expected: if worker files are still missing, build fails on unresolved includes; after the next task, it should compile.

- [ ] **Step 5: Commit**

Run:

```powershell
git add mcp_qt_transport/include/mcp_qt_transport/QtHttpSseTransport.h mcp_qt_transport/src/QtHttpSseTransport.cpp tests_qt/test_qt_transport_shutdown.cpp
git commit -m "feat: add qt transport public adapter"
```

### Task 3: Implement a focused SSE parser

**Files:**
- Create: `mcp_qt_transport/src/QtSseParser.h`
- Create: `mcp_qt_transport/src/QtSseParser.cpp`
- Create: `tests_qt/test_qt_sse_parser.cpp`

- [ ] **Step 1: Write the failing parser tests**

Create `tests_qt/test_qt_sse_parser.cpp`:

```cpp
#include "mcp_qt_transport/QtSseParser.h"
#include "tests/common.h"

void test_qt_sse_parser_reads_multiline_data() {
    mcp_qt::QtSseParser parser;
    mcp_qt::QtSseEvent event;
    bool emitted = false;

    parser.setEventCallback([&](const mcp_qt::QtSseEvent& e) {
        event = e;
        emitted = true;
    });

    parser.pushChunk("event: message\ndata: {\"a\":1}\ndata: {\"b\":2}\nid: 55\n\n");

    TM_ASSERT_TRUE(emitted, "Parser should emit complete event");
    TM_ASSERT_EQ(event.eventName, std::string("message"), "Event type should match");
    TM_ASSERT_EQ(event.data, std::string("{\"a\":1}\n{\"b\":2}"), "Data should preserve multi-line join");
    TM_ASSERT_EQ(event.lastEventId, std::string("55"), "Event id should match");
}

void test_qt_sse_parser_reads_retry() {
    mcp_qt::QtSseParser parser;
    int retryMs = 0;

    parser.setRetryCallback([&](int value) { retryMs = value; });
    parser.pushChunk("retry: 1500\n\n");

    TM_ASSERT_EQ(retryMs, 1500, "Retry should be parsed");
}
```

- [ ] **Step 2: Add the parser header**

Create `mcp_qt_transport/src/QtSseParser.h`:

```cpp
#pragma once

#include <functional>
#include <string>

namespace mcp_qt {

struct QtSseEvent {
    std::string eventName{"message"};
    std::string data;
    std::string lastEventId;
};

class QtSseParser {
public:
    using EventCallback = std::function<void(const QtSseEvent&)>;
    using RetryCallback = std::function<void(int)>;

    void setEventCallback(EventCallback callback);
    void setRetryCallback(RetryCallback callback);
    void pushChunk(const std::string& chunk);
    void reset();

private:
    void flushEventBlock(const std::string& block);

    std::string m_buffer;
    EventCallback m_eventCallback;
    RetryCallback m_retryCallback;
};

} // namespace mcp_qt
```

- [ ] **Step 3: Add the parser implementation**

Create `mcp_qt_transport/src/QtSseParser.cpp`:

```cpp
#include "QtSseParser.h"

#include <sstream>

namespace mcp_qt {

void QtSseParser::setEventCallback(EventCallback callback) { m_eventCallback = std::move(callback); }
void QtSseParser::setRetryCallback(RetryCallback callback) { m_retryCallback = std::move(callback); }
void QtSseParser::reset() { m_buffer.clear(); }

void QtSseParser::pushChunk(const std::string& chunk) {
    m_buffer += chunk;
    std::size_t pos = 0;
    while ((pos = m_buffer.find("\n\n")) != std::string::npos) {
        const std::string block = m_buffer.substr(0, pos);
        m_buffer.erase(0, pos + 2);
        flushEventBlock(block);
    }
}

void QtSseParser::flushEventBlock(const std::string& block) {
    QtSseEvent event;
    std::istringstream stream(block);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("event:", 0) == 0) {
            event.eventName = line.substr(6);
            if (!event.eventName.empty() && event.eventName.front() == ' ') {
                event.eventName.erase(0, 1);
            }
        } else if (line.rfind("data:", 0) == 0) {
            std::string part = line.substr(5);
            if (!part.empty() && part.front() == ' ') {
                part.erase(0, 1);
            }
            if (!event.data.empty()) {
                event.data += "\n";
            }
            event.data += part;
        } else if (line.rfind("id:", 0) == 0) {
            event.lastEventId = line.substr(3);
            if (!event.lastEventId.empty() && event.lastEventId.front() == ' ') {
                event.lastEventId.erase(0, 1);
            }
        } else if (line.rfind("retry:", 0) == 0 && m_retryCallback) {
            std::string value = line.substr(6);
            if (!value.empty() && value.front() == ' ') {
                value.erase(0, 1);
            }
            m_retryCallback(std::stoi(value));
        }
    }

    if (!event.data.empty() && m_eventCallback) {
        m_eventCallback(event);
    }
}

} // namespace mcp_qt
```

- [ ] **Step 4: Run the parser tests**

Run:

```powershell
cmake --build build --target tests_qt --config Release
```

Expected: parser tests compile and pass.

- [ ] **Step 5: Commit**

Run:

```powershell
git add mcp_qt_transport/src/QtSseParser.h mcp_qt_transport/src/QtSseParser.cpp tests_qt/test_qt_sse_parser.cpp
git commit -m "test: add qt sse parser coverage"
```

### Task 4: Implement the worker-thread transport core

**Files:**
- Create: `mcp_qt_transport/src/QtHttpSseWorker.h`
- Create: `mcp_qt_transport/src/QtHttpSseWorker.cpp`

- [ ] **Step 1: Write the failing worker compile boundary**

Add the internal worker contract expected by the public wrapper:

```cpp
#include "QtHttpSseWorker.h"
```

Expected: current build fails until the worker exposes `startStream`, `stopStream`, `postMessage`, and the required Qt signals.

- [ ] **Step 2: Add the internal worker header**

Create `mcp_qt_transport/src/QtHttpSseWorker.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

#include "QtSseParser.h"

namespace mcp_qt {

class QtHttpSseWorker : public QObject {
    Q_OBJECT
public:
    explicit QtHttpSseWorker(QString baseUrl, QObject* parent = nullptr);

    void setProtocolVersion(const QString& version);
    void setTokenProvider(std::function<std::string()> provider);
    void setAuthRetryHandler(std::function<bool(const std::string&)> handler);

    bool postMessage(const QString& payload);

public slots:
    void startStream();
    void stopStream();

signals:
    void messageReceived(const QString& message);
    void transportError(const QString& error);
    void transportClosed();

private:
    void openSse();
    void handleSseReadyRead();
    void handleSseFinished();
    void handleSseError(QNetworkReply::NetworkError code);
    void handleSseEvent(const QtSseEvent& event);
    void scheduleReconnect();
    QString currentBearerToken() const;
    void applyCommonHeaders(class QNetworkRequest& request) const;

    QString m_baseUrl;
    QString m_postUrl;
    QString m_protocolVersion{"2025-11-25"};
    QString m_sessionId;
    QString m_lastEventId;
    int m_retryMs{2000};
    bool m_stopping{false};
    std::function<std::string()> m_tokenProvider;
    std::function<bool(const std::string&)> m_authRetryHandler;
    class QNetworkAccessManager* m_network{nullptr};
    QPointer<QNetworkReply> m_sseReply;
    QTimer* m_reconnectTimer{nullptr};
    QtSseParser m_parser;
};

} // namespace mcp_qt
```

- [ ] **Step 3: Add the worker implementation**

Create `mcp_qt_transport/src/QtHttpSseWorker.cpp`:

```cpp
#include "QtHttpSseWorker.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace mcp_qt {

QtHttpSseWorker::QtHttpSseWorker(QString baseUrl, QObject* parent)
    : QObject(parent), m_baseUrl(std::move(baseUrl)), m_postUrl(m_baseUrl) {
    m_network = new QNetworkAccessManager(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);

    connect(m_reconnectTimer, &QTimer::timeout, this, &QtHttpSseWorker::openSse);
    m_parser.setRetryCallback([this](int retryMs) { m_retryMs = retryMs; });
    m_parser.setEventCallback([this](const QtSseEvent& event) { handleSseEvent(event); });
}

void QtHttpSseWorker::setProtocolVersion(const QString& version) { m_protocolVersion = version; }
void QtHttpSseWorker::setTokenProvider(std::function<std::string()> provider) { m_tokenProvider = std::move(provider); }
void QtHttpSseWorker::setAuthRetryHandler(std::function<bool(const std::string&)> handler) { m_authRetryHandler = std::move(handler); }

void QtHttpSseWorker::startStream() {
    m_stopping = false;
    openSse();
}

void QtHttpSseWorker::stopStream() {
    m_stopping = true;
    m_reconnectTimer->stop();
    if (m_sseReply) {
        disconnect(m_sseReply, nullptr, this, nullptr);
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

void QtHttpSseWorker::applyCommonHeaders(QNetworkRequest& request) const {
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("MCP-Protocol-Version", m_protocolVersion.toUtf8());
    if (!m_sessionId.isEmpty()) {
        request.setRawHeader("MCP-Session-Id", m_sessionId.toUtf8());
    }
    if (!m_lastEventId.isEmpty()) {
        request.setRawHeader("Last-Event-ID", m_lastEventId.toUtf8());
    }
    const QString token = currentBearerToken();
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }
}

void QtHttpSseWorker::openSse() {
    if (m_stopping) {
        return;
    }
    QNetworkRequest request(QUrl(m_baseUrl));
    applyCommonHeaders(request);
    m_sseReply = m_network->get(request);

    connect(m_sseReply, &QIODevice::readyRead, this, &QtHttpSseWorker::handleSseReadyRead);
    connect(m_sseReply, &QNetworkReply::finished, this, &QtHttpSseWorker::handleSseFinished);
    connect(m_sseReply, &QNetworkReply::errorOccurred, this, &QtHttpSseWorker::handleSseError);
}

void QtHttpSseWorker::handleSseReadyRead() {
    if (!m_sseReply) {
        return;
    }
    const QByteArray chunk = m_sseReply->readAll();
    m_parser.pushChunk(chunk.toStdString());
}

void QtHttpSseWorker::handleSseFinished() {
    if (!m_sseReply) {
        return;
    }
    const auto sessionHeader = m_sseReply->rawHeader("MCP-Session-Id");
    if (!sessionHeader.isEmpty()) {
        m_sessionId = QString::fromUtf8(sessionHeader);
    }
    m_sseReply->deleteLater();
    m_sseReply = nullptr;
    if (!m_stopping) {
        scheduleReconnect();
    }
}

void QtHttpSseWorker::handleSseError(QNetworkReply::NetworkError) {
    if (!m_sseReply) {
        return;
    }
    emit transportError(m_sseReply->errorString());
}

void QtHttpSseWorker::handleSseEvent(const QtSseEvent& event) {
    if (!event.lastEventId.empty()) {
        m_lastEventId = QString::fromStdString(event.lastEventId);
    }
    if (event.eventName == "endpoint") {
        m_postUrl = QString::fromStdString(event.data);
        return;
    }
    emit messageReceived(QString::fromStdString(event.data));
}

void QtHttpSseWorker::scheduleReconnect() {
    if (!m_reconnectTimer->isActive()) {
        m_reconnectTimer->start(m_retryMs);
    }
}

bool QtHttpSseWorker::postMessage(const QString& payload) {
    if (m_stopping) {
        return false;
    }
    QNetworkRequest request(QUrl(m_postUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("MCP-Protocol-Version", m_protocolVersion.toUtf8());
    if (!m_sessionId.isEmpty()) {
        request.setRawHeader("MCP-Session-Id", m_sessionId.toUtf8());
    }
    const QString token = currentBearerToken();
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }

    QNetworkReply* reply = m_network->post(request, payload.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto sessionHeader = reply->rawHeader("MCP-Session-Id");
        if (!sessionHeader.isEmpty()) {
            m_sessionId = QString::fromUtf8(sessionHeader);
        }
        const QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            emit messageReceived(QString::fromUtf8(body));
        }
        reply->deleteLater();
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError) {
        emit transportError(reply->errorString());
    });

    return true;
}

} // namespace mcp_qt
```

- [ ] **Step 4: Build to confirm the public wrapper and worker line up**

Run:

```powershell
cmake --build build --target mcp_qt_transport --config Release
```

Expected: build succeeds with Qt meta-object code generated for the worker.

- [ ] **Step 5: Commit**

Run:

```powershell
git add mcp_qt_transport/src/QtHttpSseWorker.h mcp_qt_transport/src/QtHttpSseWorker.cpp mcp_qt_transport/src/QtHttpSseTransport.cpp
git commit -m "feat: add qt http sse worker core"
```

### Task 5: Add Qt transport tests and shutdown guarantees

**Files:**
- Create: `tests_qt/CMakeLists.txt`
- Create: `tests_qt/test_qt_http_transport.cpp`
- Modify: `tests_qt/test_qt_transport_shutdown.cpp`

- [ ] **Step 1: Create the Qt test target**

Create `tests_qt/CMakeLists.txt`:

```cmake
add_executable(tests_qt
    ${CMAKE_SOURCE_DIR}/tests/common.cpp
    ${CMAKE_SOURCE_DIR}/tests/main_standalone.cpp
    test_qt_sse_parser.cpp
    test_qt_http_transport.cpp
    test_qt_transport_shutdown.cpp
)

target_include_directories(tests_qt PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/mcp_qt_transport/include
)

target_link_libraries(tests_qt PRIVATE
    mcp_qt_transport
    Qt6::Core
    Qt6::Network
)
```

- [ ] **Step 2: Write the failing shutdown test**

Update `tests_qt/test_qt_transport_shutdown.cpp`:

```cpp
#include "mcp_qt_transport/QtHttpSseTransport.h"
#include "tests/common.h"

void test_qt_transport_shutdown_is_idempotent() {
    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>("https://example.test/mcp");
    int closeCount = 0;
    transport->setOnClose([&]() { ++closeCount; });

    TM_ASSERT_TRUE(transport->start(), "First start should succeed");
    transport->close();
    transport->close();

    TM_ASSERT_EQ(closeCount, 1, "close should emit onClose exactly once");
}
```

- [ ] **Step 3: Write the failing HTTP transport behavior test**

Create `tests_qt/test_qt_http_transport.cpp`:

```cpp
#include "mcp_qt_transport/QtHttpSseTransport.h"
#include "tests/common.h"

void test_qt_transport_rejects_send_before_start() {
    mcp_qt::QtHttpSseTransport transport("https://example.test/mcp");
    TM_ASSERT_FALSE(transport.send(R"({"jsonrpc":"2.0"})"), "send before start should fail");
}
```

- [ ] **Step 4: Build and run the Qt tests**

Run:

```powershell
cmake --build build --target tests_qt --config Release
./build/tests_qt/Release/tests_qt.exe
```

Expected: tests pass, with no hangs after shutdown.

- [ ] **Step 5: Commit**

Run:

```powershell
git add tests_qt/CMakeLists.txt tests_qt/test_qt_http_transport.cpp tests_qt/test_qt_transport_shutdown.cpp
git commit -m "test: add qt transport lifecycle coverage"
```

### Task 6: Add session-level integration coverage

**Files:**
- Create: `tests_qt/test_qt_session_integration.cpp`
- Modify: `tests_qt/CMakeLists.txt`

- [ ] **Step 1: Write the failing session-integration test**

Create `tests_qt/test_qt_session_integration.cpp`:

```cpp
#include "mcp_qt_transport/QtHttpSseTransport.h"
#include "mcp_core/McpClientSession.h"
#include "tests/common.h"

void test_qt_transport_keeps_core_session_api_shape() {
    auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>("https://example.test/mcp");
    auto session = std::make_shared<mcp::McpClientSession>(transport);
    session->init();

    TM_ASSERT_TRUE(session->state() == mcp::SessionState::Uninitialized, "Core session should accept Qt transport");
}
```

- [ ] **Step 2: Register the test executable input**

Update `tests_qt/CMakeLists.txt` to include:

```cmake
    test_qt_session_integration.cpp
```

- [ ] **Step 3: Build and run the Qt test suite again**

Run:

```powershell
cmake --build build --target tests_qt --config Release
./build/tests_qt/Release/tests_qt.exe
```

Expected: the session test passes and confirms `IMcpTransport` compatibility.

- [ ] **Step 4: Run the existing standalone core tests**

Run:

```powershell
cmake --build build --target mcp_core_tests --config Release
./build/tests/Release/mcp_core_tests.exe
```

Expected: all existing standalone tests still pass.

- [ ] **Step 5: Commit**

Run:

```powershell
git add tests_qt/test_qt_session_integration.cpp tests_qt/CMakeLists.txt
git commit -m "test: verify qt transport session compatibility"
```

### Task 7: Mark the old HTTP transport as legacy and update docs

**Files:**
- Modify: `mcp_core/include/mcp_core/HttpSseTransport.h`
- Modify: `README.md`

- [ ] **Step 1: Mark the old HTTP transport header honestly**

Update the class comment in `mcp_core/include/mcp_core/HttpSseTransport.h` to start like this:

```cpp
/**
 * @brief Legacy pure C++ HTTP/SSE transport for MCP (experimental on Windows HTTPS/SSE).
 *
 * Preferred usage:
 * - local MCP servers: SubprocessStdioTransport
 * - remote MCP servers inside Qt agents: mcp_qt::QtHttpSseTransport
 *
 * This transport remains available for compatibility, but is not the preferred
 * remote transport for Qt-based agents.
 */
```

- [ ] **Step 2: Update the README integration guidance**

Add a new section to `README.md`:

```md
## Transport Recommendation

- Local MCP server subprocesses: use `mcp::SubprocessStdioTransport`
- Remote HTTP/HTTPS MCP servers in Qt applications: use `mcp_qt::QtHttpSseTransport`
- `mcp::HttpSseTransport` remains available as a legacy/experimental pure C++ transport
```

Also add a short Qt example:

```cpp
#include <mcp_core/McpClientSession.h>
#include <mcp_qt_transport/QtHttpSseTransport.h>

auto transport = std::make_shared<mcp_qt::QtHttpSseTransport>("https://server.example.com/mcp");
auto session = mcp::McpClientSession::connect(transport);
```

- [ ] **Step 3: Build one last time after doc-facing changes**

Run:

```powershell
cmake --build build --target mcp_core_tests --config Release
cmake --build build --target tests_qt --config Release
```

Expected: both build targets still succeed.

- [ ] **Step 4: Commit**

Run:

```powershell
git add mcp_core/include/mcp_core/HttpSseTransport.h README.md
git commit -m "docs: recommend qt transport for remote agents"
```

### Task 8: Final verification pass

**Files:**
- Modify: `QT_TRANSPORT_IMPLEMENTATION_PLAN.md` only if execution exposed mismatches

- [ ] **Step 1: Run the full intended verification set**

Run:

```powershell
./build/tests/Release/mcp_core_tests.exe
./build/tests_qt/Release/tests_qt.exe
```

Expected:

```text
All Standalone Tests PASSED!
Qt transport tests complete with 0 failures
```

- [ ] **Step 2: Verify no Qt headers leaked into `mcp_core` public surface**

Run:

```powershell
rg -n "Q[A-Z]" mcp_core/include mcp_core/src
```

Expected: no new Qt symbol usage inside `mcp_core` beyond the unchanged legacy transport files.

- [ ] **Step 3: Verify there are no detached thread regressions**

Run:

```powershell
rg -n "detach\\(" mcp_qt_transport tests_qt
```

Expected: no matches.

- [ ] **Step 4: Commit any final fixes**

Run:

```powershell
git add mcp_qt_transport tests_qt CMakeLists.txt mcp_core README.md
git commit -m "chore: finalize qt transport integration"
```

## Self-Review

- Spec coverage: this plan covers build split, public adapter, worker-thread model, SSE parser, tests, legacy labeling, and final verification.
- Placeholder scan: no `TODO`/`TBD` placeholders remain; every task references exact files and commands.
- Type consistency: the plan consistently uses `mcp_qt::QtHttpSseTransport`, `QtHttpSseWorker`, and `QtSseParser`.

## Execution Handoff

Plan complete and saved to `QT_TRANSPORT_IMPLEMENTATION_PLAN.md`. Two execution options:

**1. Subagent-Driven (recommended)** - dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - execute tasks in one session using executing-plans, batch execution with checkpoints

Choose one before implementation starts.
