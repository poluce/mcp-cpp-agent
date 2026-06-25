add_library(mcp_qt STATIC
    include/mcp_qt/QtStdioTransport.h
    include/mcp_qt/QtHttpTransport.h
    include/mcp_qt/QtMcpClient.h
    include/mcp_qt/QtMcpToolBridge.h
    src/QtStdioTransport.cpp
    src/QtHttpTransport.cpp
    src/QtMcpClient.cpp
    src/QtMcpToolBridge.cpp
)

target_include_directories(mcp_qt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(mcp_qt PUBLIC
    mcp_core
    Qt6::Core
    Qt6::Network
)
