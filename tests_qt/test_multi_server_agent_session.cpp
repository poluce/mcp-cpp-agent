#include "tests/common.h"
#include "examples/multi_server_agent/AgentSession.h"
#include "examples/multi_server_agent/DiagnosticReporter.h"
#include "examples/multi_server_agent/HeuristicToolSelector.h"
#include "mcp_qt_client/McpServerManager.h"

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
