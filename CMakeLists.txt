cmake_minimum_required(VERSION 3.20)
project(nano-agent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Check if submodules are initialized
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/spdlog/CMakeLists.txt")
    message(FATAL_ERROR "The spdlog submodule is missing! Please run: git submodule update --init --recursive")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/googletest/CMakeLists.txt")
    message(FATAL_ERROR "The googletest submodule is missing! Please run: git submodule update --init --recursive")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rd-party/json/CMakeLists.txt")
    message(FATAL_ERROR "The nlohmann/json submodule is missing! Please run: git submodule update --init --recursive")
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    option(ENABLE_ASAN "Enable AddressSanitizer" ON)
else()
    option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
endif()

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer is ENABLED")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# Add submodules
add_subdirectory(3rd-party/spdlog)
add_subdirectory(3rd-party/googletest)
add_subdirectory(3rd-party/json)

find_package(CURL REQUIRED)
# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/cli.cpp
    src/config.cpp
    src/workspace.cpp
    src/logger.cpp
    src/http.cpp
    src/llm.cpp
    src/sse_parser.cpp
    src/tool_call_assembler.cpp
    src/tool_registry.cpp
    src/write_file.cpp
    src/read_file.cpp
    src/bash_tool.cpp
    src/build_test_tools.cpp
    src/repo_tools.cpp
    src/agent_utils.cpp
    src/agent_tools.cpp
    src/skill_loader.cpp
    src/apply_patch.cpp
    src/agent_loop.cpp
    src/mcp.cpp
    src/mcp_transport_stdio.cpp
    src/mcp_tool_bridge.cpp
    src/state.cpp
    src/state_store.cpp
    src/trace.cpp
)

# Main executable
add_executable(agent ${SOURCES})
target_link_libraries(agent PRIVATE spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)

# Enable testing
enable_testing()
add_subdirectory(tests)
