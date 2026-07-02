# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

这是一个纯 C++17 实现的 Model Context Protocol (MCP) 客户端 SDK，提供两套完整的客户端实现：

1. **C++ 版** (libcurl + httplib) - 纯 C++17，零外部框架依赖
2. **Qt 版** (Qt Network Access Manager) - 基于 Qt6，零 libcurl 依赖

两套实现均通过官方 conformance 测试套件验证。

## 构建命令

### 标准构建（含 HTTP/SSE 传输 + OAuth）
```bash
cmake -B build -DMCP_ENABLE_HTTP=ON
cmake --build build
```

### Qt 版构建（含 QtHttpSseTransport + McpQtClient）
```bash
cmake -B build -DMCP_ENABLE_HTTP=ON -DMCP_ENABLE_QT_TRANSPORT=ON
cmake --build build
```

### 纯 Stdio 模式（跳过 HTTP 依赖，编译仅需约 10 秒）
```bash
cmake -B build -DMCP_ENABLE_HTTP=OFF
cmake --build build
```

## 测试命令

### 单元测试
```bash
# 构建并运行单元测试
cmake -B build -DMCP_ENABLE_HTTP=ON
cmake --build build
cd build && ctest

# 或直接运行测试可执行文件
./build/tests/mcp_core_tests
```

### 合规测试（Conformance Tests）
```bash
# 运行官方合规测试（需要启动测试服务器，见 test_mcp_server.js）
./build/conformance_runner/mcp_client_conformance --suite core
./build/conformance_runner/mcp_client_conformance --suite all

# 环境变量配置
export MCP_CONFORMANCE_SCENARIO=<scenario_name>
export MCP_CONFORMANCE_CONTEXT='{"key": "value"}'
export MCP_CONFORMANCE_PROTOCOL_VERSION=<version>
```

### Qt 合规测试
```bash
# 构建 Qt 版合规测试
cmake -B build -DMCP_ENABLE_HTTP=ON -DMCP_ENABLE_QT_TRANSPORT=ON
cmake --build build

# 运行 Qt 合规测试
./build/conformance_runner_qt/mcp_client_conformance_qt --suite core
```

## 项目架构

### 核心库结构

```
mcp_core/
├── include/mcp_core/
│   ├── mcp_core.h                 # 一站式头文件
│   ├── McpClientSession.h         # 客户端会话（核心类）
│   ├── IMcpTransport.h            # 传输层接口
│   ├── HttpSseTransport.h         # libcurl SSE 传输实现
│   ├── ConsoleStdioTransport.h    # 控制台 Stdio 传输
│   ├── SubprocessStdioTransport.h # 子进程 Stdio 传输
│   ├── McpOAuthClient.h           # OAuth 认证客户端
│   └── JsonRpcDispatcher.h        # JSON-RPC 消息分发器
└── src/                           # 实现文件
```

### Qt 客户端库

```
mcp_qt_client/
├── include/mcp_qt_client/
│   ├── McpQtClient.h              # Qt 高层客户端（QObject，信号/槽）
│   ├── McpQtToolResult.h          # 类型化工具返回对象
│   ├── McpToolsModel.h            # 工具列表 MVC 模型
│   ├── McpResourcesModel.h        # 资源列表模型
│   ├── McpPromptsModel.h          # 提示词列表模型
│   └── McpResourceSubscriptionRouter.h  # 资源订阅路由
└── src/
```

### Qt 传输层

```
mcp_qt_transport/
├── include/mcp_qt_transport/
│   ├── QtHttpSseTransport.h       # Qt HTTP/SSE 传输
│   ├── QtProcessStdioTransport.h  # Qt 子进程 Stdio 传输
│   └── QtStatelessHttpTransport.h # Qt 无状态 HTTP 传输
└── src/
```

## 关键设计模式

### 传输层抽象
- `IMcpTransport` 接口定义了所有传输层必须实现的方法
- 支持三种传输方式：HTTP/SSE、Stdio 控制台、Stdio 子进程
- 可通过实现 `IMcpTransport` 接口扩展自定义协议（如 WebSocket）

### 会话管理
- `McpClientSession` 是核心会话类，管理 MCP 协议的完整生命周期
- 提供三套 API 风格：异步回调、同步阻塞、Raw 字符串
- 支持自动重连和状态恢复（指数避退策略）

### Qt 集成
- `McpQtClient` 是 Qt 版的高层封装，提供信号/槽接口
- 支持与 Qt Model/View 框架直接集成（`McpToolsModel` 等）
- 提供双向能力：Elicitation、Sampling、Roots 提供者

## 开发工作流

### 添加新功能
1. 在 `mcp_core` 中实现核心逻辑（纯 C++17）
2. 在 `mcp_qt_client` 中添加 Qt 封装（如需要）
3. 在 `tests/` 中添加单元测试
4. 在 `conformance_runner/` 中添加合规测试场景

### 测试策略
- **单元测试**：测试单个组件的功能
- **集成测试**：测试组件间的交互
- **合规测试**：验证与 MCP 协议规范的兼容性

### 代码风格
- 使用 C++17 特性
- 遵循 RAII 原则
- 使用 `std::shared_ptr` 和 `std::weak_ptr` 管理生命周期
- 错误处理使用异常或返回错误码

## 依赖项

### 必需依赖
- C++17 编译器
- nlohmann/json（通过 FetchContent 自动下载）

### 可选依赖
- libcurl（HTTP/SSE 传输，通过 FetchContent 自动下载）
- cpp-httplib（SSE GET 支持，通过 FetchContent 自动下载）
- Qt6 Core/Network（Qt 版传输层）

## 常见开发任务

### 添加新的传输层
1. 实现 `IMcpTransport` 接口
2. 在 `McpClientSession` 中注册新传输类型
3. 添加相应的单元测试

### 扩展 MCP 协议支持
1. 在 `McpMessage.h` 中定义新的消息类型
2. 在 `JsonRpcDispatcher` 中添加消息处理
3. 在 `McpClientSession` 中实现相应的 API
4. 更新合规测试

### 调试技巧
- 使用 `setTrafficLogger()` 查看所有网络流量
- 启用详细日志：`setLoggingLevel("debug")`
- 使用合规测试验证协议兼容性

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

### 启动测试服务器
```bash
# Stdio 模式（默认）
node test_mcp_server.js

# HTTP/SSE 模式
node test_mcp_server.js --http
```

### 测试服务器功能
- 支持 Stdio 和 HTTP/SSE 双重协议
- 提供多种测试工具：计算、系统信息、异常触发、超时延迟等
- 支持资源、提示词、采样等 MCP 协议功能
- 用于集成测试和合规测试

## 注意事项

- Windows 路径使用双反斜杠 `\\`
- 中文注释使用 UTF-8 编码，不使用 `\uXXXX` 转义
- Qt 版需要 Qt6 开发环境
- 合规测试需要先启动测试服务器（见 `test_mcp_server.js`）
