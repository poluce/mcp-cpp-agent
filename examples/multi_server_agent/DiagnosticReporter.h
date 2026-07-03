#pragma once

#include <QList>
#include <QMap>
#include <QString>

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
