#include "mcp_qt_client/McpDiagnosticReporter.h"

namespace mcp_qt {

void McpDiagnosticReporter::addExecutionLogLine(const QString& line) {
    m_executionLog.append(line);
}

void McpDiagnosticReporter::addInfo(const QString& stage, const QString& message) {
    addItem(stage, McpDiagnosticLevel::Info, message, QString());
}

void McpDiagnosticReporter::addWarning(const QString& stage, const QString& message, const QString& suggestion) {
    addItem(stage, McpDiagnosticLevel::Warning, message, suggestion);
}

void McpDiagnosticReporter::addError(const QString& stage, const QString& message, const QString& suggestion) {
    addItem(stage, McpDiagnosticLevel::Error, message, suggestion);
}

void McpDiagnosticReporter::addItem(const QString& stage, McpDiagnosticLevel level, const QString& message, const QString& suggestion) {
    m_itemsByStage[stage].append(McpDiagnosticItem{level, message, suggestion});
}

QString McpDiagnosticReporter::renderExecutionLog() const {
    QString out = QStringLiteral("MCP Execution Log\n");
    out += QStringLiteral("=================\n");
    for (const auto& line : m_executionLog) {
        out += QStringLiteral("- ") + line + QStringLiteral("\n");
    }
    return out;
}

QString McpDiagnosticReporter::renderText() const {
    QString out = QStringLiteral("MCP Diagnostic Report\n");
    out += QStringLiteral("=====================\n");
    for (auto it = m_itemsByStage.constBegin(); it != m_itemsByStage.constEnd(); ++it) {
        out += QStringLiteral("\n[") + it.key() + QStringLiteral("]\n");
        for (const auto& item : it.value()) {
            out += QStringLiteral("- ") + levelToString(item.level) + QStringLiteral(": ") + item.message + QStringLiteral("\n");
            if (!item.suggestion.isEmpty()) {
                out += QStringLiteral("  suggestion: ") + item.suggestion + QStringLiteral("\n");
            }
        }
    }
    return out;
}

QString McpDiagnosticReporter::levelToString(McpDiagnosticLevel level) {
    switch (level) {
        case McpDiagnosticLevel::Info:    return QStringLiteral("INFO");
        case McpDiagnosticLevel::Warning: return QStringLiteral("WARNING");
        case McpDiagnosticLevel::Error:   return QStringLiteral("ERROR");
    }
    return QStringLiteral("UNKNOWN");
}

bool McpDiagnosticReporter::hasErrors() const {
    for (auto it = m_itemsByStage.constBegin(); it != m_itemsByStage.constEnd(); ++it) {
        for (const auto& item : it.value()) {
            if (item.level == McpDiagnosticLevel::Error) {
                return true;
            }
        }
    }
    return false;
}

void McpDiagnosticReporter::clear() {
    m_executionLog.clear();
    m_itemsByStage.clear();
}

} // namespace mcp_qt
