# mcp-cpp-agent SDK 改进建议文档 (DX Improvement Proposals)

通过对 `multi_server_agent` 示例项目的调试与重构，总结了当前 SDK (`mcp_qt_client`) 在架构设计与开发者体验（DX）上可以进一步优化的 4 个核心方向。这些建议旨在降低接入门槛，避免后续开发者重复踩坑。

## 1. 信号与函数重名的陷阱 (Signal and Method Name Clashing)
**现象与痛点**：
在 `McpServerManager` 中，`allToolsReady` 同时被定义为状态查询函数（`bool allToolsReady() const`）和信号（`void allToolsReady()`）。
这导致在使用现代 Qt 的指针 `connect` 语法时发生编译报错（ambiguous overload）。开发者被迫使用极其繁琐的强制类型转换（如 `static_cast<void(mcp_qt::McpServerManager::*)()>`）来解决重载解析问题，严重影响开发体验。

**修改建议**：
遵循现代 C++/Qt 的命名规范，彻底消除重名冲突。
- **方案 A**：将获取状态的 Getter 函数重命名为 `isAllToolsReady()`。
- **方案 B**：将信号重命名为 `allToolsReadySignal()` 或 `onAllToolsReady()`。

## 2. 工具格式导出的职责边界不清 (Tool Schema Export Redundancy)
**现象与痛点**：
SDK 中的 `McpToolRouter::exportAllToolsToLlmFormat` 直接返回了针对特定 LLM 包装好的 JSON 结构（例如 OpenAI 格式下的 `{"type": "function", "function": {...}}`）。
但在外层业务代码（如 `LlmBackends.cpp`）中，调用方往往会误以为该接口返回的只是纯粹的 MCP Tool 数据列表，从而对其进行“二次包装”，导致发送给大模型的请求中 `function.name` 被覆盖为空字符串，触发了 `HTTP 400 Bad Request` 报错。

**修改建议**：
明确 SDK 层与 LLM 层的职责边界，解耦格式转换。
- **方案 A（推荐）**：SDK 的 `McpQtClient` / `McpToolRouter` 仅负责返回原始或标准化的 MCP 工具对象数组（QJsonObject 列表）。将最终组装成 `type: function` 等大模型专有结构的逻辑完全下放给业务方的 `ILlmBackend` 处理。
- **方案 B**：如果保留当前行为，建议重命名该函数以强烈提示其返回值已被整体格式化（例如 `exportAsOpenAiFunctionPayloads()`），并在文档与注释中注明：“调用方可直接将其赋值给 API 请求的 tools 字段，无需额外包裹”。

## 3. MCP 进程的生命周期抽象与分离 (Process Lifecycle Management)
**现象与痛点**：
在原本的示例架构设计中，`McpServerManager` 的生命周期与单次对话流（AgentSession）强绑定。当用户重置会话时，整个 `McpServerManager` 会被直接销毁并重新初始化。
考虑到 MCP 的底层服务端（特别是附带 Node.js / Python 虚拟环境的进程）启动和预热开销极大，频繁杀掉重建不仅导致响应迟缓，而且违背了服务端应常驻后台的逻辑。

**修改建议**：
- **SDK 设计导向**：在文档和示例中，倡导将 `McpServerManager` 作为一个全局的长驻后台服务（Daemon / Connection Pool）存在，而非随对话任务生灭。
- **增加底层容错**：建议在 `McpServerManager` 内部集成断线重连（Auto-reconnect）或心跳检测（Keep-Alive）机制。让应用层在发起任务时只需安全地向其获取可用工具，将复杂的进程保活逻辑沉淀在 SDK 内部。

## 4. 详细的错误响应透传 (Error Response Transparency)
**现象与痛点**：
在发生 HTTP 网络错误时，Qt 自带的 `QNetworkReply::errorString()` 给出的提示模棱两可。例如，遇到未正确携带 API Key 导致的 401 错误，或 JSON 格式嵌套错误导致的 400 错误时，Qt 仅抛出 `"Host requires authentication"` 或 `"server replied with status code 400"`，这让开发者无从排查。直到修改底层代码强制打印了 HTTP Response Body，才暴露出明确的报错原因。

**修改建议**：
- 对于 SDK 中凡是涉及到网络通信的模块（无论是 `callToolAsync` 中与远程 MCP 服务器通过 HTTP 通信，还是封装的 LLM 客户端代码），在捕获 `QNetworkReply::error` 时，务必提取 `reply->readAll()`（Raw Error Body）。
- 将该原生报错信息拼接到 `errorString` 中向上抛出（例如赋值给 `McpResult.errorString`），确保开发者能在控制台直接看到诸如 `{"error": "Invalid 'tools[0]..."}` 之类的真实信息，大幅降低 Debug 门槛。
