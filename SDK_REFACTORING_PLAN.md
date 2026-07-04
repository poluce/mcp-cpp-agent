# MCP Qt SDK 重构执行计划 (Execution Plan)

**目标:** 解决 `SDK_REFACTORING_PROPOSAL.md` 中指出的架构痛点，提升 SDK 的稳定性、异步性能及开箱即用体验。

本计划供后续助理实施代码修改时作为行动指南。

---

## 阶段一：下沉代理配置，解决抽象泄露 (对应改进点一)

**目标文件:** 
1. `src/transport/src/QtProcessStdioTransport.cpp`
2. `src/client/src/McpServerManager.cpp`

**实施步骤:**
1. **修改 `QtProcessStdioTransport.cpp`:**
   - 引入必要的头文件：`<QNetworkProxyFactory>`, `<QNetworkProxyQuery>`, `<QUrl>`。
   - 在 `start()` 方法中，在配置 `QProcessEnvironment` 之前，自动探测系统 HTTP/SOCKS5 代理。
   - 将探测到的代理拼接为字符串，并注入到即将传给 `m_process` 的 `HTTP_PROXY`, `HTTPS_PROXY` 环境变量中。注意：如果传入的 `m_env` 中已经包含了自定义代理配置，则不应覆盖。
2. **修改 `McpServerManager.cpp`:**
   - 找到 `loadConfig` 方法。
   - **删除**原有的手动获取 `QNetworkProxyFactory` 并硬编码注入 `HTTP_PROXY` 的代码块（约 60-76 行）。
   - 让 `McpServerManager` 只关注业务层配置，将底层通信环境的准备完全交给 Transport 类。

---

## 阶段二：消除同步阻塞，全链路异步化 (对应改进点三)

**目标文件:**
1. `src/client/include/mcp_qt_client/McpQtClient.h`
2. `src/client/src/McpQtClient.cpp`
3. 示例中调用这些方法的业务代码 (如 `AgentSession.cpp`)

**实施步骤:**
1. **废除假同步方法:**
   - 移除或标记废弃 `McpQtClient::runSyncWithTimeout` 及其相关调用。
   - 移除同步版本的 `listTools()` 和 `fetchAllTools(int to)` 方法实现。
2. **重构异步接口:**
   - 依赖现有的 `listToolsAsync`，或者提供基于 Qt 信号的替代方案。
   - 例如：实现 `fetchAllToolsAsync()`，该方法在内部处理分页游标，当所有页面拉取完毕后，触发一个 `toolsReady(const std::vector<McpQtTool>& tools)` 的 Qt 信号。
3. **适配业务层:**
   - 修改 `multi_server_agent` 中的 `AgentSession` 等调用方，将原来阻塞等待工具列表的代码，改为连接 `toolsReady` 信号的槽函数处理逻辑。

---

## 阶段三：精确错误定义与重连机制优化 (对应改进点二)

**目标文件:**
1. `src/client/src/McpQtClient.cpp`
2. `src/transport/src/QtProcessStdioTransport.cpp`

**实施步骤:**
1. **明确 stderr 不是网络故障:**
   - 在 `QtProcessStdioTransport::handleReadyReadStandardError()` 中，确保只处理日志，**不触发** `onError` 回调从而导致 `McpQtClient` 误判为传输失败。可以考虑将 `stderr` 信息通过一个专用的 `serverLog(QString)` 信号向外抛出。
2. **梳理 `handleTransportFailure`:**
   - 确认 `McpQtClient.cpp` 中的 `handleTransportFailure` 方法已安全使用 `QTimer::singleShot` (或现有的 `m_reconnectTimer`)，彻底排查是否存在任何潜在的 `while` 重试死循环。
   - 移除原本用于规避死循环的临时注释（如 439 行的 `// 注释 handleTransportFailure() 防止无限重启死循环崩溃`），恢复正常的断线重连调用机制（前提是阶段三第 1 步已经修复了误判问题）。

---

## 阶段四：(可选进阶) 统一 Qt 类型边界

**目标文件:** `src/client/include/mcp_qt_client/*.h`

**实施步骤:**
- 检查 `McpQtClient` 向外暴露的 API。
- 确保公共方法不返回 `std::string` 或 `nlohmann::json`，而是统一转换为 `QString` 和 `QJsonObject`，降低外部 Qt 工程接入时的类型转换负担。
