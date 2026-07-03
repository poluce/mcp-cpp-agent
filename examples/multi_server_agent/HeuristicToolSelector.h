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
