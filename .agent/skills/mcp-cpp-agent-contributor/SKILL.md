---
name: mcp-cpp-agent-contributor
description: 专门用于指导 AI 代理或新开发者为基于 Qt6 的 MCP C++ SDK (mcp-cpp-agent) 贡献代码、修复 Bug 或提交 PR 的标准操作程序 (SOP)。
---

# MCP C++ Agent Contributor Skill

## 1. 触发条件 (When to use this skill)
当用户要求“给 mcp-cpp-agent 加个功能”、“修复 SDK 的 Bug”、“重构底层传输协议”或“准备提交 PR”时，**必须强制激活本技能**并严格遵守以下所有规约。

## 2. 核心架构认知 (Architecture Mindset)
在修改任何代码前，你必须理解以下架构边界：
- `src/core/`：**纯粹的协议层**。这里只处理 JSON-RPC 的序列化/反序列化，严禁引入任何 GUI 或特定网络传输库的逻辑。
- `src/transport/`：**原生传输层**。基于 `QProcess` 和 `QNetworkAccessManager`，严禁引入 `libcurl` 等第三方网络库。
- `src/client/`：**Qt 高级封装层**。负责信号槽分发、重连自愈与状态机管理。

## 3. 编码铁律 (Coding Golden Rules)
- **绝对禁止阻塞 (No Blocking)**：这是一个高度并发的网络库，严禁使用 `while(true)` 轮询或随意的 `waitForFinished()` 阻塞事件循环。所有的网络和进程通信必须通过 **Qt 信号与槽 (Signals/Slots)** 异步处理。
- **锁的安全域 (Mutex Safety)**：在 `Transport` 层处理互斥锁 (`std::mutex` 或 `QMutex`) 时，必须在调用任何可能触发 Qt 事件循环或发射信号的代码之前**释放锁**，以防止读写死锁。
- **指针与闭包捕获**：在 Lambda 回调中处理网络响应时，宿主对象极有可能已被销毁。强制使用 `QPointer<T>` 或 `std::weak_ptr` 捕获上下文，并在第一行执行存活检查。

## 4. 测试与验证强制流程 (Mandatory Validation Workflow)
在生成代码并建议用户提交 PR 之前，你必须引导用户完成以下 **“三维立体验证”**：
1. **单元测试 (Unit Test)**：指示用户进入 `test/` 目录运行组件测试，确保 JSON 解析或配置加载没有回归 Bug。
2. **协议合规 (Conformance)**：如果修改了 `src/core` 协议层，必须指示用户通过 `conformance_runner_qt` 跑通官方的 26 个基准测试场景。
3. **端到端实战 (E2E Test)**：强制要求用户运行 `examples/multi_server_agent`，并至少进行一次“点击刷新/重载按钮”的热插拔操作，确保没有破坏底层进程树的稳定性。

## 5. PR 提交规范 (PR Submission Checklist)
最后，帮用户生成 Commit Message 和 PR 描述时，必须：
- 采用 Conventional Commits 格式（如 `feat(transport): ...`, `fix(client): ...`）。
- 在 PR 描述中勾选上述“三步验证”的完成状态。
