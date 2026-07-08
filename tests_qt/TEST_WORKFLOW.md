# MCP Qt C++ Agent 测试流程指南 (Test Workflow)

本文档描述了 `tests_qt` 模块的测试架构、编译运行流程，以及编写新测试用例的最佳实践。

## 1. 测试架构概览

`tests_qt` 使用 **QTest** 框架（Qt 官方测试框架）结合自定义的轻量级宏，实现了一套多套件合并运行的集成环境。

- **`main_qt.cpp`** / **`main.cpp`**
  包含了通用的测试运行器（Runner），它会自动接管各个通过宏注册的 QObject 测试类，并提供漂亮的测试总结输出。
  
- **`TmTestRunner`**
  自定义测试调度器。在宏 `TM_REGISTER_TEST` 注册测试类后，`TmTestRunner::runTests()` 会依次执行各个类，收集 Pass/Fail 数据，最终在控制台输出如下总结：
  ```text
  ==================================================
    Test Runner Summary:
    - Total Test Cases Run: 23
    - Passed Test Cases   : 23
    - Failed Test Cases   : 0
    - Asserts (Pass/Fail) : 112/0
  ==================================================
  ```

## 2. 编译与运行流程

### 2.1 依赖环境
- CMake 3.16+
- Qt 6 (包含 Qt6Core, Qt6Test, Qt6Network, Qt6WebSockets 等核心模块)
- MinGW / MSVC (Windows), GCC/Clang (Linux/macOS)

### 2.2 编译命令
在项目根目录强制进行 **Out-of-source 独立目录编译**：

```bash
mkdir build
cd build
cmake -G "MinGW Makefiles" ..  # 或忽略 -G 使用默认生成器
cmake --build . --target tests_qt
```

### 2.3 运行命令
编译成功后，在 `build` 文件夹内运行生成的可执行文件：

```bash
# Windows
.\build_tests_qt\tests_qt.exe

# Linux/macOS
./build_tests_qt/tests_qt
```

运行日志将输出每个测试用例的 `[✓]` 或 `[✗]`。如果测试存在失败（Failure），错误日志会精确指向失败的行号和预期值（Actual vs Expected）。

## 3. 编写新的测试用例

在 `tests_qt` 目录中创建一个新的 `.cpp` 文件（例如 `test_qt_new_feature.cpp`）。不需要为其编写头文件，因为测试通过宏自动注册。

### 3.1 模板示例

```cpp
#include <QTest>
#include <QObject>
#include <QDebug>
#include "TmTestMacros.h"

// 引入你需要测试的核心类
#include "mcp_qt_client/McpQtClient.h"

// 1. 定义一个继承自 QObject 的类
class TestNewFeature : public QObject {
    Q_OBJECT

private slots:
    // 2. 初始化环境 (每个测试用例前执行)
    void init() {
        // qInfo() << "Test setup";
    }

    // 3. 清理环境 (每个测试用例后执行)
    void cleanup() {
        // qInfo() << "Test teardown";
    }

    // 4. 定义测试用例 (必须声明为 private slots)
    void test_something_works() {
        int a = 1;
        int b = 2;
        
        // 核心验证宏，TM_COMPARE(实际值, 预期值, 错误说明)
        TM_COMPARE(a + b, 3, "Addition should work correctly");
        
        // TM_VERIFY(条件, 错误说明)
        TM_VERIFY(a < b, "a must be less than b");
    }
};

// 5. 注册测试套件（Suite 名称任意）
TM_REGISTER_TEST(TestNewFeature, "New Feature Component Tests")
```

### 3.2 必须注意的 CMake 配置
在 `tests_qt/CMakeLists.txt` 中，由于使用了 Qt 的元对象编译器 (MOC)，如果你新增了一个带有 `Q_OBJECT` 宏的源文件，**必须**确保 `CMAKE_AUTOMOC` 处于开启状态。CMake 会自动扫描源文件并生成对应的 `moc_*.cpp`。
如果你在 `tests_qt` 目录下新建了文件，确保将该文件包含在 `tests_qt` 的 `add_executable` 源列表中。

## 4. 断言宏说明

框架提供了一系列定制断言宏（位于 `TmTestMacros.h`），它们能够与 QTest 兼容，但在失败时提供更清晰的格式化输出，并且能被汇总到全局计数器中：

| 宏签名 | 作用说明 |
| :--- | :--- |
| `TM_VERIFY(condition, msg)` | 检查 `condition` 是否为 true。如果为 false，输出 `msg` 并标记该测试失败，立即阻断当前函数执行。 |
| `TM_COMPARE(actual, expected, msg)` | 检查 `actual == expected`。如果失败，输出它们各自的具体值以及 `msg` 并标记失败，阻断当前函数。 |
| `TM_EXPECT_FAIL(msg)` | 标记该用例中接下来的一个断言被允许/期望失败（针对异常流测试）。 |

## 5. 异步和事件循环测试

MCP 协议依赖网络传输（HTTP / SSE / Stdio），许多逻辑是纯异步的。为了在同步风格的测试用例中验证异步逻辑，需要借助 `QEventLoop` / `QTimer` 进行阻塞等待或超时检测。

**异步测试最佳实践示例：**

```cpp
void test_async_response() {
    bool gotResponse = false;
    
    // 1. 发起异步调用
    client->sendRequest("...", [&gotResponse](auto result) {
        gotResponse = true;
    });

    // 2. 启动局部事件循环以等待
    QEventLoop loop;
    
    // 设置 2000ms 超时保护，防止死锁
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    
    // 在收到信号时退出事件循环
    connect(client.get(), &McpQtClient::requestFinished, &loop, &QEventLoop::quit);
    
    loop.exec(); // 阻塞，直到超时或信号触发

    // 3. 断言验证
    TM_VERIFY(gotResponse, "Should have received response within 2000ms");
}
```

*附录：排查异步事件时的注意事项。*
- 遇到测试执行“卡死”，往往是因为局部创建的 Mock 服务器 Socket 或 Timer 未完全销毁。在断线重连或销毁逻辑中务必对现存的 `QTcpSocket` 执行 `close()` 和 `deleteLater()`。
- 如果发现网络流处理异常或超时，建议在相应层级加上毫秒级时间戳的 `qDebug()` 日志输出，这能有效辅助确认事件触发的真实先后顺序（即发现竞态冲突）。
