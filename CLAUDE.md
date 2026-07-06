# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

纯 C++17 / Qt6 实现的 Model Context Protocol (MCP) 客户端 SDK。通过 Qt Network Access Manager 进行 HTTP/SSE 通信，零 libcurl 依赖。

## 构建命令

```bash
# 标准构建
cmake -B build
cmake --build build

# 运行测试
cd build && ctest
# 或直接运行
./build/tests_qt/tests_qt

# 合规测试（需先启动测试服务器）
node test_mcp_server.js --http &
./build/conformance_runner_qt/mcp_client_conformance_qt --suite core
```

## 项目架构

```
src/
├── core/          # mcp_core - 协议核心（纯 C++17，零 Qt 依赖）
│   ├── McpMessage.h            # JSON-RPC 消息类型定义
│   ├── McpClientSession.h      # 会话管理（核心类）
│   ├── IMcpTransport.h         # 传输层抽象接口
│   ├── JsonRpcDispatcher.h     # JSON-RPC 消息分发
│   └── McpOAuthClient.h        # OAuth 认证
├── transport/     # mcp_qt_transport - Qt 传输层实现
│   ├── QtHttpSseTransport.h    # HTTP/SSE 长连接传输
│   ├── QtStatelessHttpTransport.h  # 无状态 HTTP 传输
│   └── QtProcessStdioTransport.h   # 子进程 Stdio 传输
└── client/        # mcp_qt_client - Qt 高层封装
    ├── McpQtClient.h           # 主客户端（QObject，信号/槽）
    ├── McpServerManager.h      # 多服务器生命周期管理
    ├── McpToolsModel.h         # 工具列表 MVC 模型
    ├── McpToolRouter.h         # 跨服务器工具路由
    └── McpResourceRouter.h     # 跨服务器资源路由
```

### 依赖关系

```
mcp_core (纯 C++17 + nlohmann/json)
    ↑
mcp_qt_transport (Qt6::Core + Qt6::Network)
    ↑
mcp_qt_client (整合层)
```

### 关键设计

- **传输层抽象**：`IMcpTransport` 接口统一所有传输方式，支持 HTTP/SSE、Stdio
- **会话管理**：`McpClientSession` 管理 MCP 协议生命周期，提供异步回调和同步阻塞两套 API
- **多服务器路由**：`McpServerManager` 管理多个 `McpQtClient` 实例，`McpToolRouter`/`McpResourceRouter` 汇总跨服务器的工具和资源
- **Qt Model/View 集成**：`McpToolsModel` 等模型类直接可用于 Qt MVC 框架

## 测试

```bash
# 单个测试用例
./build/tests_qt/tests_qt testToolsModel

# HTTPS 运行时测试（独立目标，防止 TLS 冲突）
cmake --build build --target tests_qt_https_runtime
./build/tests_qt/tests_qt_https_runtime
```

测试文件位于 `tests_qt/`，使用 Qt Test 框架。

## 依赖项

通过 FetchContent 自动下载：nlohmann/json

系统依赖：Qt6 Core/Network/Test（Qt 传输层和测试）

## 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `MCP_ENABLE_HTTP` | ON | 启用 HTTP/SSE 传输和 OAuth 客户端 |
| `MCP_ENABLE_QT_TRANSPORT` | OFF | 启用 Qt 传输层和客户端 |

## 平台支持

- Windows（主要开发平台）
- Linux
- macOS

## 测试服务器

```bash
# Stdio 模式（默认）
node test_mcp_server.js

# HTTP/SSE 模式
node test_mcp_server.js --http
```

## 注意事项

- Windows 路径使用双反斜杠 `\\`
- 中文注释使用 UTF-8 编码，不使用 `\uXXXX` 转义
- 合规测试需要先启动测试服务器（见 `test_mcp_server.js`）

## Git 规则

- **不擅自删除暂存区内容**：不执行 `git reset HEAD`、`git rm --cached` 等清除暂存区的操作，除非用户明确要求
- **不擅自添加到暂存区**：不执行 `git add` 将工作区修改加入暂存区，除非用户明确要求提交
