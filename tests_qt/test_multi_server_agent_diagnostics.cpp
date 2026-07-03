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
