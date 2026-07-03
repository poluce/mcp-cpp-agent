#include "examples/multi_server_agent/DiagnosticReporter.h"

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
