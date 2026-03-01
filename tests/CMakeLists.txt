# Common source files
set(TEST_SOURCES
    ../src/cli.cpp
    ../src/config.cpp
    ../src/workspace.cpp
)

# CLI Test
add_executable(test_cli test_cli.cpp ${TEST_SOURCES})
target_link_libraries(test_cli PRIVATE GTest::gtest_main spdlog::spdlog)
target_include_directories(test_cli PRIVATE ../include)

# Config precedence Test
add_executable(test_config test_config_precedence.cpp ${TEST_SOURCES})
target_link_libraries(test_config PRIVATE GTest::gtest_main spdlog::spdlog)
target_include_directories(test_config PRIVATE ../include)

# Workspace Test
add_executable(test_workspace test_workspace.cpp ${TEST_SOURCES})
target_link_libraries(test_workspace PRIVATE GTest::gtest_main spdlog::spdlog)
target_include_directories(test_workspace PRIVATE ../include)

# Discover and run tests
include(GoogleTest)
gtest_discover_tests(test_cli)
gtest_discover_tests(test_config)
gtest_discover_tests(test_workspace)
