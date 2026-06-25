add_library(mcp_core STATIC
    src/JsonRpcDispatcher.cpp
    src/McpClientSession.cpp
)

target_include_directories(mcp_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(mcp_core PUBLIC
    nlohmann_json::nlohmann_json
)
