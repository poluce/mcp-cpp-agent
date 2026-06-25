# mcp-cpp-agent

基于 C++17 和 Qt6 开发的模块化 **Model Context Protocol (MCP) 客户端调试器**。

此项目实现了精简、坚固的 MCP 客户端核心，并以直观的 GUI 调试面板呈现出 JSON-RPC 通信流程。

---

## 🛠 架构设计 (Architecture Layout)

```
mcp-cpp-agent/
 ├── mcp_core/                  // 纯 C++17 内核 (不依赖任何 GUI/平台框架)
 │    ├── McpClientSession      // 跟踪异步请求、分发消息
 │    ├── JsonRpcDispatcher     // JSON-RPC 2.0 协议解析与分发
 │    └── IMcpTransport         // 纯虚传输协议接口 (send/recv)
 │
 ├── mcp_qt/                    // Qt 适配层 (依赖 Qt6 Core & Network)
 │    ├── QtStdioTransport     // 客户端利用 QProcess 重定向子进程的 stdio 通道
 │    ├── QtHttpTransport      // 支持以 HTTP/SSE (Server Sent Events) 与远程服务端建连
 │    └── QtMcpClient           // QObject 包装层，提供标准 Qt 信号与槽
 │
 └── app/                       // 独立 UI 调试程序
      ├── AgentController       // 核心与界面间的中控调度
      ├── ToolManager           // 缓存并管理发现的 Tools
      └── ChatWindow            // Slate Dark 暗黑精致调试界面
```

---

## 🚀 编译与运行 (Compilation)

根据 **编译隔离原则 (Out-of-source Build)**，请始终在独立目录中编译，不要污染源码目录：

### 1. 命令行编译 (以 MinGW + CMake 为例)

打开命令行并确保 CMake 与编译器在 `PATH` 中：

```powershell
# 1. 新建并进入 build 目录
mkdir build
cd build

# 2. 配置 CMake 并指向 Qt6 安装位置 (若未在全局环境变量中)
cmake .. -DCMAKE_PREFIX_PATH="E:/Qt6/6.11.0/mingw_64"

# 3. 编译
cmake --build . --config Release
```

### 2. 运行调试器

编译完成后，运行 `app.exe`。您可以通过以下两种方式调试您的 MCP 服务端：

1. **Stdio 方式**：选择 `Stdio`，输入运行命令（例如，如果是 node 开发的 server 可以输入 `node`，参数输入 `C:/path/to/server.js`），点击 `Connect`。程序将作为子进程拉起它，并完成 MCP `initialize` 握手。
2. **HTTP/SSE 方式**：选择 `HTTP / SSE`，输入服务器的 SSE 地址，点击 `Connect`，程序将动态配置 POST 终结点并拉取工具列表。

当成功握手后，可在左下角 **Tool Invoker** 中查看服务器提供的所有 Tool，并会自动根据 Schema 生成参数的 JSON 模板，点击 `Call Tool` 即可在右侧查看 JSON-RPC 的交互明细日志。
