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
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.namespacedToolName < b.namespacedToolName;
    });

    result.foundMatch = !result.candidates.empty() && result.candidates.front().score > 0;
    if (!result.foundMatch) {
        result.failureReason = QStringLiteral("No candidate tool scored above zero");
    }
    return result;
}
