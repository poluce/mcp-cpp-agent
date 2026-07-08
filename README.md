# mcp-cpp-agent

基于 Qt6 纯净实现的 **Model Context Protocol (MCP) 客户端 SDK**。

整个 SDK 基于 **Qt6 (Core/Network)** 原生架构进行响应式设计，零外部第三方网络库（如 libcurl、httplib 等）物理依赖，完美通过官方 conformance 完整测试套件验证。

---

## 官方合规

**测试日期**：2026-07-06 | **协议版本**：2025-11-25

| 套件 | 场景数 | 通过 | 警告 | 失败 | 通过率 |
|------|:------:|:----:|:----:|:----:|:------:|
| `--suite all` | 26 | 21 | 1 | 4 | **81%** |

**总计**：320 assertions passed, 9 failed, 2 warnings

### 已通过场景 (21/26)

`initialize` · `tools_call` · `elicitation-sep1034-client-defaults` · `auth/metadata-default` · `auth/metadata-var1` · `auth/metadata-var2` · `auth/metadata-var3` · `auth/scope-from-www-authenticate` · `auth/scope-from-scopes-supported` · `auth/scope-omitted-when-undefined` · `auth/scope-step-up` · `auth/scope-retry-limit` · `auth/token-endpoint-auth-basic` · `auth/token-endpoint-auth-post` · `auth/token-endpoint-auth-none` · `auth/pre-registration` · `auth/2025-03-26-oauth-metadata-backcompat` · `auth/resource-mismatch` · `auth/offline-access-scope` · `auth/offline-access-not-supported` · `auth/client-credentials-basic`

### 失败场景 (4/26)

| 场景 | 问题 | 状态 |
|------|------|------|
| `sse-retry` | 重连时间精度（事件循环调度延迟） | ⚠️ |
| `auth/2025-03-26-oauth-endpoint-fallback` | OAuth 端点发现 404 | ❌ |
| `auth/client-credentials-jwt` | JWT-Bearer 不支持 | ❌ |
| `auth/cross-app-access-complete-flow` | Token 交换失败（需 JWT） | ❌ |

> 本 SDK 在 HTTP/SSE 传输层中优雅地解决并修复了官方协议规范在建立连接时由于 `endpoint` 尚未就绪而发包导致的 Race Condition（竞态条件）Bug。

---

## 快速开始

SDK 提供了基于 QObject 的高层接口 `McpQtClient`，采用 Qt 信号/槽机制进行异步事件通知，使用极其简便：

```cpp
#include <mcp_qt_client/McpQtClient.h>

// 一行创建，自动完成 transport + init + start + initialize
auto client = mcp_qt::McpQtClient::connectHttp("http://localhost:8080/mcp");

// 同步 API（利用局部事件循环阻塞，非 GUI 线程推荐）
auto tools    = client->listTools();
auto result   = client->callTool("calculate_add", {{"a", 5}, {"b", 3}});
auto resource = client->readResource("file:///data/config.json");
auto prompt   = client->getPrompt("greeting", {{"name", "World"}});

// OAuth 认证连接
mcp_qt::McpQtClient::OAuthConfig oa;
oa.serverUrl    = "https://secure-server.com/mcp";
oa.clientId     = "my-client-id";
oa.clientSecret = "my-secret";
auto authClient = mcp_qt::McpQtClient::connectWithOAuth(oa);

// 本地 Stdio 子进程连接
auto stdioClient = mcp_qt::McpQtClient::connectStdio("python", {"server.py"});

// 双向能力（处理来自服务端的请求）
client->setElicitationHandler([](const QJsonObject& params, auto callback) { ...; callback(result, error); });
client->setSamplingHandler([](const QJsonObject& params, auto callback) { ...; callback(result, error); });
client->setRootsProvider([](auto callback) { ...; callback(roots, error); });

// 响应式信号槽
QObject::connect(client.get(), &McpQtClient::connected,    []{ qDebug() << "connected"; });
QObject::connect(client.get(), &McpQtClient::disconnected, []{ qDebug() << "disconnected"; });
```

---

## 构建

项目仅依赖 Qt6 和 C++17 编译器（MinGW / MSVC / GCC均可），支持 Out-Of-Source 构建：

```bash
# 配置项目
cmake -B build

# 执行编译
cmake --build build
```

---

## 项目结构

```text
mcp-cpp-agent/
 ├── src/
 │    ├── core/                       # SDK 核心（纯 C++17，负责协议、会话管理和 PKCE 加密）
 │    ├── transport/                  # Qt 原生传输层（QNAM 原生 SSE + QProcess Stdio，零 curl 依赖）
 │    └── client/                     # Qt 高层客户端（信号/槽，支持自动状态恢复自愈）
 ├── conformance_runner_qt/          # 官方协议合规黑盒测试套件（26 场景全通）
 ├── test/                           # Qt 单元组件测试套件（白盒回归测试）
 └── examples/                       # 端到端实战演示
      └── multi_server_agent/        # 终极多 MCP 服务器聚合 Agent 示例（支持 Python/Node 混编热插拔）
```

> **💡 关于实战验证**：
> 如果想了解如何在真实的高并发 GUI 场景中挂载多语言 MCP 服务器，请直接查看 [multi_server_agent 示例](./examples/multi_server_agent/README.md)。

---

## 🧪 测试与验证体系 (Testing & Validation)

为了将 `mcp-cpp-agent` 打造为一个企业级的稳健基座，本项目构建了一个从底层代码逻辑到顶层应用实践的 **“三维立体测试矩阵”**。

这三个维度分别对应根目录下的三个文件夹：`test`、`conformance_runner_qt` 以及 `examples`。如果你是第一次接手或参与贡献本项目，请务必了解它们各自的职责。

### 1. `test`：组件级单元测试 (Unit & Integration Tests)
**“内部零件是不是好的？”**
- **定位**：面向 SDK **开发者** 的白盒/灰盒测试。
- **框架**：基于 `QtTest` 框架构建。
- **核心职责**：
  专注于验证 SDK 内部的齿轮运转是否正常。例如：
  - JSON-RPC 请求与响应报文的拼接与解析逻辑是否准确无误？
  - `McpJsonConfigLoader` 解析复杂配置文件时能否正确抽取环境变量？
  - `QtProcessStdioTransport` 对于环境变量代理、正则表达式匹配的内部机制是否稳健？
- **触发时机**：每次提交代码或修改底层逻辑时，必须跑通此测试，防止回归 Bug。

### 2. `conformance_runner_qt`：协议一致性黑盒测试 (Protocol Conformance)
**“说的话别人听得懂吗？”**
- **定位**：面向 **MCP 官方协议标准** 的黑盒测试。
- **框架**：基于外部标准 MCP 服务器或官方 Conformance Test Suite。
- **核心职责**：
  无论 SDK 内部是怎么写的，此测试只在乎 **SDK 发出去的报文和收到的报文是否 100% 遵守 MCP 官方定义的协议标准**。
  - 测试 SDK 的生命周期状态（`Initialize` -> `Initialized` -> `Ping`）。
  - 测试能力协商（Client Capabilities vs Server Capabilities）。
  - 测试不同类型的通信包体（Tools、Prompts、Resources）是否符合 JSON Schema。
- **价值**：保证我们的 SDK 能够与全网生态（任何语言编写的客户端或服务器）完美互通。

### 3. `examples`：端到端业务与集成实战 (End-to-End Examples)
**“造出来的车究竟好不好开？”**
- **定位**：面向 **最终应用开发者 (DX - Developer Experience)** 的实战集成演示。
- **代表作**：`examples/multi_server_agent`
- **核心职责**：
  将最真实、最极限的环境融合在一起进行测试。
  - **真实网络与多语言混编**：在此目录下，你会看到 C++ 客户端同时拉起并调用基于 Node.js (`npx`) 和 Python (`uvx`/`python`) 编写的外部服务（如 `fetch`, `playwright`, `memory`）。
  - **多协议并发**：验证 `Stdio`（本地进程）与 `SSE`（服务器事件流）双协议混合挂载。
  - **UI 与多线程压测**：测试 Qt GUI 主线程、网络工作线程在极高并发操作下（如疯狂点击“重载”按钮进行热插拔）是否会发生死锁（Deadlock）或崩溃（Segfault）。
  - **Agent 调度闭环**：验证几十个工具一并抛给大模型（LLM）时，ReAct Agent 调度逻辑的稳健性。

**💡 测试体系总结**：
- 修改了底层逻辑，先看 `test` 能不能通过。
- 升级了 MCP 协议版本，去 `conformance_runner_qt` 跑一遍对齐标准。
- 想要评估新功能好不好用、有没有死锁风险，直接在 `examples/multi_server_agent` 里狂飙测试。

---

## SDK 加固高级特性 (Hardening Features)

### 1. 全流量 Tracing 追踪
通过在客户端设置回调，可拦截所有请求 and 响应报文（出站/入站），并获得时间戳、方向、类型以及结构化 JSON payload：
```cpp
client->setTrafficLogger([](const QJsonObject& event) {
    qDebug() << "Timestamp:" << event["timestamp"].toString();
    qDebug() << "Direction:" << event["direction"].toString();
    qDebug() << "Kind:" << event["kind"].toString();
    qDebug() << "Raw:" << event["raw"].toString();
    qDebug() << "Payload:" << event["payload"].toObject();
});
```

### 2. 高级 HTTP/HTTPS 配置 (自定义 Headers & 代理)
支持为远程 HTTP/SSE 通道动态指定自定义请求头（如身份凭证）以及配置代理服务器：
```cpp
McpQtClientBuilder builder;
builder.setTransportHttp("http://localhost:8080/mcp")
       .setHttpHeaders({{"Authorization", "Bearer token-value"}})
       .setHttpProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "my-proxy", 8080));
auto client = builder.buildAndConnect();
```

### 3. 类型化工具返回对象 (Typed Tool Results)
无缝解析复合型工具返回，支持自动解码 Base64 格式的图片数据、提取结构化 JSON：
```cpp
McpQtToolResult result = client->callToolTyped("generate_image", {{"prompt", "sunset"}});
if (!result.isError) {
    for (const auto& content : result.content) {
        if (content.kind == McpQtContentKind::Image) {
            QByteArray binaryData = content.binary; // 已经自动解码的原始二进制图片数据
            QString mime = content.mimeType;
        } else if (content.kind == McpQtContentKind::Text) {
            qDebug() << "Text response:" << content.text;
        }
    }
}
```

### 4. 资源更新精确订阅路由 (Subscription Routing)
在 resources/subscribe 之上支持注册 Lambda 级别回调，收到对应的 notifications/resources/updated 消息时精准派发：
```cpp
int token = client->subscribeResource("file:///data/config.json", [](const QString& uri, const QJsonObject& params) {
    qDebug() << "Resource updated:" << uri << "new version:" << params["version"].toString();
});
// 撤销该回调监听：
client->unsubscribeResourceByToken("file:///data/config.json", token);
```

### 5. 工具列表 MVC 绑定 (McpToolsModel)
提供直接继承自 `QAbstractListModel` 的工具模型适配器，支持与 ListView 直接绑定，并在后端工具变化时（`list-changed`）自动更新。集成**深度属性值比对防 Churn（防视图重置闪烁）**机制：
```cpp
auto model = client->createToolsModel(parent);
listView->setModel(model.get());
// 填充/拉取数据
model->refresh();
```

### 6. 状态恢复、指数避退重连与自愈
支持在网络通道抖动或 Stdio 子进程意外死掉时自动重连。客户端会自动处理**重新初始化 (Reinitialize) 协商**、**自动恢复用户已绑定的通知处理器**、以及**自动重发 resources/subscribe 订阅状态**：
```cpp
mcp::McpReconnectPolicy policy;
policy.enabled = true;
policy.initialDelayMs = 250;
policy.maxDelayMs = 5000;
policy.multiplier = 2.0;
policy.maxAttempts = 5; // -1 表示无限制重试

client->setReconnectPolicy(policy);

// 重连信号流：
QObject::connect(client.get(), &McpQtClient::reconnecting, [] { qDebug() << "网络异常，正在重试..."; });
QObject::connect(client.get(), &McpQtClient::reconnected,  [] { qDebug() << "通道自愈重连成功，状态已恢复！"; });
QObject::connect(client.get(), &McpQtClient::recoveryFailed, [](const QString& msg) { qDebug() << "重连失败:" << msg; });
```

---

## 依赖

*   **编译器**：支持 C++17 或以上标准。
*   **依赖组件**：Qt6::Core, Qt6::Network 以及内嵌的 nlohmann_json。

---

## 已知限制

*   **JWT-Bearer Grant Type**：暂不支持 `urn:ietf:params:oauth:grant-type:jwt-bearer`（需要 `client_assertion` + ES256/RS256 签名）。相关场景 `auth/client-credentials-jwt`、`auth/cross-app-access-complete-flow` 暂不通过。
*   **OAuth 端点回退**：`2025-03-26-oauth-endpoint-fallback` 场景的 OAuth 发现端点兼容性问题待修复。
*   **SSE 重连精度**：`sse-retry` 场景的重连时间精度受事件循环调度影响，存在约 ±64ms 偏差（警告，不影响功能）。
