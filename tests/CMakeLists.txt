# Common source files
set(TEST_SOURCES
    ../src/cli.cpp
    ../src/config.cpp
    ../src/workspace.cpp
    ../src/http.cpp
    ../src/llm.cpp
    ../src/sse_parser.cpp
    ../src/tool_call_assembler.cpp
    ../src/tool_registry.cpp
    ../src/write_file.cpp
    ../src/read_file.cpp
    ../src/bash_tool.cpp
    ../src/build_test_tools.cpp
    ../src/repo_tools.cpp
    ../src/agent_utils.cpp
    ../src/agent_tools.cpp
    ../src/skill_loader.cpp
    ../src/apply_patch.cpp
    ../src/agent_loop.cpp
)

# CLI Test
add_executable(test_cli test_cli.cpp ${TEST_SOURCES})
target_link_libraries(test_cli PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_cli PRIVATE ../include)

# Config precedence Test
add_executable(test_config test_config_precedence.cpp ${TEST_SOURCES})
target_link_libraries(test_config PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_config PRIVATE ../include)

# Workspace Test
add_executable(test_workspace test_workspace.cpp ${TEST_SOURCES})
target_link_libraries(test_workspace PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_workspace PRIVATE ../include)

# Discover and run tests
include(GoogleTest)
gtest_discover_tests(test_cli)
gtest_discover_tests(test_config)
gtest_discover_tests(test_workspace)

# LLM & HTTP Tests
add_executable(test_llm_parse test_llm_parse.cpp ${TEST_SOURCES})
target_link_libraries(test_llm_parse PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_llm_parse PRIVATE ../include)

add_executable(test_http_limits test_http_limits.cpp ${TEST_SOURCES})
target_link_libraries(test_http_limits PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_http_limits PRIVATE ../include)

add_executable(test_sse_parser test_sse_parser.cpp ${TEST_SOURCES})
target_link_libraries(test_sse_parser PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_sse_parser PRIVATE ../include)

add_executable(test_stream_limits test_stream_limits.cpp ${TEST_SOURCES})
target_link_libraries(test_stream_limits PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_stream_limits PRIVATE ../include)

add_executable(test_llm_stream test_llm_stream.cpp ${TEST_SOURCES})
target_link_libraries(test_llm_stream PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_llm_stream PRIVATE ../include)

add_executable(test_toolcall_assembler test_toolcall_assembler.cpp ${TEST_SOURCES})
target_link_libraries(test_toolcall_assembler PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_toolcall_assembler PRIVATE ../include)

add_executable(test_write_file test_write_file.cpp ${TEST_SOURCES})
target_link_libraries(test_write_file PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_write_file PRIVATE ../include)

add_executable(test_read_file test_read_file.cpp ${TEST_SOURCES})
target_link_libraries(test_read_file PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_read_file PRIVATE ../include)

add_executable(test_bash_tool test_bash_tool.cpp ${TEST_SOURCES})
target_link_libraries(test_bash_tool PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_bash_tool PRIVATE ../include)

add_executable(test_build_test_tools test_build_test_tools.cpp ${TEST_SOURCES})
target_link_libraries(test_build_test_tools PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_build_test_tools PRIVATE ../include)

gtest_discover_tests(test_llm_parse)
gtest_discover_tests(test_http_limits)
gtest_discover_tests(test_sse_parser)
gtest_discover_tests(test_stream_limits)
gtest_discover_tests(test_llm_stream)
gtest_discover_tests(test_toolcall_assembler)
gtest_discover_tests(test_write_file)
gtest_discover_tests(test_read_file)
gtest_discover_tests(test_bash_tool)
gtest_discover_tests(test_build_test_tools)

add_executable(test_agent_mock_e2e test_agent_mock_e2e.cpp ${TEST_SOURCES})
target_link_libraries(test_agent_mock_e2e PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_agent_mock_e2e PRIVATE ../include)
gtest_discover_tests(test_agent_mock_e2e)

add_executable(test_agent_loop_limits test_agent_loop_limits.cpp ${TEST_SOURCES})
target_link_libraries(test_agent_loop_limits PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_agent_loop_limits PRIVATE ../include)
gtest_discover_tests(test_agent_loop_limits)

add_executable(test_stream_robustness test_stream_robustness.cpp ${TEST_SOURCES})
target_link_libraries(test_stream_robustness PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_stream_robustness PRIVATE ../include)
gtest_discover_tests(test_stream_robustness)

add_executable(test_schema_and_args_tolerance test_schema_and_args_tolerance.cpp ${TEST_SOURCES})
target_link_libraries(test_schema_and_args_tolerance PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_schema_and_args_tolerance PRIVATE ../include)
gtest_discover_tests(test_schema_and_args_tolerance)

add_executable(test_tool_registry test_tool_registry.cpp ${TEST_SOURCES})
target_link_libraries(test_tool_registry PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_tool_registry PRIVATE ../include)
gtest_discover_tests(test_tool_registry)

add_executable(test_skill_loader test_skill_loader.cpp ${TEST_SOURCES})
target_link_libraries(test_skill_loader PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_skill_loader PRIVATE ../include)
gtest_discover_tests(test_skill_loader)

add_executable(test_repo_tools test_repo_tools.cpp ${TEST_SOURCES})
target_link_libraries(test_repo_tools PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_repo_tools PRIVATE ../include)
gtest_discover_tests(test_repo_tools)

add_executable(test_apply_patch test_apply_patch.cpp ${TEST_SOURCES})
target_link_libraries(test_apply_patch PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_apply_patch PRIVATE ../include)
gtest_discover_tests(test_apply_patch)

add_executable(test_net_smoke test_net_smoke.cpp ${TEST_SOURCES})
target_link_libraries(test_net_smoke PRIVATE GTest::gtest_main spdlog::spdlog CURL::libcurl nlohmann_json::nlohmann_json)
target_include_directories(test_net_smoke PRIVATE ../include)
# Setup net test properties
gtest_discover_tests(test_net_smoke PROPERTIES LABELS "net")
