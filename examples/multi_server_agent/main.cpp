#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <iostream>

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
        std::cerr << "Usage: multi_server_agent --config <file> --task <text> [--server <name>] [--timeout-ms <ms>]\n";
        return 2;
    }

    mcp_qt::McpServerManager manager;
    DiagnosticReporter reporter;
    HeuristicToolSelector selector;
    AgentSession session(&manager, &selector, &reporter);

    QObject::connect(&session, &AgentSession::finished, &app, [&](int exitCode) {
        std::cerr << "[main] finished signal received, exitCode=" << exitCode << std::endl;
        std::cout << reporter.renderExecutionLog().toStdString() << std::flush;
        std::cout << reporter.renderText().toStdString() << std::flush;
        std::cerr << "[main] exiting via std::exit" << std::endl;
        std::exit(exitCode);
    });

    AgentRunOptions options;
    options.configPath = parser.value(QStringLiteral("config"));
    options.task = parser.value(QStringLiteral("task"));
    options.serverFilter = parser.value(QStringLiteral("server"));
    options.timeoutMs = parser.value(QStringLiteral("timeout-ms")).toInt();
    QTimer::singleShot(0, &session, [options, &session]() {
        session.start(options);
    });

    return app.exec();
}
