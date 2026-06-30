add_executable(anysearch_qt main.cpp)

target_include_directories(anysearch_qt PRIVATE
    ${CMAKE_SOURCE_DIR}/mcp_qt_client/include
    ${CMAKE_SOURCE_DIR}/mcp_core/include
)

target_link_libraries(anysearch_qt PRIVATE
    mcp_qt_client
    Qt6::Core
    Qt6::Network
)

target_compile_features(anysearch_qt PRIVATE cxx_std_17)
