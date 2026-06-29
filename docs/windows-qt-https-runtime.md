# Windows 下 Qt HTTPS 运行时崩溃排障说明

## 背景

在 `tests_qt.exe` 中引入 `QSslSocket` 或触发 `https://` 访问后，程序可能在进入 `main()` 之前直接崩溃：

- 无任何业务日志输出
- `gdb`/调试器显示 `SIGSEGV`
- 崩溃地址常见为 `0x0000000000000000`

这类问题通常不是 `QtHttpSseTransport` 业务逻辑错误，而是 **Windows 下 Qt TLS/SSL 运行时装配失败**。

## 典型现象

- 程序未进入 `main()`
- 控制台没有测试输出
- 崩溃发生在模块加载或静态初始化阶段
- 引入 HTTPS/SSL 代码后才出现
- 移除 `QSslSocket`、HTTPS 探测或相关 TLS 依赖后，程序恢复正常

## 高概率原因

### 1. OpenSSL DLL 冲突

最常见原因是系统 `PATH` 中存在多份不兼容的：

- `libssl-*.dll`
- `libcrypto-*.dll`

典型冲突包括：

- 32 位 DLL 被 64 位程序加载
- 不同 OpenSSL 主版本混用
- 来自 Git、Python、Anaconda、MSYS2、MinGW、其他 Qt 安装目录的 DLL 相互污染

### 2. Qt TLS backend 装载链异常

Qt 在运行 HTTPS/SSL 相关代码时，会尝试装载 TLS backend。若 backend 依赖的 DLL 不匹配，可能在进入 `main()` 前就触发空函数指针调用或初始化崩溃。

### 3. Qt / 编译器 / OpenSSL 位数不一致

必须确认以下位数完全一致：

- Qt6
- `tests_qt.exe`
- `libssl-*.dll`
- `libcrypto-*.dll`

## 这不是什么问题

以下情况通常 **不是根因**：

- `QtHttpSseTransport` 的请求头逻辑
- SSE parser 的换行处理
- MCP session 状态机
- 普通 HTTP 明文请求逻辑

如果崩溃发生在 `main()` 之前，就应优先怀疑 **运行时 TLS 装配**，而不是 transport 业务代码。

## 快速排查步骤

### 1. 确认程序位数

确认当前构建产物和 Qt 安装是同一位数：

```powershell
Get-Command g++
Get-ChildItem build\tests_qt\tests_qt.exe
```

如果使用 Qt Creator / MinGW / MSVC，多检查一遍 kit 和 generator 是否一致。

### 2. 检查系统里有哪些 OpenSSL DLL

```powershell
where libssl-*.dll
where libcrypto-*.dll
where qopensslbackend.dll
where qschannelbackend.dll
```

如果输出来自多个工具链目录，需要高度警惕。

### 3. 检查 PATH 污染

```powershell
$env:PATH -split ';'
```

重点关注是否混入以下来源：

- Git 自带 OpenSSL
- Python / Anaconda
- MSYS2 / MinGW
- 其他 Qt 安装目录
- 手工复制的 OpenSSL 目录

### 4. 用模块依赖工具确认实际加载链

推荐工具：

- Dependencies
- Process Monitor
- WinDbg

重点确认 `tests_qt.exe` 最终实际加载了哪一份：

- `libssl-*.dll`
- `libcrypto-*.dll`
- `qopensslbackend.dll`

## 建议处理方式

### 方案 A：固定 DLL 来源

不要依赖系统 `PATH` 漫游寻找 OpenSSL。

建议把 Qt HTTPS 所需的运行时 DLL 放到 `tests_qt.exe` 同目录，保证加载路径稳定、可控。

目标效果：

- 测试进程优先命中本地目录
- 避免被系统其他软件携带的 OpenSSL 污染

### 方案 B：优先使用 Qt 支持的 Windows 原生 TLS backend

如果当前 Qt 构建支持 Schannel，则优先走系统 TLS backend，尽量减少对外部 OpenSSL DLL 的依赖。

注意：

- 是否可行取决于当前 Qt 构建方式
- 不能假设所有 Qt 包都默认这么工作

### 方案 C：隔离 HTTPS 运行时测试

不要把 HTTPS/TLS 探测长期放在 `tests_qt.exe` 主测试入口中。

原因：

- 一旦 TLS 运行时装配崩溃，会导致整个 Qt 测试套件无法启动
- 这种问题会掩盖普通 HTTP、SSE parser、状态机等其他测试结果

建议拆成单独测试目标，例如：

- `tests_qt_https_runtime.exe`
- 或者通过环境变量显式启用

## 推荐测试策略

### 默认测试

默认 `tests_qt.exe` 仅覆盖：

- 普通 HTTP
- SSE parser
- Session 兼容性
- 认证重试逻辑
- 关闭/重连状态机

这些测试不应该依赖真实 HTTPS 运行时。

### 单独的 HTTPS 运行时测试

单独增加一组测试，只在明确环境可控时运行：

- Qt HTTPS backend 是否可用
- `QSslSocket::supportsSsl()`
- 真实 `https://` 连接是否能建立
- 目标 DLL 是否来自预期目录

可以通过环境变量控制，例如：

```powershell
$env:MCP_RUN_QT_HTTPS_RUNTIME_TESTS='1'
```

然后在测试入口中显式判断，未设置时跳过。

## 建议给开发者的结论

当 Windows 下 Qt HTTPS 代码一引入就出现 “程序未进 `main()` 即崩溃” 时，应默认按以下优先级处理：

1. 先查 TLS/SSL 运行时装配
2. 再查 OpenSSL DLL 冲突
3. 再查位数不一致
4. 最后才回头怀疑 transport 逻辑

## 建议给本项目的工程动作

### 1. 保持 Qt transport 主逻辑与 HTTPS runtime 问题解耦

`QtHttpSseTransport` 的业务逻辑测试不应依赖系统 HTTPS 运行时是否健康。

### 2. 把 HTTPS runtime 检测从常规测试主入口剥离

不要让一个平台部署问题拖垮整个测试套件。

### 3. 补一份部署说明

后续建议在项目中继续补充：

- Qt 运行时 DLL 部署说明
- OpenSSL DLL 放置策略
- Windows PATH 清理建议
- 真实 HTTPS 集成测试启用方式

## 推荐的本地排障命令

```powershell
where libssl-*.dll
where libcrypto-*.dll
where qopensslbackend.dll
where qschannelbackend.dll
$env:PATH -split ';'
```

## 当前结论

当前捕获到的现场更像是：

- Windows 平台
- Qt6
- HTTPS/TLS backend
- OpenSSL DLL 装配冲突

导致的 **运行时初始化崩溃**。

在没有固定 DLL 来源、位数一致性和 backend 行为之前，不应把这类现象归因到 `QtHttpSseTransport` 的 MCP 业务实现本身。
