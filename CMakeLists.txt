cmake_minimum_required(VERSION 3.16)
project(mcp-cpp-agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automatic compilation of Qt Meta-Object Compiler, User Interface Compiler and Resource Compiler
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Fetch nlohmann/json (extremely lightweight single-header JSON library)
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Find Qt6 Components
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Add subdirectories for components
add_subdirectory(mcp_core)
add_subdirectory(mcp_qt)
add_subdirectory(app)
