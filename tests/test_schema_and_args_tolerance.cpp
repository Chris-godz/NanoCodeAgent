#include <gtest/gtest.h>
#include "agent_tools.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <nlohmann/json.hpp>
#include <unistd.h>

using json = nlohmann::json;

namespace {

std::string shell_escape_single_quotes(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

int run_bash(const std::string& command) {
    const std::string wrapped = "bash -lc '" + shell_escape_single_quotes(command) + "'";
    return std::system(wrapped.c_str());
}

} // namespace

TEST(SchemaAndArgsToleranceTest, GetSchemaMatchesCurrentTools) {
    json schema = get_agent_tools_schema();
    EXPECT_EQ(schema.size(), 11u);

    bool has_read        = false;
    bool has_write       = false;
    bool has_bash        = false;
    bool has_build       = false;
    bool has_test        = false;
    bool has_list        = false;
    bool has_rg          = false;
    bool has_git         = false;
    bool has_apply_patch = false;
    bool has_git_diff    = false;
    bool has_git_show    = false;
    for (const auto& tool : schema) {
        std::string name = tool["function"]["name"];
        if (name == "read_file_safe")   has_read        = true;
        if (name == "write_file_safe")  has_write       = true;
        if (name == "bash_execute_safe") has_bash       = true;
        if (name == "build_project_safe") has_build     = true;
        if (name == "test_project_safe") has_test       = true;
        if (name == "list_files_bounded") has_list      = true;
        if (name == "rg_search")        has_rg          = true;
        if (name == "git_status")       has_git         = true;
        if (name == "apply_patch")      has_apply_patch = true;
        if (name == "git_diff")         has_git_diff    = true;
        if (name == "git_show")         has_git_show    = true;
    }
    EXPECT_TRUE(has_read);
    EXPECT_TRUE(has_write);
    EXPECT_TRUE(has_bash);
    EXPECT_TRUE(has_build);
    EXPECT_TRUE(has_test);
    EXPECT_TRUE(has_list);
    EXPECT_TRUE(has_rg);
    EXPECT_TRUE(has_git);
    EXPECT_TRUE(has_apply_patch);
    EXPECT_TRUE(has_git_diff);
    EXPECT_TRUE(has_git_show);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsOutOfRangeInteger) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", 3000000000ULL}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("must be between 1 and"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsOutOfRangeString) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", "3000000000"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("must be between 1 and"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutToleratesStrings) {
    AgentConfig config;
    config.workspace_abs = "."; // Allow mock safe dir
    config.allow_execution_tools = true;
    
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

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsUnsignedOverflow) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", 3000000000ULL}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'timeout_ms' must be between 1 and"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsStringOverflow) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", "3000000000"}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'timeout_ms' must be between 1 and"), std::string::npos);
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
    const auto test_workspace =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_dispatch_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(test_workspace);
    std::filesystem::create_directories(test_workspace);

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;

    ToolCall write_call;
    write_call.name = "write_file_safe";
    write_call.arguments = {
        {"path", "hello.txt"},
        {"content", "world"}
    };

    const std::string write_result = execute_tool(write_call, config);
    const json write_json = json::parse(write_result);
    ASSERT_TRUE(write_json["ok"].get<bool>()) << write_json.dump();

    ToolCall read_call;
    read_call.name = "read_file_safe";
    read_call.arguments = {
        {"path", "hello.txt"}
    };

    const json read_json = json::parse(execute_tool(read_call, config));
    ASSERT_TRUE(read_json["ok"].get<bool>()) << read_json.dump();
    EXPECT_EQ(read_json["content"], "world");

    ToolCall bash_call;
    bash_call.name = "bash_execute_safe";
    bash_call.arguments = {
        {"command", "cat hello.txt"}
    };

    const json bash_json = json::parse(execute_tool(bash_call, config));
    ASSERT_TRUE(bash_json["ok"].get<bool>()) << bash_json.dump();
    EXPECT_NE(bash_json["stdout"].get<std::string>().find("world"), std::string::npos);

    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git add hello.txt >/dev/null 2>&1 &&"
                       " git -c user.email=t@t.com -c user.name=T commit -m init >/dev/null 2>&1 &&"
                       " printf 'planet' > hello.txt"), 0);

    ToolCall diff_call;
    diff_call.name = "git_diff";
    diff_call.arguments = json::object();
    const json diff_result = json::parse(execute_tool(diff_call, config));
    ASSERT_TRUE(diff_result["ok"].get<bool>()) << diff_result.dump();
    EXPECT_NE(diff_result["stdout"].get<std::string>().find("planet"), std::string::npos);

    ToolCall show_call;
    show_call.name = "git_show";
    show_call.arguments = {{"rev", "HEAD"}};
    const json show_result = json::parse(execute_tool(show_call, config));
    ASSERT_TRUE(show_result["ok"].get<bool>()) << show_result.dump();
    EXPECT_NE(show_result["stdout"].get<std::string>().find("init"), std::string::npos);

    ToolCall patch_call;
    patch_call.name = "apply_patch";
    patch_call.arguments = {
        {"path", "hello.txt"},
        {"old_text", "planet"},
        {"new_text", "galaxy"}
    };
    const json patch_result = json::parse(execute_tool(patch_call, config));
    ASSERT_TRUE(patch_result["ok"].get<bool>()) << patch_result.dump();

    std::ifstream patched(std::filesystem::path(test_workspace) / "hello.txt");
    std::string patched_content;
    std::getline(patched, patched_content);
    EXPECT_EQ(patched_content, "galaxy");

    std::filesystem::remove_all(test_workspace);
}

TEST(SchemaAndArgsToleranceTest, BashExecuteSafeBlockedWithoutApprovalHasNoSideEffects) {
    const auto test_workspace =
        (std::filesystem::temp_directory_path() /
         ("nano_bash_blocked_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(test_workspace);
    std::filesystem::create_directories(test_workspace);

    AgentConfig config;
    config.workspace_abs = test_workspace;

    ToolCall bash_call;
    bash_call.name = "bash_execute_safe";
    bash_call.arguments = {
        {"command", "printf 'hi' > blocked.txt"}
    };

    const json result = json::parse(execute_tool(bash_call, config));
    EXPECT_FALSE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(test_workspace) / "blocked.txt"));

    std::filesystem::remove_all(test_workspace);
}

TEST(SchemaAndArgsToleranceTest, WriteFileSafeBlockedWithoutMutatingApprovalHasNoSideEffects) {
    const auto test_workspace =
        (std::filesystem::temp_directory_path() /
         ("nano_write_blocked_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(test_workspace);
    std::filesystem::create_directories(test_workspace);

    AgentConfig config;
    config.workspace_abs = test_workspace;

    ToolCall write_call;
    write_call.name = "write_file_safe";
    write_call.arguments = {
        {"path", "blocked.txt"},
        {"content", "should-not-write"}
    };

    const json result = json::parse(execute_tool(write_call, config));
    EXPECT_FALSE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(test_workspace) / "blocked.txt"));

    std::filesystem::remove_all(test_workspace);
}

TEST(SchemaAndArgsToleranceTest, ApplyPatchBlockedWithoutMutatingApprovalHasNoSideEffects) {
    const auto test_workspace =
        (std::filesystem::temp_directory_path() /
         ("nano_apply_patch_blocked_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(test_workspace);
    std::filesystem::create_directories(test_workspace);

    {
        std::ofstream out(std::filesystem::path(test_workspace) / "hello.txt");
        out << "planet";
    }

    AgentConfig config;
    config.workspace_abs = test_workspace;

    ToolCall patch_call;
    patch_call.name = "apply_patch";
    patch_call.arguments = {
        {"path", "hello.txt"},
        {"old_text", "planet"},
        {"new_text", "galaxy"}
    };

    const json result = json::parse(execute_tool(patch_call, config));
    EXPECT_FALSE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["status"], "blocked");

    std::ifstream in(std::filesystem::path(test_workspace) / "hello.txt");
    std::string content;
    std::getline(in, content);
    EXPECT_EQ(content, "planet");

    std::filesystem::remove_all(test_workspace);
}

TEST(SchemaAndArgsToleranceTest, SchemaIncludesBuildAndTestSafeTools) {
    json schema = get_agent_tools_schema();
    json build_params;
    json test_params;

    for (const auto& tool : schema) {
        const std::string name = tool["function"]["name"];
        if (name == "build_project_safe") {
            build_params = tool["function"]["parameters"];
        }
        if (name == "test_project_safe") {
            test_params = tool["function"]["parameters"];
        }
    }

    ASSERT_TRUE(build_params.is_object());
    ASSERT_TRUE(test_params.is_object());
    EXPECT_EQ(build_params["properties"]["build_mode"]["type"], "string");
    EXPECT_EQ(build_params["properties"]["clean_first"]["type"], "boolean");
    EXPECT_EQ(build_params["properties"]["timeout_ms"]["type"], "integer");
    EXPECT_EQ(build_params["properties"]["max_output_bytes"]["type"], "integer");
    EXPECT_FALSE(build_params["properties"].contains("target"));
    EXPECT_EQ(test_params["properties"]["timeout_ms"]["type"], "integer");
    EXPECT_EQ(test_params["properties"]["max_output_bytes"]["type"], "integer");
    EXPECT_FALSE(test_params["properties"].contains("filter"));
    EXPECT_FALSE(test_params["properties"].contains("ensure_debug_build"));
}

TEST(SchemaAndArgsToleranceTest, BuildProjectSafeRejectsInvalidBuildMode) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "build_project_safe";
    tc.arguments = {{"build_mode", "fast"}};

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'build_mode' must be one of: debug, release."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BuildProjectSafeRejectsInvalidCleanFirstType) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "build_project_safe";
    tc.arguments = {{"clean_first", "true"}};

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'clean_first' must be a boolean."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BuildProjectSafeRejectsUnsupportedTarget) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "build_project_safe";
    tc.arguments = {{"target", "agent"}};

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'target' is not supported by build_project_safe in v1."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, TestProjectSafeRejectsUnsupportedFilter) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "test_project_safe";
    tc.arguments = {{"filter", "Foo*"}};

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'filter' is not supported by test_project_safe in v1."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, TestProjectSafeRejectsEnsureDebugBuild) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "test_project_safe";
    tc.arguments = {{"ensure_debug_build", true}};

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'ensure_debug_build' is not supported"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BuildProjectSafeRejectsTimeoutOverflow) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "build_project_safe";
    tc.arguments = {{"timeout_ms", "3000000000"}};

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'timeout_ms' must be between 1 and"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BuildProjectSafeRejectsHugeOutputLimit) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "build_project_safe";
    tc.arguments = {{"max_output_bytes", 2000000}};

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'max_output_bytes' must be between 1 and 1048576."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BuildProjectSafeRejectsZeroOutputLimit) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "build_project_safe";
    tc.arguments = {{"max_output_bytes", 0}};

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'max_output_bytes' must be between 1 and 1048576."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BuildProjectSafeDispatchReturnsStructuredJson) {
    const auto ws =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_buildsafe_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);

    {
        std::ofstream out(std::filesystem::path(ws) / "build.sh");
        out << "#!/bin/sh\n";
        out << "printf 'build-ok\\n'\n";
        out << "exit 0\n";
    }
    std::filesystem::permissions(std::filesystem::path(ws) / "build.sh",
                                 std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write | std::filesystem::perms::group_exec |
                                     std::filesystem::perms::group_read | std::filesystem::perms::others_exec |
                                     std::filesystem::perms::others_read);

    AgentConfig config;
    config.workspace_abs = ws;
    config.allow_execution_tools = true;

    ToolCall tc;
    tc.name = "build_project_safe";
    tc.arguments = json::object();

    const json result = json::parse(execute_tool(tc, config));
    EXPECT_TRUE(result.contains("ok"));
    EXPECT_TRUE(result.contains("status"));
    EXPECT_TRUE(result.contains("exit_code"));
    EXPECT_TRUE(result.contains("stdout"));
    EXPECT_TRUE(result.contains("stderr"));
    EXPECT_TRUE(result.contains("truncated"));
    EXPECT_TRUE(result.contains("timed_out"));
    EXPECT_TRUE(result.contains("summary"));

    std::filesystem::remove_all(ws);
}

// ---------------------------------------------------------------------------
// apply_patch schema: oneOf enforcement
// ---------------------------------------------------------------------------

/// Helper: extract the apply_patch parameters schema from the OpenAI schema
/// array returned by get_agent_tools_schema().
static json get_apply_patch_params() {
    json schema = get_agent_tools_schema();
    for (const auto& tool : schema) {
        if (tool["function"]["name"] == "apply_patch") {
            return tool["function"]["parameters"];
        }
    }
    ADD_FAILURE() << "apply_patch not found in tool schema";
    return {};
}

TEST(ApplyPatchSchemaTest, HasOneOfWithTwoModes) {
    const json params = get_apply_patch_params();
    ASSERT_TRUE(params.contains("oneOf"));
    ASSERT_TRUE(params["oneOf"].is_array());
    EXPECT_EQ(params["oneOf"].size(), 2u);
}

TEST(ApplyPatchSchemaTest, ModeASingleRequiresPathOldTextNewText) {
    const json params = get_apply_patch_params();
    ASSERT_TRUE(params.contains("oneOf"));
    ASSERT_TRUE(params["oneOf"].is_array());
    ASSERT_GE(params["oneOf"].size(), 2u);
    const json& mode_a = params["oneOf"][0];
    ASSERT_TRUE(mode_a.contains("required"));
    const json expected = json::array({"path", "old_text", "new_text"});
    EXPECT_EQ(mode_a["required"], expected);
}

TEST(ApplyPatchSchemaTest, ModeBBatchRequiresPathPatches) {
    const json params = get_apply_patch_params();
    ASSERT_TRUE(params.contains("oneOf"));
    ASSERT_TRUE(params["oneOf"].is_array());
    ASSERT_GE(params["oneOf"].size(), 2u);
    const json& mode_b = params["oneOf"][1];
    ASSERT_TRUE(mode_b.contains("required"));
    const json expected = json::array({"path", "patches"});
    EXPECT_EQ(mode_b["required"], expected);
}

TEST(ApplyPatchSchemaTest, PatchesItemsRequireOldTextAndNewText) {
    const json params = get_apply_patch_params();
    ASSERT_TRUE(params.contains("properties"));
    ASSERT_TRUE(params["properties"].contains("patches"));
    const json& patches = params["properties"]["patches"];
    ASSERT_TRUE(patches.contains("items"));
    const json& items = patches["items"];
    ASSERT_TRUE(items.contains("required"));
    const json expected = json::array({"old_text", "new_text"});
    EXPECT_EQ(items["required"], expected);
}

TEST(ApplyPatchSchemaTest, TopLevelDoesNotHaveBareRequired) {
    // Before the fix the schema had a top-level "required": ["path"].
    // After the fix, the required constraints live inside oneOf only.
    const json params = get_apply_patch_params();
    EXPECT_FALSE(params.contains("required"))
        << "Top-level 'required' should not exist; constraints belong in oneOf";
}

// ---------------------------------------------------------------------------
// apply_patch schema: mixed-mode rejection via additionalProperties
// ---------------------------------------------------------------------------

TEST(ApplyPatchSchemaTest, SchemaModeSingleHasAdditionalPropertiesFalse) {
    const json params = get_apply_patch_params();
    const json& branch = params["oneOf"][0];
    ASSERT_TRUE(branch.contains("additionalProperties"))
        << "Single-mode branch must declare additionalProperties";
    EXPECT_EQ(branch["additionalProperties"], false);
}

TEST(ApplyPatchSchemaTest, SchemaModeBatchHasAdditionalPropertiesFalse) {
    const json params = get_apply_patch_params();
    const json& branch = params["oneOf"][1];
    ASSERT_TRUE(branch.contains("additionalProperties"))
        << "Batch-mode branch must declare additionalProperties";
    EXPECT_EQ(branch["additionalProperties"], false);
}

TEST(ApplyPatchSchemaTest, SchemaModeSingleAllowsOnlyPathOldTextNewText) {
    const json params = get_apply_patch_params();
    const json& props = params["oneOf"][0]["properties"];
    ASSERT_TRUE(props.is_object());
    std::set<std::string> keys;
    for (auto it = props.begin(); it != props.end(); ++it) keys.insert(it.key());
    const std::set<std::string> expected{"path", "old_text", "new_text"};
    EXPECT_EQ(keys, expected)
        << "Single-mode branch properties must be exactly {path, old_text, new_text}";
}

TEST(ApplyPatchSchemaTest, SchemaModeBatchAllowsOnlyPathPatches) {
    const json params = get_apply_patch_params();
    const json& props = params["oneOf"][1]["properties"];
    ASSERT_TRUE(props.is_object());
    std::set<std::string> keys;
    for (auto it = props.begin(); it != props.end(); ++it) keys.insert(it.key());
    const std::set<std::string> expected{"path", "patches"};
    EXPECT_EQ(keys, expected)
        << "Batch-mode branch properties must be exactly {path, patches}";
}

// ---------------------------------------------------------------------------
// apply_patch runtime: argument combination validation
// ---------------------------------------------------------------------------

TEST(ApplyPatchSchemaTest, RuntimeRejectsPathOnly) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_mutating_tools = true;

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {{"path", "any.txt"}};

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Passing only 'path' should fail at runtime";
}

TEST(ApplyPatchSchemaTest, RuntimeAcceptsSingleMode) {
    const auto ws =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_single_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);

    // Seed a file via write_file_safe
    AgentConfig config;
    config.workspace_abs = ws;
    config.allow_mutating_tools = true;

    ToolCall write_tc;
    write_tc.name = "write_file_safe";
    write_tc.arguments = {{"path", "f.txt"}, {"content", "hello world"}};
    execute_tool(write_tc, config);

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "world"},
        {"new_text", "earth"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":true"), std::string::npos);

    std::filesystem::remove_all(ws);
}

TEST(ApplyPatchSchemaTest, RuntimeAcceptsBatchMode) {
    const auto ws =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_batch_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);

    AgentConfig config;
    config.workspace_abs = ws;
    config.allow_mutating_tools = true;

    ToolCall write_tc;
    write_tc.name = "write_file_safe";
    write_tc.arguments = {{"path", "f.txt"}, {"content", "aaa bbb"}};
    execute_tool(write_tc, config);

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({
            {{"old_text", "aaa"}, {"new_text", "xxx"}},
            {{"old_text", "bbb"}, {"new_text", "yyy"}}
        })}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":true"), std::string::npos);

    std::filesystem::remove_all(ws);
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsMixedPatchesAndOldText) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_mutating_tools = true;

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({json{{"old_text", "a"}, {"new_text", "b"}}})},
        {"old_text", "x"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Mixed patches + old_text must be rejected";
    EXPECT_NE(res.find("Ambiguous"), std::string::npos)
        << "Error should mention ambiguous input";
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsMixedPatchesAndNewText) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_mutating_tools = true;

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({json{{"old_text", "a"}, {"new_text", "b"}}})},
        {"new_text", "y"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Mixed patches + new_text must be rejected";
    EXPECT_NE(res.find("Ambiguous"), std::string::npos);
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsAllThreeFieldsCombined) {
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_mutating_tools = true;

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "x"},
        {"new_text", "y"},
        {"patches", json::array({json{{"old_text", "a"}, {"new_text", "b"}}})}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "All three fields combined must be rejected";
    EXPECT_NE(res.find("Ambiguous"), std::string::npos);
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsBatchEntryMissingNewText) {
    const auto ws =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_miss_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);

    AgentConfig config;
    config.workspace_abs = ws;
    config.allow_mutating_tools = true;

    ToolCall write_tc;
    write_tc.name = "write_file_safe";
    write_tc.arguments = {{"path", "f.txt"}, {"content", "hello"}};
    execute_tool(write_tc, config);

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({
            {{"old_text", "hello"}}
        })}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Batch entry missing 'new_text' should fail";

    std::filesystem::remove_all(ws);
}
// ---------------------------------------------------------------------------
// apply_patch runtime: unknown top-level field rejection
// (schema declares additionalProperties: false; runtime must match)
// ---------------------------------------------------------------------------

TEST(ApplyPatchSchemaTest, RuntimeRejectsUnknownTopLevelFieldSingleMode) {
    // No real file needed: the unknown-field check fires before any I/O.
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_mutating_tools = true;

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "hello"},
        {"new_text", "world"},
        {"typo_field", "x"}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Unknown field in single-mode call must be rejected";
    EXPECT_NE(res.find("typo_field"), std::string::npos)
        << "Error must name the offending field";
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsUnknownTopLevelFieldBatchMode) {
    // No real file needed: the unknown-field check fires before any I/O.
    AgentConfig config;
    config.workspace_abs = ".";
    config.allow_mutating_tools = true;

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({
            {{"old_text", "hello"}, {"new_text", "world"}}
        })},
        {"extra_key", 42}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Unknown field in batch-mode call must be rejected";
    EXPECT_NE(res.find("extra_key"), std::string::npos)
        << "Error must name the offending field";
}

// ---------------------------------------------------------------------------
// git_diff / git_show: schema and dispatch
// ---------------------------------------------------------------------------

TEST(SchemaAndArgsToleranceTest, SchemaIncludesGitDiffAndGitShow) {
    json schema = get_agent_tools_schema();
    bool has_git_diff = false;
    bool has_git_show = false;
    json git_diff_params;
    json git_show_params;
    for (const auto& tool : schema) {
        const std::string name = tool["function"]["name"];
        if (name == "git_diff") {
            has_git_diff = true;
            git_diff_params = tool["function"]["parameters"];
        }
        if (name == "git_show") {
            has_git_show = true;
            git_show_params = tool["function"]["parameters"];
        }
    }
    EXPECT_TRUE(has_git_diff) << "git_diff must appear in tool schema";
    EXPECT_TRUE(has_git_show) << "git_show must appear in tool schema";
    ASSERT_TRUE(git_diff_params.is_object());
    ASSERT_TRUE(git_show_params.is_object());
    EXPECT_EQ(git_diff_params["properties"]["cached"]["type"], "boolean");
    EXPECT_EQ(git_diff_params["properties"]["pathspecs"]["type"], "array");
    EXPECT_EQ(git_diff_params["properties"]["pathspecs"]["items"]["type"], "string");
    EXPECT_EQ(git_show_params["properties"]["patch"]["type"], "boolean");
    EXPECT_EQ(git_show_params["properties"]["stat"]["type"], "boolean");
    EXPECT_EQ(git_show_params["required"], json::array({"rev"}));
}

TEST(SchemaAndArgsToleranceTest, ExecuteToolDispatchesGitDiffAndGitShow) {
    const auto ws =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_gitdiff_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);

    // Initialise a minimal git repo so calls do not fail on missing-repo.
    ASSERT_EQ(run_bash("cd '" + ws + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " touch .keep && git add . >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1"), 0);

    AgentConfig config;
    config.workspace_abs = ws;

    // git_diff: expect a well-formed JSON response (not an "unknown tool" error).
    ToolCall diff_call;
    diff_call.name = "git_diff";
    diff_call.arguments = json::object();
    const std::string diff_res = execute_tool(diff_call, config);
    const json diff_json = json::parse(diff_res);
    EXPECT_TRUE(diff_json.contains("ok"))
        << "git_diff dispatch must return JSON with 'ok' field; got: " << diff_res;

    // git_show with a valid rev.
    ToolCall show_call;
    show_call.name = "git_show";
    show_call.arguments = {{"rev", "HEAD"}};
    const std::string show_res = execute_tool(show_call, config);
    const json show_json = json::parse(show_res);
    EXPECT_TRUE(show_json.contains("ok"))
        << "git_show dispatch must return JSON with 'ok' field; got: " << show_res;

    std::filesystem::remove_all(ws);
}

TEST(SchemaAndArgsToleranceTest, GitDiffClampsHugeContextLines) {
    AgentConfig config;
    const std::string ws =
        (std::filesystem::temp_directory_path() /
         ("nano_git_diff_clamp_test_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);
    config.workspace_abs = ws;

    ASSERT_EQ(run_bash("cd '" + ws + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'one\\ntwo\\nthree\\nfour\\nfive\\n' > sample.txt &&"
                       " git add sample.txt >/dev/null 2>&1 && git commit -m init >/dev/null 2>&1 &&"
                       " printf 'one\\nTWO\\nthree\\nfour\\nfive\\n' > sample.txt"), 0);

    ToolCall huge;
    huge.name = "git_diff";
    huge.arguments = {
        {"context_lines", 1000000}
    };

    ToolCall capped;
    capped.name = "git_diff";
    capped.arguments = {
        {"context_lines", 1000}
    };

    const std::string huge_res = execute_tool(huge, config);
    const std::string capped_res = execute_tool(capped, config);
    EXPECT_EQ(nlohmann::json::parse(huge_res), nlohmann::json::parse(capped_res));

    std::filesystem::remove_all(ws);
}

TEST(SchemaAndArgsToleranceTest, GitShowClampsHugeContextLines) {
    AgentConfig config;
    const std::string ws =
        (std::filesystem::temp_directory_path() /
         ("nano_git_show_clamp_test_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);
    config.workspace_abs = ws;

    ASSERT_EQ(run_bash("cd '" + ws + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'one\\ntwo\\nthree\\nfour\\nfive\\n' > sample.txt &&"
                       " git add sample.txt >/dev/null 2>&1 && git commit -m init >/dev/null 2>&1 &&"
                       " printf 'one\\nTWO\\nthree\\nfour\\nfive\\n' > sample.txt &&"
                       " git add sample.txt >/dev/null 2>&1 && git commit -m second >/dev/null 2>&1"), 0);

    ToolCall huge;
    huge.name = "git_show";
    huge.arguments = {
        {"rev", "HEAD"},
        {"context_lines", 1000000},
        {"stat", false}
    };

    ToolCall capped;
    capped.name = "git_show";
    capped.arguments = {
        {"rev", "HEAD"},
        {"context_lines", 1000},
        {"stat", false}
    };

    const std::string huge_res = execute_tool(huge, config);
    const std::string capped_res = execute_tool(capped, config);
    EXPECT_EQ(nlohmann::json::parse(huge_res), nlohmann::json::parse(capped_res));

    std::filesystem::remove_all(ws);
}

TEST(SchemaAndArgsToleranceTest, GitDiffRejectsInvalidCachedType) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "git_diff";
    tc.arguments = {
        {"cached", "true"}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'cached' must be a boolean."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, GitShowRejectsInvalidPatchType) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "git_show";
    tc.arguments = {
        {"rev", "HEAD"},
        {"patch", "false"}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'patch' must be a boolean."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, GitDiffRejectsInvalidPathspecElementType) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "git_diff";
    tc.arguments = {
        {"pathspecs", json::array({1})}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'pathspecs' must be an array of strings."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, GitDiffRejectsNonArrayPathspecs) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "git_diff";
    tc.arguments = {
        {"pathspecs", "not-an-array"}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'pathspecs' must be an array of strings."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, GitShowRejectsNonArrayPathspecs) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "git_show";
    tc.arguments = {
        {"rev", "HEAD"},
        {"pathspecs", "not-an-array"}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'pathspecs' must be an array of strings."), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, GitShowRejectsInvalidPathspecElementType) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "git_show";
    tc.arguments = {
        {"rev", "HEAD"},
        {"pathspecs", json::array({1})}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'pathspecs' must be an array of strings."), std::string::npos);
}
