# Multi-Server Agent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `examples/multi_server_agent` with a CLI-based simulated LLM agent that accepts a task string, discovers tools from real MCP servers, executes one routed tool call when safe, and prints an SDK diagnostic report.

**Architecture:** Keep the feature self-contained inside `examples/multi_server_agent`, but split it into small example-local classes. Build the shared logic as an example-local library target so both the example executable and `tests_qt` can exercise the same code paths. Drive the work with TDD around three seams: tool selection, diagnostic rendering, and session orchestration.

**Tech Stack:** C++17, Qt6 Core/Network/Test, existing `mcp_qt_client` classes (`McpServerManager`, `McpToolRouter`, `McpQtClient`), existing custom `TmTestRunner`

**Git Note:** Do not stage or commit during execution unless the user explicitly authorizes it. Repository rules forbid commands that merge unstaged changes into the index. Replace commit checkpoints with diff-review checkpoints.

---

## File Structure

### Create

- `examples/multi_server_agent/HeuristicToolSelector.h`
- `examples/multi_server_agent/HeuristicToolSelector.cpp`
- `examples/multi_server_agent/DiagnosticReporter.h`
- `examples/multi_server_agent/DiagnosticReporter.cpp`
- `examples/multi_server_agent/AgentSession.h`
- `examples/multi_server_agent/AgentSession.cpp`
- `tests_qt/test_multi_server_agent_selector.cpp`
- `tests_qt/test_multi_server_agent_diagnostics.cpp`
- `tests_qt/test_multi_server_agent_session.cpp`

### Modify

- `examples/multi_server_agent/main.cpp`
- `examples/multi_server_agent/CMakeLists.txt`
- `examples/CMakeLists.txt`
- `tests_qt/CMakeLists.txt`
- `tests_qt/main_qt.cpp`
- `CMakeLists.txt`

### Responsibilities

- `HeuristicToolSelector.*`: rank namespaced tools for a task and explain scores
- `DiagnosticReporter.*`: collect execution-log lines plus stage-level observations/problems/suggestions and render two console sections
- `AgentSession.*`: own the end-to-end discovery, selection, safe-argument construction, invocation, and exit code flow
- `main.cpp`: parse CLI arguments, assemble objects, print final output, quit the event loop
- `tests_qt/test_multi_server_agent_selector.cpp`: stable selector behavior
- `tests_qt/test_multi_server_agent_diagnostics.cpp`: report grouping and rendering
- `tests_qt/test_multi_server_agent_session.cpp`: orchestration behavior for no-tools and unsafe-arguments flows
- build files: expose an example-local core library and bring `tests_qt` into the top-level Qt build

## Shared Build Commands

Use these commands throughout the plan:

```powershell
cmake -S . -B build -DMCP_ENABLE_HTTP=ON -DMCP_ENABLE_QT_TRANSPORT=ON
cmake --build build --target tests_qt multi_server_agent
```

Run the Qt test runner with:

```powershell
.\build\tests_qt.exe
```

Run the example with:

```powershell
.\build\examples\multi_server_agent\multi_server_agent.exe --config path\to\mcp_servers.json --task "search Qt MCP transport recovery"
```

If the generator places binaries under a configuration subdirectory, append `\Debug` or `\Release` as needed.

## Task 1: Build Seams And Implement Tool Selection

**Files:**
- Create: `examples/multi_server_agent/HeuristicToolSelector.h`
- Create: `examples/multi_server_agent/HeuristicToolSelector.cpp`
- Create: `tests_qt/test_multi_server_agent_selector.cpp`
- Modify: `examples/multi_server_agent/CMakeLists.txt`
- Modify: `tests_qt/CMakeLists.txt`
- Modify: `tests_qt/main_qt.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing selector test and wire it into the Qt test target**

Add this test file:

```cpp
#include "tests/common.h"
#include "examples/multi_server_agent/HeuristicToolSelector.h"
#include <QJsonObject>

void test_multi_server_agent_selector_prefers_search_tool() {
    HeuristicToolSelector selector;

    std::vector<mcp_qt::McpQtTool> tools{
        {QStringLiteral("search_notes"), QStringLiteral("Search notes by keyword"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{{QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}
        }},
        {QStringLiteral("save_note"), QStringLiteral("Save a note"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{{QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}
        }}
    };

    auto result = selector.rankTools(
        QStringLiteral("search notes about MCP transport"),
        QStringLiteral("mock-memory"),
        tools);

    TM_ASSERT_TRUE(result.foundMatch, "selector should find a matching tool");
    TM_ASSERT_EQ(result.candidates.size(), static_cast<size_t>(2), "selector should rank both tools");
    TM_ASSERT_EQ(result.candidates.front().namespacedToolName.toStdString(),
                 std::string("mock-memory_search_notes"),
                 "search tool should rank first");
    TM_ASSERT_TRUE(!result.candidates.front().reasons.empty(),
                   "top candidate should explain why it won");
}
```

Update `tests_qt/main_qt.cpp` declarations and runner calls:

```cpp
void test_multi_server_agent_selector_prefers_search_tool();
...
TM_RUN_TEST(test_multi_server_agent_selector_prefers_search_tool);
```

Update `tests_qt/CMakeLists.txt` sources:

```cmake
    test_multi_server_agent_selector.cpp
```

Update `CMakeLists.txt` so Qt builds include `tests_qt`:

```cmake
    if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
        add_subdirectory(conformance_runner_qt)
        add_subdirectory(examples)
        add_subdirectory(tests_qt)
    endif()
```

Expected current state: build should fail because the selector files and example-local core target do not exist yet.

- [ ] **Step 2: Run the build to verify the test fails for the expected reason**

Run:

```powershell
cmake -S . -B build -DMCP_ENABLE_HTTP=ON -DMCP_ENABLE_QT_TRANSPORT=ON
cmake --build build --target tests_qt
```

Expected: FAIL with a compile error similar to `cannot open source file "examples/multi_server_agent/HeuristicToolSelector.h"` or unresolved symbols for `HeuristicToolSelector`.

- [ ] **Step 3: Add the example-local core target and minimal selector implementation**

Replace `examples/multi_server_agent/CMakeLists.txt` with:

```cmake
set(CMAKE_AUTOMOC ON)

add_library(multi_server_agent_core STATIC
    AgentSession.cpp
    DiagnosticReporter.cpp
    HeuristicToolSelector.cpp
)

target_include_directories(multi_server_agent_core PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/mcp_qt_client/include
    ${CMAKE_SOURCE_DIR}/mcp_core/include
)

target_link_libraries(multi_server_agent_core PUBLIC
    mcp_qt_client
    Qt6::Core
    Qt6::Network
)

target_compile_features(multi_server_agent_core PUBLIC cxx_std_17)

add_executable(multi_server_agent main.cpp)

target_link_libraries(multi_server_agent PRIVATE
    multi_server_agent_core
    Qt6::Core
    Qt6::Network
)

target_compile_features(multi_server_agent PRIVATE cxx_std_17)
```

Update `tests_qt/CMakeLists.txt` includes and links:

```cmake
target_include_directories(tests_qt PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/examples/multi_server_agent
    ${CMAKE_SOURCE_DIR}/mcp_qt_transport/include
    ${CMAKE_SOURCE_DIR}/mcp_qt_transport/src
    ${CMAKE_SOURCE_DIR}/mcp_qt_client/include
)

target_link_libraries(tests_qt PRIVATE
    mcp_qt_transport
    mcp_qt_client
    multi_server_agent_core
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
```

Create `examples/multi_server_agent/HeuristicToolSelector.h`:

```cpp
#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <vector>
#include <mcp_qt_client/McpQtClient.h>

struct ToolCandidateScore {
    QString serverName;
    QString originalToolName;
    QString namespacedToolName;
    int score{0};
    QStringList reasons;
    QJsonObject inputSchema;
    QString description;
};

struct ToolSelectionResult {
    bool foundMatch{false};
    QString failureReason;
    std::vector<ToolCandidateScore> candidates;
};

class HeuristicToolSelector {
public:
    ToolSelectionResult rankTools(const QString& task,
                                  const QString& serverName,
                                  const std::vector<mcp_qt::McpQtTool>& tools) const;
};
```

Create `examples/multi_server_agent/HeuristicToolSelector.cpp`:

```cpp
#include "examples/multi_server_agent/HeuristicToolSelector.h"

#include <algorithm>

namespace {
int scoreTool(const QString& taskLower, const mcp_qt::McpQtTool& tool, QStringList* reasons) {
    int score = 0;
    const QString nameLower = tool.name.toLower();
    const QString descriptionLower = tool.description.toLower();

    if (taskLower.contains(QStringLiteral("search")) && nameLower.contains(QStringLiteral("search"))) {
        score += 5;
        reasons->append(QStringLiteral("task mentions search and tool name matches"));
    }
    if (taskLower.contains(QStringLiteral("note")) && descriptionLower.contains(QStringLiteral("note"))) {
        score += 3;
        reasons->append(QStringLiteral("task and description both mention notes"));
    }
    const QJsonObject properties = tool.inputSchema.value(QStringLiteral("properties")).toObject();
    if (properties.contains(QStringLiteral("query"))) {
        score += 2;
        reasons->append(QStringLiteral("tool accepts query-like input"));
    }
    if (properties.contains(QStringLiteral("text"))) {
        score += 1;
        reasons->append(QStringLiteral("tool accepts free-form text"));
    }
    return score;
}
}

ToolSelectionResult HeuristicToolSelector::rankTools(const QString& task,
                                                     const QString& serverName,
                                                     const std::vector<mcp_qt::McpQtTool>& tools) const {
    ToolSelectionResult result;
    const QString taskLower = task.toLower();

    for (const auto& tool : tools) {
        ToolCandidateScore candidate;
        candidate.serverName = serverName;
        candidate.originalToolName = tool.name;
        candidate.namespacedToolName = serverName + QStringLiteral("_") + tool.name;
        candidate.description = tool.description;
        candidate.inputSchema = tool.inputSchema;
        candidate.score = scoreTool(taskLower, tool, &candidate.reasons);
        result.candidates.push_back(candidate);
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const ToolCandidateScore& a, const ToolCandidateScore& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.namespacedToolName < b.namespacedToolName;
    });

    result.foundMatch = !result.candidates.empty() && result.candidates.front().score > 0;
    if (!result.foundMatch) {
        result.failureReason = QStringLiteral("No candidate tool scored above zero");
    }
    return result;
}
```

Create temporary stub files so the library target compiles before later tasks:

`examples/multi_server_agent/DiagnosticReporter.cpp`

```cpp
// Temporary stub for Task 1. Real implementation arrives in Task 2.
```

`examples/multi_server_agent/AgentSession.cpp`

```cpp
// Temporary stub for Task 1. Real implementation arrives in Task 3.
```

Also create matching temporary headers:

```cpp
#pragma once
class DiagnosticReporter {};
```

```cpp
#pragma once
class AgentSession {};
```

- [ ] **Step 4: Run the selector test to verify it passes**

Run:

```powershell
cmake --build build --target tests_qt
.\build\tests_qt.exe
```

Expected: `test_multi_server_agent_selector_prefers_search_tool` PASS.

- [ ] **Step 5: Review the diff and leave git untouched**

Run:

```powershell
git diff -- examples/multi_server_agent tests_qt CMakeLists.txt
```

Expected: diff shows only build wiring and selector-related files. Do not stage or commit.

## Task 2: Add Diagnostic Reporting And Rendering

**Files:**
- Create: `tests_qt/test_multi_server_agent_diagnostics.cpp`
- Modify: `examples/multi_server_agent/DiagnosticReporter.h`
- Modify: `examples/multi_server_agent/DiagnosticReporter.cpp`
- Modify: `tests_qt/CMakeLists.txt`
- Modify: `tests_qt/main_qt.cpp`

- [ ] **Step 1: Write the failing diagnostic-report test**

Create `tests_qt/test_multi_server_agent_diagnostics.cpp`:

```cpp
#include "tests/common.h"
#include "examples/multi_server_agent/DiagnosticReporter.h"

void test_multi_server_agent_diagnostics_groups_entries_by_stage() {
    DiagnosticReporter reporter;

    reporter.addExecutionLogLine(QStringLiteral("Loaded config and discovered tools"));
    reporter.addObservation(QStringLiteral("tool/discovery"), QStringLiteral("Loaded 3 namespaced tools"));
    reporter.addProblem(QStringLiteral("tool/discovery"),
                        QStringLiteral("Cached tool list was empty until toolsChanged arrived"),
                        QStringLiteral("Expose an explicit readiness API for tool cache"));
    reporter.addProblem(QStringLiteral("tool/call"),
                        QStringLiteral("Argument construction stopped before invocation"),
                        QStringLiteral("Add higher-level schema helpers"));

    const QString logText = reporter.renderExecutionLog();
    const QString text = reporter.renderText();

    TM_ASSERT_TRUE(logText.contains(QStringLiteral("Loaded config and discovered tools")),
                   "execution log should be rendered separately");
    TM_ASSERT_TRUE(text.contains(QStringLiteral("SDK Diagnostic Report")),
                   "report should contain the report header");
    TM_ASSERT_TRUE(text.contains(QStringLiteral("[tool/discovery]")),
                   "report should contain the discovery stage");
    TM_ASSERT_TRUE(text.contains(QStringLiteral("Loaded 3 namespaced tools")),
                   "observation should be rendered");
    TM_ASSERT_TRUE(text.contains(QStringLiteral("Expose an explicit readiness API for tool cache")),
                   "suggestion should be rendered");
}
```

Wire the test into `tests_qt/CMakeLists.txt` and `tests_qt/main_qt.cpp`:

```cmake
    test_multi_server_agent_diagnostics.cpp
```

```cpp
void test_multi_server_agent_diagnostics_groups_entries_by_stage();
...
TM_RUN_TEST(test_multi_server_agent_diagnostics_groups_entries_by_stage);
```

Expected current state: build should fail because `DiagnosticReporter` is still a stub without the required methods.

- [ ] **Step 2: Run the build to verify the test fails for the expected reason**

Run:

```powershell
cmake --build build --target tests_qt
```

Expected: FAIL with errors for missing `addObservation`, `addProblem`, or `renderText`.

- [ ] **Step 3: Implement the real diagnostic reporter**

Replace `examples/multi_server_agent/DiagnosticReporter.h` with:

```cpp
#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct DiagnosticItem {
    QString kind;
    QString message;
    QString suggestion;
};

class DiagnosticReporter {
public:
    void addExecutionLogLine(const QString& line);
    void addObservation(const QString& stage, const QString& message);
    void addProblem(const QString& stage, const QString& message, const QString& suggestion = QString());
    QString renderExecutionLog() const;
    QString renderText() const;
    bool hasProblems() const;

private:
    void addItem(const QString& stage, const QString& kind, const QString& message, const QString& suggestion);

    QList<QString> m_executionLog;
    QMap<QString, QList<DiagnosticItem>> m_itemsByStage;
};
```

Replace `examples/multi_server_agent/DiagnosticReporter.cpp` with:

```cpp
#include "examples/multi_server_agent/DiagnosticReporter.h"

#include <QStringBuilder>

void DiagnosticReporter::addExecutionLogLine(const QString& line) {
    m_executionLog.append(line);
}

void DiagnosticReporter::addObservation(const QString& stage, const QString& message) {
    addItem(stage, QStringLiteral("observation"), message, QString());
}

void DiagnosticReporter::addProblem(const QString& stage, const QString& message, const QString& suggestion) {
    addItem(stage, QStringLiteral("problem"), message, suggestion);
}

void DiagnosticReporter::addItem(const QString& stage, const QString& kind, const QString& message, const QString& suggestion) {
    m_itemsByStage[stage].append(DiagnosticItem{kind, message, suggestion});
}

bool DiagnosticReporter::hasProblems() const {
    for (auto it = m_itemsByStage.constBegin(); it != m_itemsByStage.constEnd(); ++it) {
        for (const auto& item : it.value()) {
            if (item.kind == QStringLiteral("problem")) {
                return true;
            }
        }
    }
    return false;
}

QString DiagnosticReporter::renderExecutionLog() const {
    QString out = QStringLiteral("Agent Execution Log\n");
    out += QStringLiteral("===================\n");
    for (const auto& line : m_executionLog) {
        out += QStringLiteral("- ") + line + QStringLiteral("\n");
    }
    return out;
}

QString DiagnosticReporter::renderText() const {
    QString out = QStringLiteral("SDK Diagnostic Report\n");
    out += QStringLiteral("=====================\n");
    for (auto it = m_itemsByStage.constBegin(); it != m_itemsByStage.constEnd(); ++it) {
        out += QStringLiteral("\n[") + it.key() + QStringLiteral("]\n");
        for (const auto& item : it.value()) {
            out += QStringLiteral("- ") + item.kind + QStringLiteral(": ") + item.message + QStringLiteral("\n");
            if (!item.suggestion.isEmpty()) {
                out += QStringLiteral("  suggestion: ") + item.suggestion + QStringLiteral("\n");
            }
        }
    }
    return out;
}
```

- [ ] **Step 4: Run the diagnostic test to verify it passes**

Run:

```powershell
cmake --build build --target tests_qt
.\build\tests_qt.exe
```

Expected: `test_multi_server_agent_diagnostics_groups_entries_by_stage` PASS.

- [ ] **Step 5: Review the diff and leave git untouched**

Run:

```powershell
git diff -- examples/multi_server_agent tests_qt
```

Expected: diff shows diagnostic reporter and its test only. Do not stage or commit.

## Task 3: Implement Agent Session Orchestration

**Files:**
- Create: `tests_qt/test_multi_server_agent_session.cpp`
- Modify: `examples/multi_server_agent/AgentSession.h`
- Modify: `examples/multi_server_agent/AgentSession.cpp`
- Modify: `tests_qt/CMakeLists.txt`
- Modify: `tests_qt/main_qt.cpp`

- [ ] **Step 1: Write failing session tests for no-tools and unsafe-arguments paths**

Create `tests_qt/test_multi_server_agent_session.cpp`:

```cpp
#include "tests/common.h"
#include "examples/multi_server_agent/AgentSession.h"
#include "examples/multi_server_agent/DiagnosticReporter.h"
#include "examples/multi_server_agent/HeuristicToolSelector.h"
#include "mcp_qt_client/McpServerManager.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>

void test_multi_server_agent_session_reports_no_tools() {
    mcp_qt::McpServerManager manager;
    DiagnosticReporter reporter;
    HeuristicToolSelector selector;
    AgentSession session(&manager, &selector, &reporter);

    QEventLoop loop;
    int exitCode = -1;
    QObject::connect(&session, &AgentSession::finished, &loop, [&](int code) {
        exitCode = code;
        loop.quit();
    });

    session.runAgainstCurrentClients(QStringLiteral("search anything"), QString(), 100);
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();

    TM_ASSERT_TRUE(exitCode != 0, "no-tools flow should fail");
    TM_ASSERT_TRUE(reporter.renderText().contains(QStringLiteral("[tool/discovery]")),
                   "no-tools flow should be reported as discovery failure");
}

void test_multi_server_agent_session_reports_unsafe_arguments_before_call() {
    mcp_qt::McpServerManager manager;
    auto client = mcp_qt::McpQtClient::createForTest(&manager);
    manager.registerClient(QStringLiteral("mock-files"), client);

    mcp_qt::McpQtTool openFile{
        QStringLiteral("open_file"),
        QStringLiteral("Open a file by path"),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{
                {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}
            }},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}
        }
    };
    emit client->toolsChanged({openFile});

    DiagnosticReporter reporter;
    HeuristicToolSelector selector;
    AgentSession session(&manager, &selector, &reporter);

    QEventLoop loop;
    int exitCode = -1;
    QObject::connect(&session, &AgentSession::finished, &loop, [&](int code) {
        exitCode = code;
        loop.quit();
    });

    session.runAgainstCurrentClients(QStringLiteral("open the latest config file"), QStringLiteral("mock-files"), 100);
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();

    TM_ASSERT_TRUE(exitCode != 0, "unsafe-argument flow should fail");
    TM_ASSERT_TRUE(reporter.renderText().contains(QStringLiteral("[tool/selection]")),
                   "unsafe arguments should be classified before invocation");
}
```

Wire the test into `tests_qt/CMakeLists.txt` and `tests_qt/main_qt.cpp`:

```cmake
    test_multi_server_agent_session.cpp
```

```cpp
void test_multi_server_agent_session_reports_no_tools();
void test_multi_server_agent_session_reports_unsafe_arguments_before_call();
...
TM_RUN_TEST(test_multi_server_agent_session_reports_no_tools);
TM_RUN_TEST(test_multi_server_agent_session_reports_unsafe_arguments_before_call);
```

Expected current state: build should fail because `AgentSession` is still a stub.

- [ ] **Step 2: Run the build to verify the tests fail for the expected reason**

Run:

```powershell
cmake --build build --target tests_qt
```

Expected: FAIL with missing `AgentSession` constructor, signal, or `runAgainstCurrentClients`.

- [ ] **Step 3: Implement the session class with a test seam for preloaded managers**

Replace `examples/multi_server_agent/AgentSession.h` with:

```cpp
#pragma once

#include "examples/multi_server_agent/DiagnosticReporter.h"
#include "examples/multi_server_agent/HeuristicToolSelector.h"
#include "mcp_qt_client/McpServerManager.h"
#include "mcp_qt_client/McpToolRouter.h"

#include <QObject>
#include <QJsonObject>

struct AgentRunOptions {
    QString configPath;
    QString task;
    QString serverFilter;
    int timeoutMs{30000};
};

class AgentSession : public QObject {
    Q_OBJECT
public:
    AgentSession(mcp_qt::McpServerManager* manager,
                 HeuristicToolSelector* selector,
                 DiagnosticReporter* reporter,
                 QObject* parent = nullptr);

    void start(const AgentRunOptions& options);
    void runAgainstCurrentClients(const QString& task, const QString& serverFilter, int timeoutMs);

signals:
    void finished(int exitCode);

private:
    QJsonObject buildSafeArguments(const ToolCandidateScore& candidate, const QString& task, QString* error) const;
    void finishWithError(const QString& stage, const QString& message, const QString& suggestion);
    void finishSuccessfully(const QString& message);
    void beginRunAgainstCurrentClients(const QString& task, const QString& serverFilter);

    mcp_qt::McpServerManager* m_manager{nullptr};
    HeuristicToolSelector* m_selector{nullptr};
    DiagnosticReporter* m_reporter{nullptr};
    mcp_qt::McpToolRouter m_router;
    bool m_runStarted{false};
};
```

Replace `examples/multi_server_agent/AgentSession.cpp` with:

```cpp
#include "examples/multi_server_agent/AgentSession.h"

#include <QJsonArray>
#include <QTimer>

AgentSession::AgentSession(mcp_qt::McpServerManager* manager,
                           HeuristicToolSelector* selector,
                           DiagnosticReporter* reporter,
                           QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_selector(selector)
    , m_reporter(reporter)
    , m_router(manager) {}

void AgentSession::start(const AgentRunOptions& options) {
    if (!m_manager->loadConfigFile(options.configPath)) {
        finishWithError(QStringLiteral("config/load"),
                        QStringLiteral("Failed to load config file"),
                        QStringLiteral("Check the path and JSON structure"));
        return;
    }
    m_reporter->addExecutionLogLine(QStringLiteral("Loaded config file %1").arg(options.configPath));
    m_reporter->addExecutionLogLine(QStringLiteral("Waiting for server connection events"));

    QObject::connect(m_manager, &mcp_qt::McpServerManager::clientConnected, this, [this, options](const QString& serverName) {
        m_reporter->addExecutionLogLine(QStringLiteral("Server connected: %1").arg(serverName));
        if (!m_runStarted) {
            beginRunAgainstCurrentClients(options.task, options.serverFilter);
        }
    });
    QObject::connect(m_manager, &mcp_qt::McpServerManager::clientErrorOccurred, this, [this](const QString& serverName, const QString& error) {
        m_reporter->addProblem(QStringLiteral("server/connect"),
                               QStringLiteral("Server '%1' reported error: %2").arg(serverName, error),
                               QStringLiteral("Expose a clearer aggregate readiness API after config load"));
    });

    QTimer::singleShot(options.timeoutMs, this, [this, options]() {
        if (!m_runStarted) {
            m_reporter->addExecutionLogLine(QStringLiteral("Connection wait elapsed; continuing with currently registered clients"));
            beginRunAgainstCurrentClients(options.task, options.serverFilter);
        }
    });
}

void AgentSession::runAgainstCurrentClients(const QString& task, const QString& serverFilter, int) {
    beginRunAgainstCurrentClients(task, serverFilter);
}

void AgentSession::beginRunAgainstCurrentClients(const QString& task, const QString& serverFilter) {
    m_runStarted = true;
    QStringList targetServers = m_manager->serverNames();
    if (!serverFilter.isEmpty()) {
        targetServers = QStringList{serverFilter};
    }

    m_reporter->addExecutionLogLine(QStringLiteral("Evaluating %1 server(s) for task: %2").arg(targetServers.size()).arg(task));

    std::vector<ToolCandidateScore> allCandidates;
    for (const QString& serverName : targetServers) {
        auto client = m_manager->client(serverName);
        if (!client) {
            m_reporter->addExecutionLogLine(QStringLiteral("Skipping missing client for server %1").arg(serverName));
            continue;
        }

        const auto tools = client->cachedTools();
        if (tools.empty()) {
            m_reporter->addExecutionLogLine(QStringLiteral("Server %1 has no cached tools").arg(serverName));
            continue;
        }

        m_reporter->addObservation(QStringLiteral("tool/discovery"),
                                   QStringLiteral("Loaded %1 tools from %2").arg(tools.size()).arg(serverName));

        auto ranked = m_selector->rankTools(task, serverName, tools);
        allCandidates.insert(allCandidates.end(), ranked.candidates.begin(), ranked.candidates.end());
    }

    if (allCandidates.empty()) {
        finishWithError(QStringLiteral("tool/discovery"),
                        QStringLiteral("No discovered tools were available from the selected servers"),
                        QStringLiteral("Expose a clearer readiness signal before routing"));
        return;
    }

    std::sort(allCandidates.begin(), allCandidates.end(), [](const ToolCandidateScore& a, const ToolCandidateScore& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.namespacedToolName < b.namespacedToolName;
    });

    const int previewCount = std::min<int>(3, allCandidates.size());
    for (int i = 0; i < previewCount; ++i) {
        const auto& candidate = allCandidates[static_cast<size_t>(i)];
        m_reporter->addExecutionLogLine(
            QStringLiteral("Candidate %1: %2 (score=%3, reasons=%4)")
                .arg(i + 1)
                .arg(candidate.namespacedToolName)
                .arg(candidate.score)
                .arg(candidate.reasons.join(QStringLiteral("; "))));
    }

    const ToolCandidateScore best = allCandidates.front();
    if (best.score <= 0) {
        finishWithError(QStringLiteral("tool/selection"),
                        QStringLiteral("No suitable tool scored above zero"),
                        QStringLiteral("Provide richer tool metadata for agent-facing discovery"));
        return;
    }

    QString argError;
    const QJsonObject args = buildSafeArguments(best, task, &argError);
    if (!argError.isEmpty()) {
        finishWithError(QStringLiteral("tool/selection"),
                        argError,
                        QStringLiteral("Add higher-level helpers for safe schema-to-argument mapping"));
        return;
    }

    m_reporter->addExecutionLogLine(QStringLiteral("Selected tool %1 with score %2").arg(best.namespacedToolName).arg(best.score));
    m_reporter->addObservation(QStringLiteral("tool/call"),
                               QStringLiteral("Calling %1").arg(best.namespacedToolName));

    m_router.callToolAsync(best.namespacedToolName, args, [this, best](mcp_qt::McpResult result) {
        if (result.isError) {
            finishWithError(QStringLiteral("tool/call"),
                            QStringLiteral("Tool call failed: %1").arg(result.errorString),
                            QStringLiteral("Surface structured invocation errors rather than empty results"));
            return;
        }
        finishSuccessfully(QStringLiteral("Tool call succeeded for %1").arg(best.namespacedToolName));
    });
}

QJsonObject AgentSession::buildSafeArguments(const ToolCandidateScore& candidate, const QString& task, QString* error) const {
    const QJsonObject properties = candidate.inputSchema.value(QStringLiteral("properties")).toObject();
    const QJsonArray required = candidate.inputSchema.value(QStringLiteral("required")).toArray();
    static const QStringList safeFields{
        QStringLiteral("query"),
        QStringLiteral("q"),
        QStringLiteral("keyword"),
        QStringLiteral("text"),
        QStringLiteral("prompt")
    };

    QJsonObject args;
    for (const QString& field : safeFields) {
        if (properties.contains(field)) {
            args[field] = task;
        }
    }

    for (const auto& requiredValue : required) {
        const QString field = requiredValue.toString();
        if (!args.contains(field)) {
            if (error) {
                *error = QStringLiteral("Cannot safely construct required argument '%1' for %2")
                    .arg(field, candidate.namespacedToolName);
            }
            return QJsonObject{};
        }
    }
    return args;
}

void AgentSession::finishWithError(const QString& stage, const QString& message, const QString& suggestion) {
    m_reporter->addProblem(stage, message, suggestion);
    emit finished(1);
}

void AgentSession::finishSuccessfully(const QString& message) {
    m_reporter->addExecutionLogLine(message);
    m_reporter->addObservation(QStringLiteral("result/render"), message);
    emit finished(0);
}
```

- [ ] **Step 4: Run the session tests to verify they pass**

Run:

```powershell
cmake --build build --target tests_qt
.\build\tests_qt.exe
```

Expected: both `test_multi_server_agent_session_*` tests PASS.

- [ ] **Step 5: Review the diff and leave git untouched**

Run:

```powershell
git diff -- examples/multi_server_agent tests_qt
```

Expected: diff shows session orchestration and tests only. Do not stage or commit.

## Task 4: Integrate The CLI Example End-To-End

**Files:**
- Modify: `examples/multi_server_agent/main.cpp`
- Modify: `examples/multi_server_agent/AgentSession.cpp`
- Modify: `examples/multi_server_agent/DiagnosticReporter.cpp`

- [ ] **Step 1: Write the failing integration behavior as a manual acceptance script**

Document the intended manual behavior in a local scratch note while keeping code unchanged:

```text
Command:
  multi_server_agent.exe --config demo.json --task "search notes about MCP"

    Expected behavior:
      1. parse args
      2. print "Agent Execution Log"
      3. either route one tool call or stop with a selection diagnostic
      4. print "SDK Diagnostic Report"
      5. exit 0 only on a successful call
```

This step is manual because the final glue lives in `main.cpp`, but the logic it depends on is already covered by tests from Tasks 1-3.

- [ ] **Step 2: Run the current executable to verify the behavior is still wrong**

Run:

```powershell
cmake --build build --target multi_server_agent
.\build\examples\multi_server_agent\multi_server_agent.exe
```

Expected: FAIL or print the old demo flow instead of the new CLI contract.

- [ ] **Step 3: Replace the old demo entry point with the CLI agent flow**

Replace `examples/multi_server_agent/main.cpp` with:

```cpp
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>

#include "examples/multi_server_agent/AgentSession.h"
#include "examples/multi_server_agent/DiagnosticReporter.h"
#include "examples/multi_server_agent/HeuristicToolSelector.h"
#include "mcp_qt_client/McpServerManager.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("multi_server_agent"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringList{QStringLiteral("config")}, QStringLiteral("Path to MCP server config"), QStringLiteral("config")});
    parser.addOption({QStringList{QStringLiteral("task")}, QStringLiteral("Task text for the simulated agent"), QStringLiteral("task")});
    parser.addOption({QStringList{QStringLiteral("server")}, QStringLiteral("Restrict execution to one server"), QStringLiteral("server")});
    parser.addOption({QStringList{QStringLiteral("timeout-ms")}, QStringLiteral("Connection timeout in milliseconds"), QStringLiteral("timeout"), QStringLiteral("30000")});
    parser.process(app);

    if (!parser.isSet(QStringLiteral("config")) || !parser.isSet(QStringLiteral("task"))) {
        qCritical().noquote() << "Usage: multi_server_agent --config <file> --task <text> [--server <name>] [--timeout-ms <ms>]";
        return 2;
    }

    mcp_qt::McpServerManager manager;
    DiagnosticReporter reporter;
    HeuristicToolSelector selector;
    AgentSession session(&manager, &selector, &reporter);

    QObject::connect(&session, &AgentSession::finished, &app, [&](int exitCode) {
        qInfo().noquote() << reporter.renderExecutionLog();
        qInfo().noquote() << reporter.renderText();
        app.exit(exitCode);
    });

    AgentRunOptions options;
    options.configPath = parser.value(QStringLiteral("config"));
    options.task = parser.value(QStringLiteral("task"));
    options.serverFilter = parser.value(QStringLiteral("server"));
    options.timeoutMs = parser.value(QStringLiteral("timeout-ms")).toInt();
    session.start(options);

    return app.exec();
}
```

Then improve `DiagnosticReporter::renderText()` to prepend an execution-log-friendly separator:

```cpp
QString DiagnosticReporter::renderText() const {
    QString out = QStringLiteral("SDK Diagnostic Report\n");
    out += QStringLiteral("=====================\n");
    ...
}
```

If `AgentSession::start()` currently routes immediately after `loadConfigFile()`, add a bounded wait using `QTimer::singleShot` plus `clientConnected` bookkeeping before calling `runAgainstCurrentClients()`.

- [ ] **Step 4: Run the executable with and without required args**

Run:

```powershell
.\build\examples\multi_server_agent\multi_server_agent.exe
```

Expected: exits with code `2` and prints usage.

Run:

```powershell
.\build\examples\multi_server_agent\multi_server_agent.exe --config path\to\mcp_servers.json --task "search Qt MCP transport recovery"
```

Expected: prints the new two-section output and exits cleanly with either a successful call or a classified diagnostic failure.

- [ ] **Step 5: Review the diff and leave git untouched**

Run:

```powershell
git diff -- examples/multi_server_agent
```

Expected: diff shows the CLI rewrite only. Do not stage or commit.

## Task 5: Full Verification And Example Smoke Runs

**Files:**
- Modify: `examples/multi_server_agent/AgentSession.cpp` as needed
- Modify: `examples/multi_server_agent/HeuristicToolSelector.cpp` as needed
- Modify: `examples/multi_server_agent/DiagnosticReporter.cpp` as needed

- [ ] **Step 1: Run the full Qt test suite**

Run:

```powershell
cmake --build build --target tests_qt
.\build\tests_qt.exe
```

Expected: all existing Qt tests plus the new multi-server-agent tests PASS.

- [ ] **Step 2: Run one smoke test against a real stdio server**

Run with a known local config:

```powershell
.\build\examples\multi_server_agent\multi_server_agent.exe --config .\mcp_servers.json --task "search notes about MCP reconnection"
```

Expected:

- server connection either succeeds or fails explicitly
- tool discovery output is visible
- one candidate is chosen or a classified selection failure is produced
- `SDK Diagnostic Report` contains stage-specific findings

- [ ] **Step 3: Run one smoke test against an HTTP/SSE server if available**

Run:

```powershell
.\build\examples\multi_server_agent\multi_server_agent.exe --config .\mcp_servers_http.json --task "search Qt MCP transport recovery"
```

Expected: output format matches the stdio run even if the server behavior differs.

- [ ] **Step 4: Tighten any diagnostics that are too vague**

If the smoke runs produce generic messages like `No discovered tools were available`, refine them to name:

- the selected server
- the number of clients registered
- whether cached tools were empty
- whether the call was blocked by unsafe arguments

Example refinement:

```cpp
finishWithError(QStringLiteral("tool/discovery"),
                QStringLiteral("Server '%1' connected but cached tool list stayed empty").arg(serverName),
                QStringLiteral("Expose a synchronous or signal-based readiness API for tool discovery"));
```

- [ ] **Step 5: Final diff review with no staging**

Run:

```powershell
git diff -- examples/multi_server_agent tests_qt CMakeLists.txt
git status --short
```

Expected: only the intended files changed, and nothing has been staged or committed.
