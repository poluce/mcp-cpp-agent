add_executable(mcp_client_conformance
    main.cpp
    ../tests/common.cpp
    ../tests/protocol/test_initialize.cpp
    ../tests/protocol/test_json_rpc.cpp
    ../tests/protocol/test_capabilities.cpp
    ../tests/protocol/test_error_response.cpp
    ../tests/transport/test_stdio_transport.cpp
    ../tests/transport/test_http_transport.cpp
    ../tests/transport/test_process_lifecycle.cpp
    ../tests/features/test_tools.cpp
    ../tests/features/test_resources.cpp
    ../tests/features/test_prompts.cpp
    ../tests/features/test_notifications.cpp
    ../tests/integration/test_with_filesystem_server.cpp
    ../tests/integration/test_with_anysearch_mcp.cpp
    ../tests/integration/test_with_inspector_cases.cpp
    ../tests/main.cpp
)

target_include_directories(mcp_client_conformance PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mcp_core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(mcp_client_conformance PRIVATE
    mcp_core
)
