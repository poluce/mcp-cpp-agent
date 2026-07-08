# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

纯 C++17 / Qt6 实现的 Model Context Protocol (MCP) 客户端 SDK。通过 Qt Network Access Manager 进行 HTTP/SSE 通信，零 libcurl 依赖。

## 构建命令

```bash
# 标准构建
cmake -B build
cmake --build build

# 运行全部测试
cd build && ctest

# 运行单个测试
./build/test/tests_qt testToolsModel

# 列出所有测试用例
./build/test/tests_qt -help

# HTTPS 运行时测试（独立目标，防止 TLS 动态装载冲突）
cmake --build build --target tests_qt_https_runtime
./build/test/tests_qt_https_runtime

# 合规测试（需先在另一个终端启动测试服务器）
node test_mcp_server.js --http &
./build/conformance_runner_qt/mcp_client_conformance_qt --suite core
./build/conformance_runner_qt/mcp_client_conformance_qt --suite all
```

## 项目架构

### 分层依赖

```
mcp_core (纯 C++17 + nlohmann/json, 零 Qt 依赖)
    ↑
mcp_qt_transport (Qt6::Core + Qt6::Network)
    ↑
mcp_qt_client (整合层, QObject 信号/槽)
```

### 各层职责与关键类

#### core/ — 协议核心（`mcp::` 命名空间）

| 类 | 职责 |
|---|---|
| `IMcpTransport` | 传输层抽象接口：纯 C++ 回调（send/onMessage/onClose/onError），不依赖 Qt |
| `McpClientSession` | MCP 协议生命周期管理：JSON-RPC 分发、请求超时清理、双向能力（Sampling/Elicitation/Roots）、通知去重。同时提供异步回调和同步阻塞两套 API |
| `JsonRpcDispatcher` | 原始 JSON-RPC 消息解析与方法路由 |
| `McpOAuthClient` | OAuth 2.0 认证（PKCE、client_credentials、token 刷新） |
| `McpMessage.h` | JSON-RPC 消息类型定义（`RequestId`、`McpErrorCode` 等） |
| `McpReconnectPolicy.h` | 重连策略结构体（指数退避、最大次数） |

#### transport/ — Qt 传输层（`mcp_qt_transport` 命名空间）

`IMcpTransport` 的三个 Qt 实现：

| 类 | 传输方式 | 关键内部组件 |
|---|---|---|
| `QtHttpSseTransport` | HTTP/SSE 长连接 | `QtHttpSseWorker`（工作线程）+ `QtSseParser`（SSE 事件解析） |
| `QtStatelessHttpTransport` | 无状态 HTTP 请求/响应 | 每次请求独立发送，无持久连接 |
| `QtProcessStdioTransport` | 子进程 stdin/stdout | 通过 QProcess 管理子进程生命周期 |

#### client/ — Qt 高层封装（`mcp_qt::` 命名空间）

| 类 | 职责 |
|---|---|
| `McpQtClient` | 主客户端：QObject 包装 `McpClientSession`，信号/槽驱动。所有 MCP 操作都有同步+异步双 API |
| `McpQtClientBuilder` | Builder 模式：链式配置 transport/http 头/proxy/重连策略后构建 |
| `McpServerManager` | 多服务器生命周期管理、心跳保活、工具预热 |
| `McpHost` | 外观模式：配置加载 → 多服务器启动 → 工具路由 → 诊断报告一站式入口 |
| `McpToolRouter` | 跨服务器工具路由（按名称前缀分发到对应 `McpQtClient`） |
| `McpPromptRouter` | 跨服务器提示词路由 |
| `McpResourceRouter` | 跨服务器资源路由 |
| `McpDiagnosticReporter` | 多服务器诊断报告生成 |
| `McpToolsModel` | `QAbstractListModel` 子类，可直接绑定 `QListView`，内置深度属性比对防 Churn（防视图重置闪烁） |
| `McpPromptsModel` / `McpResourcesModel` / `McpResourceTemplatesModel` | 同上，对应 Prompts/Resources/Templates 的 MVC 模型 |
| `McpResourceSubscriptionRouter` | 将 `notifications/resources/updated` 按 URI + token 精准派发到各回调 |
| `McpJsonConfigLoader` | 从 JSON 配置文件解析 `McpServerConfig` 列表，支持环境变量替换 |

### 关键设计模式

- **双 API 设计**：每个 MCP 操作同时提供同步版（`callTool()` 内部跑局部事件循环阻塞等待）和异步版（`callToolAsync()` 回调 / `callToolFuture()` 返回 `QFuture`）。同步 API 仅限非 GUI 线程使用
- **Builder 模式**：`McpQtClientBuilder` 链式配置后调用 `buildAndConnectAndWait()`（同步）或 `buildAndConnectAsync()`（异步）
- **重连自愈**：`McpReconnectPolicy` 指数退避重连 + `McpQtClient` 自动恢复已注册的 notification handler、resource subscription、双向能力处理器。重连期间收到的请求排队，连接恢复后回放
- **通知去重**：`McpClientSession::enableNotificationDebounce()` 在指定时间窗口内合并重复通知，避免高频震荡
- **外观模式**：`McpHost` 组合 `McpServerManager` + Router 三部曲 + `McpDiagnosticReporter`，对外暴露统一接口

## 测试体系

### 三维测试矩阵

| 维度 | 目录 | 定位 | 框架 |
|------|------|------|------|
| 组件单元测试 | `test/` | SDK 内部白盒/灰盒测试 | Qt Test |
| 协议合规测试 | `conformance_runner_qt/` | 官方 MCP 协议黑盒验证 | 外部标准服务器 |
| 端到端实战 | `examples/` | 真实多语言混编、GUI 并发压测 | 手动/自动化 |

### 测试文件说明

`test/CMakeLists.txt` 定义了两个目标：
- `tests_qt`：主测试可执行文件（14 个测试文件），链接 `mcp_qt_transport` + `mcp_qt_client`
- `tests_qt_https_runtime`：独立的 HTTPS 运行时测试（`EXCLUDE_FROM_ALL`），防止 TLS 动态装载冲突挂掉主测试

### 合规测试状态

测试日期：2026-07-06 | 协议版本：2025-11-25 | 通过率：81%（21/26 场景）

失败场景均为已知限制：JWT-Bearer 不支持（2 个）、OAuth 端点回退（1 个）、SSE 重连精度（1 个警告）。

## 依赖项

- CMake ≥ 3.16
- Qt 6.x（Core, Network, Test；multi_server_agent 额外需要 Widgets）
- C++17 编译器（MSVC 2019+ / GCC 8+ / Clang 7+）
- nlohmann/json 3.11.3（通过 `FetchContent` 自动下载）
- Windows 上 `mcp_core` 额外链接 `bcrypt`（用于 OAuth PKCE）

通过 `FetchContent` 自动下载：nlohmann/json

系统依赖：Qt6 Core/Network/Test（Qt 传输层和测试）

## 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `MCP_ENABLE_HTTP` | ON | 启用 HTTP/SSE 传输和 OAuth 客户端 |
| `MCP_ENABLE_QT_TRANSPORT` | OFF | 启用 Qt 传输层和客户端 |

## 示例项目

| 示例 | 演示内容 |
|------|---------|
| `examples/multi_server_agent` | 完整 GUI 应用：多 MCP 服务器聚合、LLM Agent 调度（ReAct 模式）、Stdio+SSE 混合挂载、热插拔重载、Python/Node 混编服务器 |
| `examples/anysearch_qt` | 轻量搜索客户端示例 |

`multi_server_agent` 编译为静态库 `multi_server_agent_core` + 可执行文件，测试目录会链接该库以复用 Agent 组件。

## 编码约定

- **命名空间**：核心协议用 `mcp::`，Qt 层用 `mcp_qt::`（注意：transport 层在 `mcp_qt_transport` 目录但头文件路径为 `mcp_qt_transport/`）
- **头文件包含**：优先使用前向声明，避免在 .h 中包含大型头文件。`McpQtClient.h` 使用 `QPointer<T>` 而非 `T*` 来安全引用 QObject 子对象
- **中文注释**：UTF-8 编码直接写汉字，不使用 `\uXXXX` 转义
- **段落注释**：超过 10 行的函数体按逻辑阶段用 `// 中文描述` 拆分，段间留空行
- **MSVC 编译**：需要 `/utf-8` 选项（已在各 CMakeLists.txt 中配置）和 `/Zc:__cplusplus`（根 CMakeLists.txt）
- **CMAKE_AUTOMOC**：Qt 传输层和客户端需要启用 `set(CMAKE_AUTOMOC ON)`

## 平台支持

- Windows（主要开发平台）
- Linux
- macOS

## 测试服务器

```bash
# Stdio 模式（默认）
node test_mcp_server.js

# HTTP/SSE 模式（合规测试需要）
node test_mcp_server.js --http
```

## 注意事项

- Windows 路径使用双反斜杠 `\\`
- 中文注释使用 UTF-8 编码，不使用 `\uXXXX` 转义
- 合规测试需要先启动测试服务器（见 `test_mcp_server.js`）

## Git 规则

- **不擅自删除暂存区内容**：不执行 `git reset HEAD`、`git rm --cached` 等清除暂存区的操作，除非用户明确要求
- **不擅自添加到暂存区**：不执行 `git add` 将工作区修改加入暂存区，除非用户明确要求提交
