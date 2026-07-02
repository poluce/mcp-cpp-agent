set(CMAKE_AUTOMOC ON)

add_executable(multi_server_agent main.cpp)

target_include_directories(multi_server_agent PRIVATE
    ${CMAKE_SOURCE_DIR}/mcp_qt_client/include
    ${CMAKE_SOURCE_DIR}/mcp_core/include
)

target_link_libraries(multi_server_agent PRIVATE
    mcp_qt_client
    Qt6::Core
    Qt6::Network
)

target_compile_features(multi_server_agent PRIVATE cxx_std_17)
