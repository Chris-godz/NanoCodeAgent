#include <gtest/gtest.h>
#include "agent_tools.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST(SchemaAndArgsToleranceTest, GetSchemaMatchesCurrentTools) {
    json schema = get_agent_tools_schema();
    // String timeout should be accepted by argument parsing.
    EXPECT_EQ(res.find("Invalid 'timeout_ms' argument"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsOutOfRangeInteger) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", 3000000000ULL}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("Invalid 'timeout_ms' argument"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsOutOfRangeString) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", "3000000000"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("Invalid 'timeout_ms' argument"), std::string::npos);
    bool has_read = false;
    bool has_write = false;
    bool has_bash = false;
    bool has_list = false;
    bool has_rg = false;
    bool has_git = false;
    for (const auto& tool : schema) {
        std::string name = tool["function"]["name"];
        if (name == "read_file_safe") has_read = true;
        if (name == "write_file_safe") has_write = true;
        if (name == "bash_execute_safe") has_bash = true;
        if (name == "list_files_bounded") has_list = true;
        if (name == "rg_search") has_rg = true;
        if (name == "git_status") has_git = true;
    }
    EXPECT_TRUE(has_read);
    EXPECT_TRUE(has_write);
    EXPECT_TRUE(has_bash);
    EXPECT_TRUE(has_list);
    EXPECT_TRUE(has_rg);
    EXPECT_TRUE(has_git);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutToleratesStrings) {
    AgentConfig config;
    config.workspace_abs = "."; // Allow mock safe dir
    
    ToolCall tc;
    tc.name = "bash_execute_safe";
    // We explicitly pass timeout_ms as string instead of int
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", "100"} 
    };
    
    std::string res = execute_tool(tc, config);
    // It should not throw and should safely execute
    EXPECT_NE(res.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(res.find("hello"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, MissingArgumentsReturnsErrorGracefully) {
    AgentConfig config;
    config.workspace_abs = "."; 
    
    ToolCall tc;
    tc.name = "read_file_safe";
    // Empty args!
    tc.arguments = json::object();
    
    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("Missing 'path' argument"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, TypeErrorsCaughtSafelyAndDontCrash) {
    AgentConfig config;
    config.workspace_abs = "."; 

    ToolCall tc;
    tc.name = "read_file_safe";
    // Path should be a string, we pass an object
    tc.arguments = {
        {"path", {{"unexpected", "object"}}} 
    };
    
    std::string res = execute_tool(tc, config);
    // Exception handled inside
    EXPECT_NE(res.find("failed"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, ExistingToolsStillDispatchThroughRegistry) {
    const auto test_workspace = std::filesystem::absolute("test_ws_tool_dispatch").string();
    std::filesystem::remove_all(test_workspace);
    std::filesystem::create_directories(test_workspace);

    AgentConfig config;
    config.workspace_abs = test_workspace;

    ToolCall write_call;
    write_call.name = "write_file_safe";
    write_call.arguments = {
        {"path", "hello.txt"},
        {"content", "world"}
    };

    const std::string write_result = execute_tool(write_call, config);
    EXPECT_NE(write_result.find("\"ok\":true"), std::string::npos);

    ToolCall read_call;
    read_call.name = "read_file_safe";
    read_call.arguments = {
        {"path", "hello.txt"}
    };

    const std::string read_result = execute_tool(read_call, config);
    EXPECT_NE(read_result.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(read_result.find("world"), std::string::npos);

    ToolCall bash_call;
    bash_call.name = "bash_execute_safe";
    bash_call.arguments = {
        {"command", "cat hello.txt"}
    };

    const std::string bash_result = execute_tool(bash_call, config);
    EXPECT_NE(bash_result.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(bash_result.find("world"), std::string::npos);

    std::filesystem::remove_all(test_workspace);
}
