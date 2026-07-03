set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(multi_server_agent_core STATIC
    AgentSession.cpp
    DiagnosticReporter.cpp
    LlmAgentExecutor.cpp
    LlmBackends.cpp
    AgentMainWindow.cpp
    AgentMainWindow.h
)

target_include_directories(multi_server_agent_core PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/client/include
    ${CMAKE_SOURCE_DIR}/src/core/include
)

target_link_libraries(multi_server_agent_core PUBLIC
    mcp_qt_client
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
)

target_compile_features(multi_server_agent_core PUBLIC cxx_std_17)

add_executable(multi_server_agent main.cpp)

target_link_libraries(multi_server_agent PRIVATE
    multi_server_agent_core
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
)

target_compile_features(multi_server_agent PRIVATE cxx_std_17)
