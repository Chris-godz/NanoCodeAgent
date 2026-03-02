# Common source files
set(TEST_SOURCES
    ../src/cli.cpp
    ../src/config.cpp
    ../src/workspace.cpp
    ../src/http.cpp
    ../src/llm.cpp
    ../src/sse_parser.cpp
    ../src/tool_call_assembler.cpp
    ../src/write_file.cpp
    ../src/read_file.cpp
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

gtest_discover_tests(test_llm_parse)
gtest_discover_tests(test_http_limits)
gtest_discover_tests(test_sse_parser)
gtest_discover_tests(test_stream_limits)
gtest_discover_tests(test_llm_stream)
gtest_discover_tests(test_toolcall_assembler)
gtest_discover_tests(test_write_file)
gtest_discover_tests(test_read_file)
