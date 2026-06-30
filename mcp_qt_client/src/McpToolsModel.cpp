#include "mcp_qt_client/McpToolsModel.h"
#include "mcp_qt_client/McpQtClient.h"

namespace mcp_qt {

McpToolsModel::McpToolsModel(QObject* parent)
    : QAbstractListModel(parent) {}

void McpToolsModel::setClient(McpQtClient* client) {
    if (m_client == client) return;
    m_client = client;

    if (!m_client) return;

    // 监听 notifications/tools/list-changed 以实现自动刷新
    // 注意：这仅在 client 已经初始化后才能注册成功
    m_client->registerNotificationHandler(
        "notifications/tools/list-changed",
        this,                          // 以 this 为上下文，保护生命周期
        [this](const QJsonObject&) {
            refresh();                 // 通知到达时自动刷新
        });
}

void McpToolsModel::refresh() {
    if (!m_client) {
        emit refreshError("No client set. Call setClient() before refresh().");
        return;
    }

    const auto tools = m_client->fetchAllTools();

    // 数据比对防 Churn 优化
    bool changed = false;
    if (m_tools.size() != static_cast<int>(tools.size())) {
        changed = true;
    } else {
        for (size_t i = 0; i < tools.size(); ++i) {
            const auto& t = tools[i];
            const auto& existing = m_tools[static_cast<int>(i)];
            if (existing.name != t.name ||
                existing.description != t.description ||
                existing.inputSchema != t.inputSchema) {
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        return; // 工具列表无变化，直接返回，避免重置 View
    }

    beginResetModel();
    m_tools.clear();
    for (const auto& t : tools) {
        m_tools.append({t.name, t.description, t.inputSchema});
    }
    endResetModel();

    emit countChanged();
}

int McpToolsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_tools.size();
}

QVariant McpToolsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tools.size()) {
        return {};
    }
    const ToolEntry& entry = m_tools.at(index.row());
    switch (role) {
    case NameRole:
        return entry.name;
    case DescriptionRole:
        return entry.description;
    case InputSchemaRole:
        return entry.inputSchema;
    case Qt::DisplayRole:
        return entry.name; // 默认显示工具名
    default:
        return {};
    }
}

QHash<int, QByteArray> McpToolsModel::roleNames() const {
    return {
        {NameRole,        "name"},
        {DescriptionRole, "description"},
        {InputSchemaRole, "inputSchema"},
    };
}

} // namespace mcp_qt
