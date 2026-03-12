#include <gtest/gtest.h>

#include "agent_tools.hpp"
#include "build_test_tools.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

#include "tool_registry.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const char* value)
        : name_(name), had_original_(std::getenv(name) != nullptr) {
        if (had_original_) {
            original_value_ = std::getenv(name);
        }
        const int rc = setenv(name_, value, 1);
        EXPECT_EQ(rc, 0);
    }

    ~ScopedEnvVar() {
        if (had_original_) {
            setenv(name_, original_value_.c_str(), 1);
            return;
        }
        unsetenv(name_);
    }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
    const char* name_;
    bool had_original_ = false;
    std::string original_value_;
};

class BuildTestToolsTest : public ::testing::Test {
protected:
    std::string workspace_;

    void SetUp() override {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        workspace_ = (fs::temp_directory_path() / ("nano_build_test_tools_" + std::to_string(tick))).string();
        fs::create_directories(workspace_);
    }

    void TearDown() override {
        fs::remove_all(workspace_);
    }

    void write_build_script(const std::string& body) {
        const fs::path path = fs::path(workspace_) / "build.sh";
        std::ofstream out(path, std::ios::binary);
        out << "#!/bin/sh\n";
        out << body;
        out.close();
        fs::permissions(path,
                        fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                            fs::perms::group_exec | fs::perms::group_read |
                            fs::perms::others_exec | fs::perms::others_read);
    }

    AgentConfig approved_config() const {
        AgentConfig config;
        config.workspace_abs = workspace_;
        config.allow_execution_tools = true;
        return config;
    }
};

TEST(ParseCTestSummaryTest, ParsesCountsAndFailedTests) {
    const auto parsed = parse_ctest_summary(
        "50% tests passed, 1 tests failed out of 2\nThe following tests FAILED:\n\t1 - alpha.case (Failed)\n",
        "");

    ASSERT_TRUE(parsed.passed_count.has_value());
    ASSERT_TRUE(parsed.failed_count.has_value());
    EXPECT_EQ(*parsed.passed_count, 1);
    EXPECT_EQ(*parsed.failed_count, 1);
    ASSERT_EQ(parsed.failed_tests.size(), 1u);
    EXPECT_EQ(parsed.failed_tests[0], "alpha.case");
}

TEST(ParseCTestSummaryTest, PreservesFailedTestNamesContainingParentheses) {
    const auto parsed = parse_ctest_summary(
        "50% tests passed, 1 tests failed out of 2\nThe following tests FAILED:\n\t1 - my_test() (Failed)\n",
        "");

    ASSERT_EQ(parsed.failed_tests.size(), 1u);
    EXPECT_EQ(parsed.failed_tests[0], "my_test()");
}

TEST_F(BuildTestToolsTest, BuildProjectSafeRunsDebugBuild) {
    write_build_script(
        "printf '%s\n' \"$1\" > run_mode.txt\n"
        "printf 'building-%s\\n' \"$1\"\n"
        "exit 0\n");

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = json::object();

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["status"], "ok");
    EXPECT_EQ(result["exit_code"], 0);
    EXPECT_EQ(result["timed_out"], false);
    EXPECT_EQ(result["truncated"], false);
    EXPECT_NE(result["stdout"].get<std::string>().find("building-debug"), std::string::npos);
    EXPECT_EQ(result["summary"], "./build.sh debug completed successfully.");
}

TEST_F(BuildTestToolsTest, BuildProjectSafeSupportsCleanFirst) {
    write_build_script(
        "printf '%s\n' \"$1\" >> invocations.txt\n"
        "exit 0\n");

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = {
        {"build_mode", "release"},
        {"clean_first", true}
    };

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();

    std::ifstream in(fs::path(workspace_) / "invocations.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "clean\nrelease\n");
}

TEST_F(BuildTestToolsTest, BuildProjectSafeReturnsStructuredFailure) {
    write_build_script(
        "printf 'compile failed\\n' 1>&2\n"
        "exit 7\n");

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = json::object();

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "failed");
    EXPECT_EQ(result["exit_code"], 7);
    EXPECT_NE(result["stderr"].get<std::string>().find("compile failed"), std::string::npos);
    EXPECT_NE(result["summary"].get<std::string>().find("failed with exit code 7"), std::string::npos);
}

TEST_F(BuildTestToolsTest, BuildProjectSafeTimeoutIsStructured) {
    write_build_script(
        "sleep 2\n"
        "exit 0\n");

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = {
        {"timeout_ms", 50}
    };

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "timed_out");
    EXPECT_TRUE(result["timed_out"].get<bool>());
}

TEST_F(BuildTestToolsTest, BuildProjectSafeStillTimesOutAfterPipesClose) {
    write_build_script(
        "exec >/dev/null 2>/dev/null\n"
        "sleep 2\n"
        "exit 0\n");

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = {
        {"timeout_ms", 50}
    };

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_FALSE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["status"], "timed_out");
    EXPECT_TRUE(result["timed_out"].get<bool>());
}

TEST_F(BuildTestToolsTest, BuildProjectSafeSucceedsWhenPipesCloseBeforeExit) {
    write_build_script(
        "exec >/dev/null 2>/dev/null\n"
        "sleep 1\n"
        "exit 0\n");

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = {
        {"timeout_ms", 2000}
    };

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["status"], "ok");
    EXPECT_FALSE(result["timed_out"].get<bool>());
}

TEST_F(BuildTestToolsTest, BuildProjectSafeRetainsTailWhenOutputTruncated) {
    write_build_script(
        "i=1\n"
        "while [ $i -le 40 ]; do\n"
        "  printf 'line-%02d-abcdefghijklmnopqrstuvwxyz\\n' \"$i\"\n"
        "  i=$((i + 1))\n"
        "done\n"
        "exit 0\n");

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = {
        {"max_output_bytes", 80}
    };

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_NE(result["stdout"].get<std::string>().find("line-40"), std::string::npos);
}

TEST_F(BuildTestToolsTest, BuildProjectSafeClearsChildEnvironment) {
    ScopedEnvVar scoped_secret("NANO_AGENT_TEST_SECRET", "top-secret");
    write_build_script(
        "if [ -n \"$NANO_AGENT_TEST_SECRET\" ]; then\n"
        "  printf 'leaked:%s\\n' \"$NANO_AGENT_TEST_SECRET\"\n"
        "else\n"
        "  printf 'clean-env\\n'\n"
        "fi\n"
        "exit 0\n");

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = json::object();

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["stdout"], "clean-env\n");
}

TEST_F(BuildTestToolsTest, BuildProjectSafeHonorsRegistryOutputLimit) {
    write_build_script(
        "i=1\n"
        "while [ $i -le 20 ]; do\n"
        "  printf 'line-%02d-abcdefghijklmnopqrstuvwxyz\\n' \"$i\"\n"
        "  i=$((i + 1))\n"
        "done\n"
        "exit 0\n");

    const ToolRegistry& registry = get_default_tool_registry();
    const ToolDescriptor* descriptor = registry.find("build_project_safe");
    ASSERT_NE(descriptor, nullptr);

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = {
        {"max_output_bytes", 512}
    };

    const json result = descriptor->execute(call, approved_config(), 40);
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_LE(result["stdout"].get<std::string>().size(), 40u);
}

TEST_F(BuildTestToolsTest, TestProjectSafeParsesCountsAndFailedTests) {
    write_build_script(
        "printf '50%% tests passed, 1 tests failed out of 2\\n'\n"
        "printf 'The following tests FAILED:\\n'\n"
        "printf '        1 - suite.alpha (Failed)\\n'\n"
        "exit 1\n");

    ToolCall call;
    call.name = "test_project_safe";
    call.arguments = json::object();

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "failed");
    ASSERT_TRUE(result.contains("passed_count"));
    ASSERT_TRUE(result.contains("failed_count"));
    EXPECT_EQ(result["passed_count"], 1);
    EXPECT_EQ(result["failed_count"], 1);
    ASSERT_TRUE(result.contains("failed_tests"));
    ASSERT_EQ(result["failed_tests"].size(), 1);
    EXPECT_EQ(result["failed_tests"][0], "suite.alpha");
}

TEST_F(BuildTestToolsTest, TestProjectSafeReturnsStructuredSuccessResult) {
    write_build_script(
        "printf '100%% tests passed, 0 tests failed out of 2\\n'\n"
        "exit 0\n");

    ToolCall call;
    call.name = "test_project_safe";
    call.arguments = json::object();

    const auto result = json::parse(execute_tool(call, approved_config()));
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["status"], "ok");
    EXPECT_TRUE(result.contains("summary"));
    EXPECT_TRUE(result.contains("stdout"));
    EXPECT_TRUE(result.contains("stderr"));
    ASSERT_TRUE(result.contains("passed_count"));
    ASSERT_TRUE(result.contains("failed_count"));
    EXPECT_EQ(result["passed_count"], 2);
    EXPECT_EQ(result["failed_count"], 0);
}

TEST_F(BuildTestToolsTest, TestProjectSafeHonorsRegistryOutputLimit) {
    write_build_script(
        "i=1\n"
        "while [ $i -le 20 ]; do\n"
        "  printf 'test-line-%02d-abcdefghijklmnopqrstuvwxyz\\n' \"$i\"\n"
        "  i=$((i + 1))\n"
        "done\n"
        "printf '100%% tests passed, 0 tests failed out of 1\\n'\n"
        "exit 0\n");

    const ToolRegistry& registry = get_default_tool_registry();
    const ToolDescriptor* descriptor = registry.find("test_project_safe");
    ASSERT_NE(descriptor, nullptr);

    ToolCall call;
    call.name = "test_project_safe";
    call.arguments = {
        {"max_output_bytes", 512}
    };

    const json result = descriptor->execute(call, approved_config(), 48);
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_LE(result["stdout"].get<std::string>().size(), 48u);
}

TEST_F(BuildTestToolsTest, BuildProjectSafeBlockedWithoutApproval) {
    write_build_script("exit 0\n");

    AgentConfig config;
    config.workspace_abs = workspace_;

    ToolCall call;
    call.name = "build_project_safe";
    call.arguments = json::object();

    const auto result = json::parse(execute_tool(call, config));
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_EQ(result["category"], "execution");
}

} // namespace
