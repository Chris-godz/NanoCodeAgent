---
description: "Use when writing, editing, or reviewing C++ source files, headers, CMakeLists, or build scripts. Covers TDD with GoogleTest, AddressSanitizer, C++23 standards, spdlog logging, dependency management via submodules, and build automation with build.sh."
applyTo: "**/*.{cpp,hpp,h,c,cxx,cmake,sh}"
---
# C++ Engineering Rules & Skills

When acting as an AI coding assistant or agent in this repository, you must strictly adhere to the following engineering skills and rules:

## 1. Test-Driven Development (TDD)
- **Skill**: Always write tests before implementing features.
- **Framework**: Use GoogleTest (`gtest`).
- **Location**: Place tests in the `tests/` directory, mirroring the `src/` structure (e.g., `src/cli.cpp` -> `tests/test_cli.cpp`).
- **Integration**: Use `gtest_discover_tests()` in `tests/CMakeLists.txt`.
- **State Management**: Reset global states (like `getopt_long`'s `optind`) in the `SetUp()` method of test fixtures.

## 2. Memory Safety & Leak Detection
- **Skill**: Ensure zero memory leaks in all C++ code.
- **Tool**: AddressSanitizer (ASan).
- **Configuration**: The root `CMakeLists.txt` must include `-fsanitize=address -g` in both `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS`.
- **Validation**: All tests must pass with ASan enabled without any warnings or crashes.

## 3. Dependency Management
- **Skill**: Manage third-party libraries strictly via Git Submodules.
- **Location**: All dependencies must reside in the `3rd-party/` directory.
- **CMake Rules**: 
  - Use `add_subdirectory()` to include dependencies.
  - Must include a pre-check in `CMakeLists.txt` using `if(NOT EXISTS ...)` to verify submodule initialization, failing with a `FATAL_ERROR` if missing.

## 4. Modern C++ Standards
- **Skill**: Write modern, standard-compliant C++ code.
- **Standard**: Strictly use **C++23**.
- **Compiler Extensions**: Disable compiler-specific extensions (`set(CMAKE_CXX_EXTENSIONS OFF)`) to ensure cross-platform compatibility.

## 5. Logging Standards
- **Skill**: Use structured logging instead of standard output.
- **Framework**: Use `spdlog`.
- **Rules**: 
  - Prohibit the use of `std::cout` or `printf` for business logic (except for CLI `--help` and `--version`).
  - Use the wrapper macros defined in `include/logger.hpp` (`LOG_INFO`, `LOG_DEBUG`, `LOG_ERROR`).
  - Support dynamic log level switching (e.g., via `--debug` CLI flag).

## 6. Build & Test Automation
- **Skill**: Use the provided `build.sh` script for all build and test operations instead of manual CMake commands.
- **Commands**:
  - `./build.sh debug` (or just `./build.sh`): Build the project in Debug mode.
  - `./build.sh release`: Build the project in Release mode.
  - `./build.sh test`: Automatically build in Debug mode and run the CTest suite.
  - `./build.sh clean`: Remove the build directory.
- **Rule**: When asked to build, compile, or test the project, ALWAYS use `./build.sh <command>`. Do not write raw `mkdir build && cd build && cmake ..` commands.
