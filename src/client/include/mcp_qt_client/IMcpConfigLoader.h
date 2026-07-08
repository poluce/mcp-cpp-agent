#pragma once
#include <QList>
#include <mcp_qt_client/McpServerConfig.h>

namespace mcp_qt {

class IMcpConfigLoader {
public:
    virtual ~IMcpConfigLoader() = default;
    virtual QList<McpServerConfig> load() = 0;
};

} // namespace mcp_qt
