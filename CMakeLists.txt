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

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/cli.cpp
    src/config.cpp
    src/workspace.cpp
    src/logger.cpp
)

# Main executable
add_executable(agent ${SOURCES})
target_link_libraries(agent PRIVATE spdlog::spdlog)

# Enable testing
enable_testing()
add_subdirectory(tests)
