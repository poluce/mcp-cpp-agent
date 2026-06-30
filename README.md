# mcp-cpp-agent

纯 C++17 实现的 **Model Context Protocol (MCP) 客户端 SDK**。

提供两套完整的客户端实现，均通过官方 conformance 测试套件验证。

---

## 官方合规

| 套件 | C++ 版 (libcurl) | Qt 版 (QNAM) |
|------|:---:|:---:|
| `--suite core` (18 场景) | **235/235** ✅ | 233/235 |
| `--suite all` (26 场景) | **287/294** ✅ | — |
| 新协议 (22 场景) | **287/287 100%** ✅ | — |

> C++ 版全部通过，Qt 版仅 `sse-retry` 有时间相关的 1 项差距（QTimer 与 `sleep_for` 的精度差异）。

---

## 快速开始

### C++ 版（libcurl + httplib）

```cpp
#include <mcp_core/mcp_core.h>

// HTTP/SSE 连接
auto transport = std::make_shared<mcp::HttpSseTransport>("http://localhost:8080/mcp");
auto session   = mcp::McpClientSession::connect(transport);
session->initializeSync("my-app", "1.0.0");
auto tools = session->listToolsSync();
auto result = session->callToolSync("add", {{"a", 5}, {"b", 3}});
```

### Qt 版（纯 QNAM，零 libcurl）

```cpp
#include <mcp_qt_client/McpQtClient.h>

// 一行创建，自动完成 transport + init + start + initialize
auto client = mcp_qt::McpQtClient::connectHttp("http://localhost:8080/mcp");

// 同步 API
auto tools    = client->listTools();
auto result   = client->callTool("add", {{"a", 5}, {"b", 3}});
auto resource = client->readResource("file:///data/config.json");
auto prompt   = client->getPrompt("greeting", {{"name", "World"}});

// OAuth 认证
mcp_qt::McpQtClient::OAuthConfig oa;
oa.serverUrl    = "https://secure-server.com/mcp";
oa.clientId     = "my-client-id";
oa.clientSecret = "my-secret";
auto authClient = mcp_qt::McpQtClient::connectWithOAuth(oa);

// Stdio 子进程
auto stdioClient = mcp_qt::McpQtClient::connectStdio("python", {"server.py"});

// 双向能力
client->setElicitationHandler([](const QJsonObject& params, auto callback) { ...; callback(result, error); });
client->setSamplingHandler([](const QJsonObject& params, auto callback) { ...; callback(result, error); });
client->setRootsProvider([](auto callback) { ...; callback(roots, error); });

// 信号
QObject::connect(client.get(), &McpQtClient::connected,    []{ qDebug() << "connected"; });
QObject::connect(client.get(), &McpQtClient::disconnected, []{ qDebug() << "disconnected"; });
```

---

## API 概览

### McpQtClient 完整 API

| 分类 | 方法 |
|------|------|
| 创建 | `connectHttp(url)` `connectStdio(cmd, args)` `connectWithOAuth(config)` `McpQtClientBuilder::setHttpHeaders()` `McpQtClientBuilder::setHttpProxy()` `McpQtClientBuilder::setReconnectPolicy()` |
| 工具 | `listTools()` `listTools(cursor, &next)` `fetchAllTools()` `callTool(name, args)` `callTool(name, args, onProgress)` `callToolTyped()` `callToolTypedAsync()` `createToolsModel()` |
| 资源 | `listResources()` `fetchAllResources()` `readResource(uri)` `subscribeResource(uri)` `subscribeResource(uri, callback)` `unsubscribeResource(uri)` `unsubscribeResourceByToken()` |
| 资源模板 | `listResourceTemplates()` `fetchAllResourceTemplates()` |
| 提示词 | `listPrompts()` `fetchAllPrompts()` `getPrompt(name, args)` |
| 其他 | `ping()` `complete(ref, arg)` `setLoggingLevel(level)` `setTrafficLogger()` |
| 双向 | `setElicitationHandler(handler)` `setSamplingHandler(handler)` `setRootsProvider(provider)` `notifyRootsListChanged()` |
| 通知 | `registerNotificationHandler()` `enableNotificationDebounce()` `sendNotification()` |
| 异步 | `callToolAsync(name, args, [context], callback)` `sendRequest(method, params, [context], callback)` `cancelRequest(id)` |
| 生命周期 | `isConnected()` `close()` `setReconnectPolicy()` `setTransportFactory()` |

### McpClientSession 底层 API

三套 API 风格：**异步回调** / **同步阻塞** / **Raw 字符串**

| 操作 | 异步 | 同步 |
|------|------|------|
| 初始化 | `initialize()` | `initializeSync()` |
| 关闭 | `shutdown()` | `shutdownSync()` |
| 工具 | `listTools()` `callTool()` | `listToolsSync()` `callToolSync()` |
| 资源 | `listResources()` `readResource()` `subscribeResource()` `unsubscribeResource()` | `*Sync()` |
| 提示词 | `listPrompts()` `getPrompt()` | `*Sync()` |
| Ping | `ping()` | `pingSync()` |

---

## 构建

```bash
# C++ 版（含 HTTP/SSE 传输 + OAuth）
cmake -B build -DMCP_ENABLE_HTTP=ON
cmake --build build

# Qt 版（含 QtHttpSseTransport + McpQtClient）
cmake -B build -DMCP_ENABLE_HTTP=ON -DMCP_ENABLE_QT_TRANSPORT=ON
cmake --build build

# 纯 Stdio 模式（跳过 HTTP 依赖，编译仅需 10 秒）
cmake -B build -DMCP_ENABLE_HTTP=OFF
cmake --build build
```

---

## 项目结构

```
mcp-cpp-agent/
 ├── mcp_core/                       # SDK 核心（纯 C++17）
 │    ├── include/mcp_core/
 │    │    ├── mcp_core.h                     # 一站式头文件
 │    │    ├── McpClientSession.h              # 客户端会话
 │    │    ├── IMcpTransport.h                 # 传输层接口
 │    │    ├── HttpSseTransport.h              # libcurl SSE 传输
 │    │    ├── ConsoleStdioTransport.h         # 控制台 Stdio
 │    │    ├── SubprocessStdioTransport.h      # 子进程 Stdio
 │    │    ├── McpOAuthClient.h                # OAuth 客户端
 │    │    └── JsonRpcDispatcher.h             # JSON-RPC 分发器
 │    └── src/
 │
 ├── mcp_qt_transport/               # Qt 传输层（QNAM，零 libcurl）
 │    ├── include/mcp_qt_transport/
 │    │    └── QtHttpSseTransport.h
 │    └── src/
 │
 ├── mcp_qt_client/                  # Qt 高层客户端（QObject，信号/槽）
 │    ├── include/mcp_qt_client/
 │    │    └── McpQtClient.h
 │    └── src/
 │
 ├── conformance_runner/             # 官方合规测试客户端（C++ 版）
 ├── conformance_runner_qt/          # 官方合规测试客户端（Qt 版）
 ├── tests/                          # 单元测试
 └── tests_qt/                       # Qt 传输层测试
```

---

## SDK 加固高级特性 (Hardening Features)

### 1. 全流量 Tracing 追踪
通过在客户端设置回调，可拦截所有请求和响应报文（出站/入站），并获得时间戳、方向、类型以及结构化 JSON payload：
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

| 组件 | 依赖 |
|------|------|
| mcp_core | C++17, nlohmann/json, libcurl |
| mcp_qt_transport | Qt6::Core, Qt6::Network |
| mcp_qt_client | mcp_qt_transport, mcp_core |

---

## 传输层选择

| 场景 | 推荐 |
|------|------|
| 本地 MCP 子进程 | `SubprocessStdioTransport` |
| Qt 应用远程 HTTP/HTTPS | `QtHttpSseTransport` + `McpQtClient` |
| 非 Qt 环境远程 HTTP/HTTPS | `HttpSseTransport` |
| 自定义协议（WebSocket 等） | 实现 `IMcpTransport` 接口 |
