#pragma once

#include <QList>
#include <QMap>
#include <QString>

namespace mcp_qt {

enum class McpDiagnosticLevel {
    Info,
    Warning,
    Error
};

struct McpDiagnosticItem {
    McpDiagnosticLevel level;
    QString message;
    QString suggestion;
};

class McpDiagnosticReporter {
public:
    void addExecutionLogLine(const QString& line);
    void addInfo(const QString& stage, const QString& message);
    void addWarning(const QString& stage, const QString& message, const QString& suggestion = QString());
    void addError(const QString& stage, const QString& message, const QString& suggestion = QString());
    QString renderExecutionLog() const;
    QString renderText() const;
    bool hasErrors() const;
    void clear();

private:
    void addItem(const QString& stage, McpDiagnosticLevel level, const QString& message, const QString& suggestion);
    static QString levelToString(McpDiagnosticLevel level);

    QList<QString> m_executionLog;
    QMap<QString, QList<McpDiagnosticItem>> m_itemsByStage;
};

} // namespace mcp_qt
