add_executable(mcp_client_conformance
    main.cpp
)

target_include_directories(mcp_client_conformance PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mcp_core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_options(mcp_client_conformance PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(mcp_client_conformance PRIVATE
    mcp_core
    nlohmann_json::nlohmann_json
)
