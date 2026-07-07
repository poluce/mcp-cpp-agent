# MCP C++ SDK 长期演进与重构路线图 (Roadmap & Refactoring Plan)

## 一、 背景与愿景 (Context & Vision)

基于 MCP (Model Context Protocol) 协议的不断演进，以及在 `multi_server_agent` 等复杂业务场景中的实际开发体验，当前的 `mcp-cpp-agent` SDK 在底层传输与 JSON-RPC 机制上已经相当健壮。

为了使该 SDK 真正达到“企业级”通用标准，本路线图融合了**“底层架构强解耦（内功修炼）”**与**“大模型开发者体验（外功 DX）”**双重维度，旨在将其打造成一个极易接入、安全可控、高度自动化的现代化 Qt/C++ MCP 客户端基座。

> **核心架构约束**：为了最大程度保障 Qt 编译器的跨平台兼容性与生态稳定，在当前的生命周期内，本重构计划**暂不涉及**引入 C++20 协程（Coroutines）相关的异步接口重构，核心异步机制仍将依托于成熟的信号槽（Signals/Slots）或受控回调体系。

---

## 二、 阶段一：底层架构与安全解耦 (Foundation & Security)
本阶段主要解决当前 SDK 存在的强耦合、死锁风险及硬编码密钥问题，夯实地基。

### 1. 传输层智能探测与握手健壮性机制 (Auto-Negotiation)
- **智能协议嗅探**：当 JSON 配置未显式指定 `type` 且存在 `url` 时，根据 HTTP 响应头的 `Content-Type` 是否为 `text/event-stream`，智能路由选用 `QtHttpSseTransport` 还是 `QtStatelessHttpTransport`。
- **SSE 握手防死锁 (Watchdog)**：在建立网络级 HTTP 连接后，引入硬性看门狗超时机制。如果在规定时间内（如 5-10 秒）未收到必需的 `event: endpoint` 初始报文，必须果断切断连接并向上层抛出明确错误，严禁界面层出现无尽等待挂死。

### 2. 凭证隔离与环境变量动态插值 (Env Interpolation)
- **拒绝硬编码**：禁止在代码或常规配置文件中明文暴露敏感 Token（如 `Authorization: Bearer as_sk_xxx`）。
- **变量插值引擎**：在解析配置文件（`headers`、`args`、`url` 等）时，增加对占位符（如 `${API_KEY}`）的正则提取机制，并在运行时自动从宿主操作系统的环境变量中注入。
- **标准化鉴权池**：规范底层的 `TokenProvider` 机制，为将来对接更复杂的动态 OAuth2 Token 刷新体系打下接口基础。

### 3. 配置加载器彻底剥离 (Config Decoupling)
- **职责分离原则 (SRP)**：打破目前 `McpServerManager` 大包大揽的现状。
- **抽象工厂设计**：提取 `IMcpConfigLoader` 接口，并实现专门的 `McpJsonConfigLoader`。让 `McpServerManager` 彻底回归其本职工作（客户端生命周期管理、跨服务消息聚合），依赖外部工厂传入已解析好的构建参数。

---

## 三、 阶段二：生命周期与状态管理 (State & Lifecycle)
本阶段旨在解决多服务器并发连接时极易产生的时序错乱（Race Condition）与假就绪问题。

### 4. 精准的生命周期与就绪状态机 (Readiness State Machine)
- **分离“连通”与“就绪”**：将底层的 `connected`（TCP握手/SSE通路建立）与业务层的 `ready`（获取完整能力清单）严格区分。
- **自动初始化同步**：提供开箱即用的自动化能力同步选项。在底层接通后，SDK 自动并行为开发者下发 `tools/list`、`prompts/list` 请求。
- **Ready 屏障**：直到所有必需的数据拉取并反序列化完成，状态机才会流转至 `Ready` 并抛出最终的就绪信号。彻底根除业务层需要手写复杂 `fetchCount` 同步锁的痛点。

### 5. 强类型的诊断与错误上下文封装 (Diagnostic Error Context)
- **丰富错误信息**：改变目前仅抛出扁平化 `errorString` 的简陋处理。
- **McpError 类**：将底层的 JSON-RPC 错误码（Code）、错误摘要（Message）以及附加详细数据（Data）封装为专门的错误对象，使得业务层能够精准实施策略（如拦截 `-32603` 异常通知大模型重试，拦截 `401 Unauthorized` 触发重连）。

---

## 四、 阶段三：大模型业务侧“甜点”特性 (Developer Experience)
本阶段专注于打造 LLM Friendly（大模型友好）体验，让接入智能体框架的代码量降至最低。

### 6. 内置大模型 Schema 一键转换适配器 (LLM Schema Adapters)
- **消除样板代码**：业务开发者不再需要手动遍历拼接复杂的函数声明 JSON。
- **开箱即用接口**：在 SDK 基础模型（如 `McpQtTool`）中原生提供诸如 `QJsonObject toOpenAiFunctionSchema()`、`QJsonObject toAnthropicToolSchema()` 等序列化方法，一键将 MCP 工具协议翻译为指定大模型 API 原生认可的格式规范。

### 7. 透明化的多服务器路由与命名空间管理 (Built-in Namespacing)
- **化解同名冲突**：原生解决当接入多个不同提供商（如 AnySearch 与 FileSystem）同时拥有重名工具（如 `search`、`read`）时的路由危机。
- **底层接管路由**：在客户端工厂化构建时允许设置命名空间（如 `builder.setNamespace("web")`）。上层取出的工具自动带有 `web__` 前缀供大模型识别；当大模型发起调用 `web__search` 时，SDK 内部自动剔除前缀并精准定向分发，无需开发者在外部维护复杂的路由器 (`McpToolRouter`)。

---

## 五、 实际架构踩坑指北 (Architectural Context & Pitfalls)
为了帮助下一位接手的开发者少走弯路，特此记录我们在实战环境与极限压测中总结出的核心架构信息与血泪教训：

### 1. Windows 平台的 SSE 静默断连假象
- **踩坑现象**：在 Windows 下使用 `QNetworkAccessManager` 处理 `Transfer-Encoding: chunked` 的长连接 SSE 流时，如果底层 TCP 被服务器静默丢弃（非正常挥手），Qt 内部无法及时抛出 `finished()` 或 `errorOccurred()`，导致重连机制彻底瘫痪。
- **强制约束**：在 `QtHttpSseWorker` 中实现的长连接，**必须保留且不得随意删改**我们加入的“数据看门狗 (Health Check Timer)”机制（即 `m_lastHackTime` 和 100ms/5s 冷却探测器）。这是对抗系统级 Socket 黑洞的唯一有效手段。

### 2. 多线程亲和性与 `QNetworkAccessManager`
- **踩坑现象**：将 SSE 抽离成独立的网络层时，如果直接在主线程创建 QNAM 然后跨线程调用，必定导致 `QSocketNotifier: Socket notifiers cannot be enabled or disabled from another thread` 崩溃。
- **强制约束**：网络工作者必须严格遵守 Qt 的对象树与线程亲和性（Thread Affinity）。`QNetworkAccessManager` 必须在其宿主 `QThread` 启动后才实例化，且所有的通信必须通过跨线程信号槽（Queued Connection）来代理，**绝对禁止**主线程越权直接调用 Worker 内部方法。

### 3. “虚假就绪”与 0 计数器的竞态陷阱
- **踩坑现象**：在多服务器代理（`multi_server_agent`）中，最初使用 `m_pendingFetchCount == 0` 来判断所有工具是否就绪。但由于网络连接有快慢，当所有服务器都还在握手时（`fetchCount` 还未开始累加，等于 0），主循环误以为“万事俱备”，直接空载启动了 LLM。
- **强制约束**：未来的就绪状态机（Readiness）判断，必须是**“已注册的服务器列表长度 > 0 且 均已进入独立 Ready 状态”**。永远不要用简单的递减 0 计数器去度量具有时序差异的并发网络事件。

### 4. 闭包捕获与“野指针”幽灵
- **踩坑现象**：目前的异步接口（如 `fetchAllToolsAsync`）大量使用 C++11 的 `std::function` 回调。由于网络请求经常耗时数秒，如果在回调返回前，发起请求的 Session 或 GUI 对象已被用户销毁，裸指针的调用会瞬间导致 Segfault 崩溃。
- **强制约束**：只要还没有全面替换为 `QFuture`，任何写入 lambda 闭包的宿主指针，**必须且只能**通过 `QPointer<T>`（针对 Qt 对象）或 `std::weak_ptr<T>`（针对智能指针对象）进行安全捕获。在闭包执行的第一行，永远强制执行 `if (!safeThis) return;` 存活检查！

### 5. 高并发压测下的算力击穿
- **踩坑现象**：Node.js 原生测试框架的 `--suite all` 瞬间并发拉起 26 个 C++ Qt 客户端与服务器。由于 Qt `QCoreApplication` 完整的消息循环与庞大的网络线程池，会瞬间吃满 100% CPU，拖慢 I/O，导致原本毫秒级响应的测试（如 elicitation）被框架严格的 5 秒超时无情判定为失败（假报错 False Positive）。
- **强制约束**：在遇到大批量的高频端到端（E2E）并发报错时，务必先采取**降级运行（如单独 `--scenario`）**。不要对业务代码产生技术焦虑，先排查是否是测试框架本身的并发度与当前机器算力发生了“硬碰撞”。
