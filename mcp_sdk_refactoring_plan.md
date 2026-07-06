# MCP C++ SDK 架构重构与优化计划

## 一、 背景与目标 (Context & Objectives)
当前 `mcp-cpp-agent` 的 SDK 核心在处理服务端连接、配置加载以及网络传输协商时，存在一定的耦合与硬编码情况。
经过诊断，本重构计划旨在对 SDK 核心 (`src/` 目录下) 进行针对性优化，以提升 SDK 的健壮性、安全性与代码模块解耦，使其更加符合现代化、通用型的企业级 C++ SDK 标准。

注：本次重构暂**不涉及**使用 C++20 协程相关的异步接口优化。

## 二、 核心任务分解 (Tasks)

### 任务 1: 传输层智能探测与握手健壮性机制
**当前问题**：连接网络 MCP Server 时需要人工在配置中显式指定 `type`（如 `stateless_http`），且遇到服务端不发送 `endpoint` 事件的假 SSE 服务端时，连接会永久挂死。
**执行要求**：
1. **智能协议嗅探 (Auto-Negotiation)**：当 JSON 配置中未显式提供 `type` 且存在 `url` 时，客户端需对该 `url` 发起预检（如通过发出带超时的初始请求）。根据 HTTP 响应头的 `Content-Type` 是否为 `text/event-stream`，智能路由选用 `QtHttpSseTransport` 还是 `QtStatelessHttpTransport`。
2. **SSE 握手防死锁**：在 `QtHttpSseTransport` (或底层的 `QtHttpSseWorker`) 中增加初始连接超时看门狗。若 HTTP 层连接成功（Status 200），但在合理时间（如 5-10 秒）内未接收到服务端必须发送的 `event: endpoint` 报文，则必须主动切断连接、并抛出明显的连接超时/失败 Error，避免 GUI 层或其他集成层无反馈死等。

### 任务 2: 安全与配置：凭证隔离与变量解析
**当前问题**：现有的 `McpServerManager` 直接将 JSON 中的 `Authorization` Bearer 令牌当作普通字符串解析，导致敏感密钥必须以明文写死在配置文件中。
**执行要求**：
1. **环境变量插值 (Env Interpolation)**：在解析配置文件中的 `headers`、`args`、`url` 等字符串值时，增加对环境变量占位符（如 `${ANYSEARCH_API_KEY}` 或 `$ANYSEARCH_API_KEY`）的正则匹配机制。若命中占位符，需自动从宿主操作系统的环境变量中提取并替换。
2. **鉴权机制规范化**：梳理目前代码中存在的 `TokenProvider` 机制，确保无论是底层走 SSE 还是 Stateless HTTP，SDK 都支持一种标准的鉴权回调或注入机制，供外部调用者动态提供凭证（为将来的 OAuth2 动态刷新打好基础）。

### 任务 3: 核心架构解耦：剥离配置加载器
**当前问题**：`McpServerManager` 作为一个总管类，既管理了所有客户端的生命周期，又硬编码了怎么读取并解析 `.json` 配置文件的具体逻辑，违反了单一职责原则 (SRP)。
**执行要求**：
1. **抽象接口层**：设计并提取一个抽象接口层（如 `IMcpConfigLoader` 或类似设计）。
2. **职责分离**：将解析 `examples_config.json` 的全部强类型 JSON 逻辑从 `McpServerManager.cpp` 中完整剥离，封装到一个单独的类中（如 `McpJsonConfigLoader`）。
3. **依赖注入与净化**：让 `McpServerManager` 只关注纯粹的 `McpQtClient` 管理、工具汇总以及跨服务消息路由，依赖外部传入的配置清单来初始化内部状态。

## 三、 审查标准 (Review Guidelines)
执行该重构计划的 Agent 完成代码修改后，必须确保：
1. 原有的 `examples/multi_server_agent` 测试用例依然能被无缝编译并启动。
2. 剥离后的代码结构清晰，严格遵守现有的 Qt C++ 命名规范。
3. 提交审查前，应当在独立的 `build` 目录下成功完成 `cmake` 验证编译。
