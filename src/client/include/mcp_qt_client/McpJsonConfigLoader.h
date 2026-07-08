#pragma once
#include <mcp_qt_client/IMcpConfigLoader.h>
#include <QJsonObject>
#include <QString>

namespace mcp_qt {

class McpJsonConfigLoader : public IMcpConfigLoader {
public:
    explicit McpJsonConfigLoader(const QJsonObject& configObj);
    static McpJsonConfigLoader fromFile(const QString& filePath);

    QList<McpServerConfig> load() override;

private:
    QJsonObject m_configObj;
    QString interpolateEnv(const QString& value) const;
};

} // namespace mcp_qt
