add_executable(mcp_client_conformance
    main.cpp
)

target_include_directories(mcp_client_conformance PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mcp_core/include
)

target_link_libraries(mcp_client_conformance PRIVATE
    mcp_core
)
