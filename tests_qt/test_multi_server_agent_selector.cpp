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
