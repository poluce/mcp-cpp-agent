add_executable(mcp_conformance_runner_tests
    ${CMAKE_SOURCE_DIR}/tests/common.cpp
    ${CMAKE_SOURCE_DIR}/tests/main_standalone_conformance.cpp
    ${CMAKE_SOURCE_DIR}/conformance_runner/RunnerConfig.cpp
    ${CMAKE_SOURCE_DIR}/conformance_runner/ScenarioContext.cpp
    ${CMAKE_SOURCE_DIR}/conformance_runner/ScenarioRegistry.cpp
    ${CMAKE_SOURCE_DIR}/conformance_runner/ScenarioHandlersBasic.cpp
    ${CMAKE_SOURCE_DIR}/conformance_runner/ScenarioHandlersAuth.cpp
    ${CMAKE_SOURCE_DIR}/conformance_runner/ScenarioHandlersList.cpp
    test_runner_config.cpp
    test_scenario_registry.cpp
    test_runner_process.cpp
)

target_include_directories(mcp_conformance_runner_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/conformance_runner
    ${CMAKE_SOURCE_DIR}/mcp_core/include
)

target_link_libraries(mcp_conformance_runner_tests PRIVATE
    mcp_core
    nlohmann_json::nlohmann_json
)
