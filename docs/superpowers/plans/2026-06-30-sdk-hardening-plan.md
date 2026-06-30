# MCP SDK Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the six confirmed commercialization gaps in the SDK: resilience/recovery, typed tool results, resource subscription routing, advanced HTTP auth/proxy configuration, traffic tracing, and Qt Model/View integration.

**Architecture:** Keep the current layering (`mcp_core` -> `mcp_qt_transport` -> `mcp_qt_client`) and land the work as additive APIs first. Build shared recovery and tracing primitives in `mcp_core`, expose transport-specific HTTP knobs in `mcp_qt_transport`, then add Qt-facing wrappers and models in `mcp_qt_client`. Do not break the existing `QJsonObject`-based API in the first pass; deprecate it only after the typed wrappers are proven by tests.

**Tech Stack:** C++17, Qt 6 (`QObject`, `QAbstractListModel`, `QNetworkAccessManager`, `QNetworkRequest`, `QNetworkProxy`), `nlohmann::json`, existing `McpClientSession` / `IMcpTransport` / `QtHttpSseTransport` / `McpQtClient`.

---

## Scope and Guardrails

- This plan covers **one cohesive hardening project**, but it should be implemented as **6 reviewable PR-sized tasks**.
- Preserve source compatibility where practical:
  - Keep `McpQtClient::callTool()` returning `McpResult` during the migration.
  - Add typed result helpers alongside the current raw JSON return path.
  - Keep `subscribeResource(uri)` working even after callback-based routing is added.
- Do not add unrelated refactors.
- Do not assume HTTP-only: Stdio transports need explicit recovery behavior too.
- Follow repository constraints:
  - Use `pwsh.exe -NoLogo -Command` for shell commands.
  - Do not run any command that merges pre-existing staged and unstaged changes into the index.

## Current-State Snapshot

These facts are already validated in the codebase and should be treated as the starting point:

- `McpClientSession` releases all pending requests and enters `Shutdown` when transport close fires; there is no session-level replay or reinitialize path.
- `QtHttpSseWorker` already supports `retry:` parsing, `Last-Event-ID`, bearer token injection, and 401/403 auth retry, but the behavior stops at transport continuity.
- `QtProcessStdioTransport` and `SubprocessStdioTransport` surface crashes as close/error only; they do not auto-restart the server process.
- `McpQtClient::callTool()` still returns raw `QJsonObject`.
- `subscribeResource()` only sends `resources/subscribe`; notification routing remains the caller's responsibility.
- There is no `QAbstractListModel` adapter for tools.
- `McpClientSession::setLogCallback()` exists, but it is not a full bidirectional raw-traffic logger.

## File Map

### Existing files that will definitely change

- `mcp_core/include/mcp_core/McpClientSession.h`
- `mcp_core/src/McpClientSession.cpp`
- `mcp_core/include/mcp_core/IMcpTransport.h`
- `mcp_core/include/mcp_core/HttpSseTransport.h`
- `mcp_core/src/HttpSseTransport.cpp`
- `mcp_core/include/mcp_core/SubprocessStdioTransport.h`
- `mcp_core/src/SubprocessStdioTransport.cpp`
- `mcp_qt_transport/include/mcp_qt_transport/QtHttpSseTransport.h`
- `mcp_qt_transport/src/QtHttpSseTransport.cpp`
- `mcp_qt_transport/src/QtHttpSseWorker.h`
- `mcp_qt_transport/src/QtHttpSseWorker.cpp`
- `mcp_qt_transport/include/mcp_qt_transport/QtProcessStdioTransport.h`
- `mcp_qt_transport/src/QtProcessStdioTransport.cpp`
- `mcp_qt_client/include/mcp_qt_client/McpQtClient.h`
- `mcp_qt_client/src/McpQtClient.cpp`
- `README.md`
- `tests/CMakeLists.txt`
- `tests_qt/CMakeLists.txt`

### New files recommended in this plan

- `mcp_core/include/mcp_core/McpTrafficEvent.h`
- `mcp_core/include/mcp_core/McpReconnectPolicy.h`
- `mcp_qt_client/include/mcp_qt_client/McpQtToolResult.h`
- `mcp_qt_client/include/mcp_qt_client/McpResourceSubscriptionRouter.h`
- `mcp_qt_client/include/mcp_qt_client/McpToolsModel.h`
- `mcp_qt_client/src/McpToolsModel.cpp`
- `tests/features/test_recovery.cpp`
- `tests/features/test_traffic_logging.cpp`
- `tests/features/test_typed_results.cpp`
- `tests_qt/test_qt_resource_router.cpp`
- `tests_qt/test_qt_tools_model.cpp`
- `tests_qt/test_qt_transport_recovery.cpp`
- `tests_qt/test_qt_http_request_config.cpp`

If file count must be minimized, `McpResourceSubscriptionRouter` can be folded into `McpQtClient`, but keeping it separate will make recovery and GUI refresh logic easier to reason about.

## Recommended Delivery Order

1. Add traffic tracing first so later failures are diagnosable.
2. Add HTTP request customization second so auth/proxy work can be tested independently.
3. Add typed results third; this is additive and low-risk.
4. Add resource subscription routing fourth; resilience will depend on a canonical subscription registry.
5. Add `McpToolsModel` fifth; it depends on routed notifications and cached tool refreshes.
6. Add reconnection/state recovery last; it is the highest-value but most cross-cutting change.

## Baseline Verification Commands

- Configure core-only build:

```powershell
pwsh.exe -NoLogo -Command "cmake -S . -B build -DMCP_ENABLE_HTTP=ON"
```

- Configure Qt build:

```powershell
pwsh.exe -NoLogo -Command "cmake -S . -B build_qt -DMCP_ENABLE_HTTP=ON -DMCP_ENABLE_QT_TRANSPORT=ON"
```

- Build current test targets:

```powershell
pwsh.exe -NoLogo -Command "cmake --build build --target mcp_core_tests"
pwsh.exe -NoLogo -Command "cmake --build build_qt --target tests_qt"
```

- Run current test targets with generator-agnostic PowerShell:

```powershell
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build -Filter mcp_core_tests.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build_qt -Filter tests_qt.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
```

Record the baseline before modifying behavior.

---

### Task 1: Full Raw Traffic Tracing

**Files:**
- Create: `mcp_core/include/mcp_core/McpTrafficEvent.h`
- Modify: `mcp_core/include/mcp_core/McpClientSession.h`
- Modify: `mcp_core/src/McpClientSession.cpp`
- Modify: `mcp_qt_client/include/mcp_qt_client/McpQtClient.h`
- Modify: `mcp_qt_client/src/McpQtClient.cpp`
- Test: `tests/features/test_traffic_logging.cpp`

- [ ] **Step 1: Add failing core traffic tests**

Add tests that assert:

```cpp
// Outbound request should emit the full JSON-RPC envelope before transport->send().
// Inbound response/notification should emit the original parsed envelope after receipt.
// Logger should distinguish request / response / notification / direction.
```

- [ ] **Step 2: Introduce a stable traffic event type**

Add a small value type in `McpTrafficEvent.h`:

```cpp
enum class McpTrafficDirection { Outbound, Inbound };
enum class McpTrafficKind { Request, Response, Notification, Unknown };

struct McpTrafficEvent {
    McpTrafficDirection direction{McpTrafficDirection::Outbound};
    McpTrafficKind kind{McpTrafficKind::Unknown};
    nlohmann::json payload;
};
```

- [ ] **Step 3: Extend `McpClientSession` with a traffic callback**

Add additive APIs similar to:

```cpp
using TrafficCallback = std::function<void(const McpTrafficEvent&)>;
void setTrafficCallback(TrafficCallback callback);
```

Emit traffic events in:

- `sendRequest()`
- `sendNotification()`
- `handleIncomingMessage()`

Do not emit partially-built payloads; log the exact envelope that is sent or received.

- [ ] **Step 4: Add Qt wrapper surface**

Expose a Qt-facing wrapper in `McpQtClient`:

```cpp
using TrafficLogger = std::function<void(const QJsonObject& event)>;
void setTrafficLogger(TrafficLogger logger);
```

Map `McpTrafficEvent` into a `QJsonObject` with keys:

- `direction`
- `kind`
- `payload`

- [ ] **Step 5: Verify tests**

Run:

```powershell
pwsh.exe -NoLogo -Command "cmake --build build --target mcp_core_tests"
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build -Filter mcp_core_tests.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
```

Expected:

- new traffic test passes
- existing notification and protocol tests still pass

**Acceptance notes:**

- The logger must see complete JSON-RPC envelopes, not only method names.
- Logging must be passive; it must not change request timing or callback ownership.

---

### Task 2: Advanced HTTP Auth, Header, and Proxy Configuration

**Files:**
- Create: `mcp_core/include/mcp_core/McpReconnectPolicy.h`
- Modify: `mcp_qt_transport/include/mcp_qt_transport/QtHttpSseTransport.h`
- Modify: `mcp_qt_transport/src/QtHttpSseTransport.cpp`
- Modify: `mcp_qt_transport/src/QtHttpSseWorker.h`
- Modify: `mcp_qt_transport/src/QtHttpSseWorker.cpp`
- Modify: `mcp_qt_client/include/mcp_qt_client/McpQtClient.h`
- Modify: `mcp_qt_client/src/McpQtClient.cpp`
- Test: `tests_qt/test_qt_http_request_config.cpp`

- [ ] **Step 1: Add failing Qt request-configuration tests**

Cover these behaviors:

```cpp
// Custom headers must be present on both SSE GET and JSON POST.
// Runtime bearer token must still override or coexist predictably.
// Proxy settings must be applied to the worker-owned QNetworkAccessManager.
```

- [ ] **Step 2: Add a transport configuration object**

Prefer one additive struct over a growing list of setters:

```cpp
struct QtHttpRequestConfig {
    QMap<QByteArray, QByteArray> defaultHeaders;
    std::optional<QNetworkProxy> proxy;
    bool allowAuthorizationOverride{true};
};
```

Expose it via:

```cpp
void setRequestConfig(const QtHttpRequestConfig& config);
QtHttpRequestConfig requestConfig() const;
```

- [ ] **Step 3: Apply config in the worker**

Update `QtHttpSseWorker::applyCommonHeaders()` and `postMessage()` so:

- default headers are copied first
- protocol/session headers are enforced after defaults
- bearer token injection is deterministic
- proxy is applied to `QNetworkAccessManager`

Do not lose current OAuth retry behavior.

- [ ] **Step 4: Add builder-level entry points**

Extend `McpQtClientBuilder` and `McpQtClient::connectHttp()` with additive overloads:

```cpp
McpQtClientBuilder& setHttpHeaders(const QMap<QString, QString>& headers);
McpQtClientBuilder& setHttpProxy(const QNetworkProxy& proxy);
```

Internally convert these to the transport config object.

- [ ] **Step 5: Verify tests**

Run:

```powershell
pwsh.exe -NoLogo -Command "cmake --build build_qt --target tests_qt"
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build_qt -Filter tests_qt.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
```

Expected:

- auth retry tests still pass
- new header/proxy tests pass

**Acceptance notes:**

- Do not expose `QNetworkRequest` directly in the first pass unless needed by tests.
- Header configuration must affect both SSE and POST paths.

---

### Task 3: Typed Tool Result Model

**Files:**
- Create: `mcp_qt_client/include/mcp_qt_client/McpQtToolResult.h`
- Modify: `mcp_qt_client/include/mcp_qt_client/McpQtClient.h`
- Modify: `mcp_qt_client/src/McpQtClient.cpp`
- Modify: `README.md`
- Test: `tests/features/test_typed_results.cpp`

- [ ] **Step 1: Add failing typed-result tests**

Cover at least:

```cpp
// Text content array -> typed text items.
// Image content with base64 data -> typed image item with decoded bytes.
// Unknown content types -> preserved as opaque JSON instead of dropped.
```

- [ ] **Step 2: Define additive typed wrappers**

Prefer a lightweight wrapper set such as:

```cpp
enum class McpQtContentKind { Text, Image, Resource, Unknown };

struct McpQtContent {
    McpQtContentKind kind{McpQtContentKind::Unknown};
    QString mimeType;
    QString text;
    QByteArray binary;
    QJsonObject raw;
};

struct McpQtToolResult {
    QList<McpQtContent> content;
    QJsonObject structuredContent;
    QJsonObject raw;
    bool isError{false};
    QString errorString;
};
```

- [ ] **Step 3: Implement parser helpers**

Add helper functions in `McpQtClient.cpp` to parse MCP `content` arrays:

- `type == "text"` -> `text`
- `type == "image"` -> `data` + `mimeType`
- embedded resource payloads -> `raw`
- unknown content -> preserve `raw`

Do not delete the existing `McpResult` path yet.

- [ ] **Step 4: Add additive APIs**

Add methods such as:

```cpp
McpQtToolResult callToolTyped(const QString& name, const QJsonObject& arguments, int timeoutMs = 10000);
void callToolTypedAsync(...);
```

Keep these implemented on top of existing request flow so there is one JSON-RPC execution path.

- [ ] **Step 5: Update docs and verify**

Document both paths in `README.md`:

- `callTool()` for raw JSON
- `callToolTyped()` for higher-level consumption

Run:

```powershell
pwsh.exe -NoLogo -Command "cmake --build build --target mcp_core_tests"
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build -Filter mcp_core_tests.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
```

**Acceptance notes:**

- Typed wrappers must never discard the original raw result.
- Base64 decode failures should surface as typed-result errors, not silent empty data.

---

### Task 4: Resource Subscription Routing

**Files:**
- Create: `mcp_qt_client/include/mcp_qt_client/McpResourceSubscriptionRouter.h`
- Modify: `mcp_qt_client/include/mcp_qt_client/McpQtClient.h`
- Modify: `mcp_qt_client/src/McpQtClient.cpp`
- Modify: `tests/features/test_notifications.cpp`
- Test: `tests_qt/test_qt_resource_router.cpp`

- [ ] **Step 1: Add failing router tests**

Test these cases:

```cpp
// subscribeResource(uri, callback) receives only matching URI updates.
// Multiple subscribers for same URI all fire.
// unsubscribe removes callback and stops delivery.
// Unknown URI notifications still reach generic notification handlers.
```

- [ ] **Step 2: Create a dedicated router abstraction**

Prefer a private helper or separate class that owns:

- subscribed URI set
- callback lists per URI
- fan-out logic
- optional payload refresh behavior

Suggested Qt API:

```cpp
using ResourceUpdateCallback = std::function<void(const QString& uri, const QJsonObject& payload)>;
bool subscribeResource(const QString& uri, ResourceUpdateCallback callback, int timeoutMs = 10000);
bool unsubscribeResource(const QString& uri, QObject* context = nullptr, int timeoutMs = 10000);
```

- [ ] **Step 3: Hook routing into notification ingress**

Intercept `notifications/resources/updated` in `McpQtClient` before or alongside the existing generic emission:

- inspect `params["uri"]`
- find matching subscribers
- deliver callback on the receiver thread when a context object is present

Do not remove `registerNotificationHandler()`.

- [ ] **Step 4: Decide payload strategy explicitly**

Choose one behavior and document it in code comments and README:

1. **Notify only** with the raw update payload.
2. **Notify + lazy read helper**.
3. **Notify + eager `readResource()` refresh**.

Recommended first pass: option 1, because it is deterministic and avoids hidden IO.

- [ ] **Step 5: Verify tests**

Run:

```powershell
pwsh.exe -NoLogo -Command "cmake --build build_qt --target tests_qt"
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build_qt -Filter tests_qt.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
```

**Acceptance notes:**

- Callback-based subscription routing must be additive.
- Generic notification APIs must still see the original raw notification.

---

### Task 5: Qt Tools Model / View Binding

**Files:**
- Create: `mcp_qt_client/include/mcp_qt_client/McpToolsModel.h`
- Create: `mcp_qt_client/src/McpToolsModel.cpp`
- Modify: `mcp_qt_client/CMakeLists.txt`
- Modify: `mcp_qt_client/include/mcp_qt_client/McpQtClient.h`
- Modify: `mcp_qt_client/src/McpQtClient.cpp`
- Test: `tests_qt/test_qt_tools_model.cpp`

- [ ] **Step 1: Add failing model tests**

Cover:

```cpp
// Initial refresh populates rows from tools/list.
// Role data exposes name/description/schema.
// Model refresh emits proper begin/end reset or row updates.
// Repeated refresh with identical payload avoids unnecessary churn if possible.
```

- [ ] **Step 2: Implement a narrow `QAbstractListModel`**

Provide roles:

```cpp
enum Roles {
    NameRole = Qt::UserRole + 1,
    DescriptionRole,
    InputSchemaRole
};
```

Expose:

```cpp
void setClient(McpQtClient* client);
Q_INVOKABLE void refresh();
```

Store plain `McpQtTool` rows internally.

- [ ] **Step 3: Add list-changed integration hook**

The codebase does not yet expose tool list change notifications. Implement one of these:

1. add support for `notifications/tools/list-changed` if the server sends it
2. otherwise expose an explicit `refreshToolsModel()` path and document the limitation

Recommended first pass: support both; auto-refresh when the notification exists, manual refresh otherwise.

- [ ] **Step 4: Add client-side helper**

Add an additive helper in `McpQtClient`:

```cpp
std::unique_ptr<McpToolsModel> createToolsModel(QObject* parent = nullptr);
```

This keeps GUI users from wiring boilerplate manually.

- [ ] **Step 5: Verify tests**

Run:

```powershell
pwsh.exe -NoLogo -Command "cmake --build build_qt --target tests_qt"
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build_qt -Filter tests_qt.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
```

**Acceptance notes:**

- Avoid hidden background polling.
- Keep the model reusable in Widgets and QML.

---

### Task 6: Recovery, Backoff, Reinitialize, and Request Replay

**Files:**
- Modify: `mcp_core/include/mcp_core/IMcpTransport.h`
- Modify: `mcp_core/include/mcp_core/McpClientSession.h`
- Modify: `mcp_core/src/McpClientSession.cpp`
- Modify: `mcp_core/include/mcp_core/HttpSseTransport.h`
- Modify: `mcp_core/src/HttpSseTransport.cpp`
- Modify: `mcp_core/include/mcp_core/SubprocessStdioTransport.h`
- Modify: `mcp_core/src/SubprocessStdioTransport.cpp`
- Modify: `mcp_qt_transport/include/mcp_qt_transport/QtHttpSseTransport.h`
- Modify: `mcp_qt_transport/src/QtHttpSseTransport.cpp`
- Modify: `mcp_qt_transport/include/mcp_qt_transport/QtProcessStdioTransport.h`
- Modify: `mcp_qt_transport/src/QtProcessStdioTransport.cpp`
- Modify: `mcp_qt_client/include/mcp_qt_client/McpQtClient.h`
- Modify: `mcp_qt_client/src/McpQtClient.cpp`
- Test: `tests/features/test_recovery.cpp`
- Test: `tests_qt/test_qt_transport_recovery.cpp`

- [ ] **Step 1: Add failing recovery tests before changing state machines**

Cover these scenarios separately:

```cpp
// SSE transient disconnect: transport reconnects, session remains usable.
// Stdio child crash: transport restarts with backoff, session reinitializes.
// After recovery, tools/list is refreshed automatically.
// Registered resource subscriptions are resubscribed automatically.
// Replayable pending requests complete after recovery instead of failing immediately.
```

Do not try to solve all scenarios in one first test; write 2-3 focused tests.

- [ ] **Step 2: Add an explicit reconnect policy**

Introduce a small reusable policy:

```cpp
struct McpReconnectPolicy {
    bool enabled{true};
    int initialDelayMs{250};
    int maxDelayMs{5000};
    double multiplier{2.0};
    int maxAttempts{-1}; // -1 = unlimited
};
```

Apply it to both:

- `SubprocessStdioTransport`
- `QtProcessStdioTransport`

Keep HTTP transport reuse separate; it already has internal SSE reconnect behavior.

- [ ] **Step 3: Extend transport/session contracts for recoverability**

Current `onClose` means terminal shutdown. Add enough semantics to distinguish:

- transient disconnect / recoverable
- terminal close / unrecoverable

Do this with the smallest viable contract change. Two acceptable options:

1. extend `IMcpTransport` with a richer close event type
2. keep `IMcpTransport` stable and add recovery orchestration in `McpQtClient`

Recommended approach: option 2 for the first pass, because it avoids breaking all transports at once.

- [ ] **Step 4: Add session rehydrate hooks**

`McpClientSession` needs additive helpers to support client-driven recovery:

```cpp
bool reinitializeSync(...);
void restoreCapabilities(...);
void restoreNotificationHandlers(...); // if needed
```

Also track replayable requests explicitly. Only replay methods that are safe and idempotent in this SDK:

- `tools/list`
- `resources/list`
- `resources/templates/list`
- `prompts/list`
- `resources/subscribe`

Do **not** auto-replay arbitrary `tools/call` in the first pass unless the request is explicitly marked replayable.

- [ ] **Step 5: Implement client-level recovery orchestration**

In `McpQtClient`, add:

- cached initialize inputs (`clientName`, `clientVersion`, timeout)
- subscription registry
- optional auto-reconnect enable flag
- reconnect backoff timer

Recovery flow:

1. detect terminal transport loss
2. rebuild or restart transport
3. run `initialize`
4. refresh `tools/list`
5. re-subscribe active resources
6. resolve queued replayable requests

Emit high-signal events during this flow:

- `reconnecting()`
- `reconnected()`
- `recoveryFailed(QString)`

- [ ] **Step 6: Verify tests and conformance-sensitive behavior**

Run:

```powershell
pwsh.exe -NoLogo -Command "cmake --build build --target mcp_core_tests"
pwsh.exe -NoLogo -Command "cmake --build build_qt --target tests_qt"
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build -Filter mcp_core_tests.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
pwsh.exe -NoLogo -Command "$exe = Get-ChildItem -Recurse build_qt -Filter tests_qt.exe | Select-Object -First 1 -ExpandProperty FullName; & $exe"
```

Then re-run the Qt conformance runner if available:

```powershell
pwsh.exe -NoLogo -Command "cmake --build build_qt --target conformance_runner_qt"
```

**Acceptance notes:**

- Do not silently replay non-idempotent tool calls.
- A failed recovery attempt must surface clearly instead of leaving the client half-initialized.

---

## Cross-Cutting Review Checklist

Before marking the implementation complete, verify all of the following:

- `README.md` documents every new public API.
- Old raw APIs still work.
- New tests fail before implementation and pass after.
- Traffic logger covers request, response, and notification directions.
- Custom headers/proxy apply to both SSE GET and POST.
- Typed result parsing preserves raw payloads.
- Resource routing still allows generic notification handling.
- `McpToolsModel` works without polling.
- Recovery never auto-replays arbitrary `tools/call`.

## Suggested PR Boundaries

- PR 1: traffic tracing
- PR 2: HTTP header/proxy config
- PR 3: typed tool results
- PR 4: resource routing
- PR 5: tools model
- PR 6: recovery and replay

This split keeps each review scoped and lets regressions be bisected quickly.

## Open Decisions To Resolve During Implementation

- Whether proxy support needs to be available in pure C++ `HttpSseTransport` in this phase, or Qt-only is sufficient.
- Whether typed content models belong in `mcp_core` for reuse, or `mcp_qt_client` is enough for the first pass.
- Whether resource update callbacks should eagerly read resource content or only surface notification payloads.
- Whether tool-list refresh should rely on a future `notifications/tools/list-changed` contract or stay explicit when unsupported.
- Whether transport recoverability should be represented in `IMcpTransport` now or deferred to a higher-level orchestrator.

Pick one answer per item and make it explicit in code and docs. Do not leave ambiguous behavior for later.

## Final Handoff

Plan saved to `docs/superpowers/plans/2026-06-30-sdk-hardening-plan.md`.

Recommended execution mode: **Subagent-Driven**, because the six tasks are loosely coupled but still benefit from review checkpoints between phases.
