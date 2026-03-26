#include <gtest/gtest.h>

#include "agent_tools.hpp"
#include "repo_tools.hpp"
#include "tool_call.hpp"
#include "config.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <type_traits>

namespace fs = std::filesystem;

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

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const std::string& value)
        : name_(name ? name : "") {
        if (name_.empty()) {
            return;
        }
        if (const char* existing = std::getenv(name_.c_str())) {
            had_original_ = true;
            original_value_ = existing;
        }
        setenv(name_.c_str(), value.c_str(), 1);
    }

    ~ScopedEnvVar() {
        if (name_.empty()) {
            return;
        }
        if (had_original_) {
            setenv(name_.c_str(), original_value_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
    ScopedEnvVar(ScopedEnvVar&&) = delete;
    ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

private:
    std::string name_;
    std::string original_value_;
    bool had_original_ = false;
};

static_assert(!std::is_copy_constructible_v<ScopedEnvVar>);
static_assert(!std::is_copy_assignable_v<ScopedEnvVar>);
static_assert(!std::is_move_constructible_v<ScopedEnvVar>);
static_assert(!std::is_move_assignable_v<ScopedEnvVar>);

} // namespace

class RepoToolsTest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        test_workspace = (fs::temp_directory_path() / ("nano_repo_tools_" + std::to_string(tick))).string();
        fs::create_directories(test_workspace);
    }

    void TearDown() override {
        clear_rg_binary_for_testing();
        fs::remove_all(test_workspace);
    }

    void create_file(const std::string& rel_path, const std::string& content) {
        fs::path path = fs::path(test_workspace) / rel_path;
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    std::string create_fake_rg_script() {
        const fs::path script_path = fs::path(test_workspace) / "fake-rg.sh";
        std::ofstream out(script_path);
        out << "#!/bin/sh\n";
        // Args: rg --json --line-number --color never -e <query> <dir>
        // Positional: $0=rg_binary $1=--json $2=--line-number $3=--color $4=never
        //             $5=-e $6=<query> $7=<dir>
        out << "query=\"$6\"\n";
        out << "target=\"$7\"\n";
        out << "if [ \"$query\" = \"needle\" ] || [ \"$query\" = \"-dash-query\" ]; then\n";
        out << "  grep -R -n -- \"needle\" \"$target\" | while IFS=: read -r file line rest; do\n";
        out << "    printf '{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"%s\"},\"line_number\":%s,\"submatches\":[{\"start\":0}],\"lines\":{\"text\":\"match:%s\\\\n\"}}}\\n' \"$file\" \"$line\" \"$query\"\n";
        out << "  done\n";
        out << "  exit 0\n";
        out << "fi\n";
        out << "exit 1\n";
        out.close();
        fs::permissions(script_path,
                        fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                        fs::perms::group_exec | fs::perms::group_read |
                        fs::perms::others_exec | fs::perms::others_read);
        return script_path.string();
    }
};

TEST_F(RepoToolsTest, ListFilesBoundedRespectsLimitAndTruncation) {
    create_file("a.cpp", "int a = 1;\n");
    create_file("b.txt", "skip\n");
    create_file(".git/config", "[core]\n");
    create_file("src/c.hpp", "#pragma once\n");
    create_file("src/d.cpp", "int d = 1;\n");

    const auto result = list_files_bounded(test_workspace, "", {".cpp", ".hpp"}, 2);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_TRUE(result["truncated"].get<bool>());
    EXPECT_EQ(result["returned"].get<size_t>(), 2U);
    ASSERT_EQ(result["files"].size(), 2);
    EXPECT_EQ(result["files"][0]["path"], "a.cpp");
    EXPECT_EQ(result["files"][1]["path"], "src/c.hpp");
    for (const auto& file : result["files"]) {
        EXPECT_FALSE(file["path"].get<std::string>().starts_with(".git/"));
    }
}

TEST_F(RepoToolsTest, RgSearchFindsMatchesWithInjectedBinary) {
    create_file("src/sample.cpp", "int needle = 42;\n");
    set_rg_binary_for_testing(create_fake_rg_script());

    const auto result = rg_search(test_workspace, "needle", "src", 10, 20);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_EQ(result["directory"], "src");
    EXPECT_EQ(result["returned"].get<size_t>(), 1U);
    ASSERT_EQ(result["matches"].size(), 1);
    EXPECT_EQ(result["matches"][0]["file"], "src/sample.cpp");
    EXPECT_EQ(result["matches"][0]["line"], 1);
    EXPECT_NE(result["matches"][0]["snippet"].get<std::string>().find("needle"), std::string::npos);
}

TEST_F(RepoToolsTest, RgSearchReturnsReadableErrorWhenBinaryCannotExecute) {
    set_rg_binary_for_testing((fs::path(test_workspace) / "missing-rg").string());

    const auto result = rg_search(test_workspace, "needle", "", 10, 40);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("failed to exec"), std::string::npos);
}

TEST_F(RepoToolsTest, GitStatusFailsGracefullyOutsideGitRepo) {
    const auto result = git_status(test_workspace, 0);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("git repository"), std::string::npos);
}

TEST_F(RepoToolsTest, GitStatusParsesRepositoryStateAndTruncates) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);

    create_file("tracked.txt", "hello\n");
    create_file("untracked.txt", "world\n");

    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add tracked.txt >/dev/null 2>&1"), 0);

    const auto full = git_status(test_workspace, 10);
    ASSERT_TRUE(full["ok"].get<bool>()) << full["error"];
    EXPECT_EQ(full["branch"], "main");
    EXPECT_TRUE(full["has_changes"].get<bool>());
    ASSERT_GE(full["entries"].size(), 2U);

    const auto limited = git_status(test_workspace, 1);
    ASSERT_TRUE(limited["ok"].get<bool>()) << limited["error"];
    EXPECT_TRUE(limited["truncated"].get<bool>());
    ASSERT_EQ(limited["entries"].size(), 1U);
}

TEST_F(RepoToolsTest, FindExecutableResolvesFromProcessPath) {
    // Create a fake rg binary named exactly "rg" in a custom directory so that
    // find_executable_in_path must locate it through the PATH env var rather
    // than the hardcoded fallback directories.
    const fs::path custom_bin = fs::path(test_workspace) / "custom-bin";
    fs::create_directories(custom_bin);
    const fs::path fake_rg = custom_bin / "rg";
    {
        std::ofstream out(fake_rg);
        out << "#!/bin/sh\n";
        // Emit one rg-format match line regardless of arguments.
        // Using echo with single-quoted JSON avoids shell escape complexity.
        out << "echo '{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"found.cpp\"},"
               "\"line_number\":1,\"submatches\":[{\"start\":0}],"
               "\"lines\":{\"text\":\"line\"}}}' \n";
        out << "exit 0\n";
    }
    fs::permissions(fake_rg,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    // Save and override PATH so our custom-bin directory is searched first.
    const char* old_path_cstr = std::getenv("PATH");
    const std::string old_path = old_path_cstr ? old_path_cstr : "";
    const bool had_path = (old_path_cstr != nullptr);
    const std::string new_path = custom_bin.string() + (had_path ? ":" + old_path : "");
    setenv("PATH", new_path.c_str(), 1);

    // Ensure override is cleared so rg_search uses find_executable_in_path.
    clear_rg_binary_for_testing();
    const auto result = rg_search(test_workspace, "anything", "", 10, 40);

    // Restore PATH unconditionally before any assertions.
    if (had_path) {
        setenv("PATH", old_path.c_str(), 1);
    } else {
        unsetenv("PATH");
    }

    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_EQ(result["returned"].get<size_t>(), 1U);
}

TEST_F(RepoToolsTest, GitStatusDoesNotSplitFilenameContainingArrow) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);

    // A file whose name legitimately contains the " -> " substring.
    create_file("a -> b.txt", "content\n");

    const auto result = git_status(test_workspace, 10);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];

    bool found = false;
    for (const auto& entry : result["entries"]) {
        // Git may C-quote filenames containing spaces, so check with find().
        if (entry["path"].get<std::string>().find("a -> b.txt") != std::string::npos) {
            found = true;
            EXPECT_FALSE(entry.contains("orig_path"))
                << "Untracked file with arrow in name must not gain a fabricated orig_path";
        }
    }
    EXPECT_TRUE(found) << "Expected an entry for 'a -> b.txt' in git status output";
}

TEST_F(RepoToolsTest, RgSearchHandlesDashPrefixedQuery) {
    // A query starting with '-' must be passed through -e so rg does not
    // interpret it as a flag. The fake binary accepts "-dash-query" as a
    // valid pattern and returns a match for "needle" in the file.
    create_file("src/sample.cpp", "int needle = 42;\n");
    set_rg_binary_for_testing(create_fake_rg_script());

    const auto result = rg_search(test_workspace, "-dash-query", "src", 10, 40);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_EQ(result["returned"].get<size_t>(), 1U);
}

TEST_F(RepoToolsTest, GitStatusRenameWithArrowInOrigPath) {
    // Machine-safe porcelain parsing must preserve the original path even when
    // the source filename itself contains the arrow substring.
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);

    // Simulate a rename where old path itself has " -> " in the name.
    // We parse the line directly through git_status by staging a rename.
    create_file("a -> b.txt", "hello\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add . >/dev/null 2>&1"), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git -c user.email=t@t.com -c user.name=T commit -m init >/dev/null 2>&1"), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git mv 'a -> b.txt' c.txt >/dev/null 2>&1"), 0);

    const auto result = git_status(test_workspace, 10);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];

    bool found = false;
    for (const auto& entry : result["entries"]) {
        if (entry["path"].get<std::string>().find("c.txt") != std::string::npos) {
            found = true;
            // orig_path must end in "a -> b.txt", not an intermediate split
            ASSERT_TRUE(entry.contains("orig_path"))
                << "Rename entry must carry orig_path";
            EXPECT_NE(entry["orig_path"].get<std::string>().find("a -> b.txt"), std::string::npos)
                << "orig_path should contain the original filename 'a -> b.txt'";
        }
    }
    EXPECT_TRUE(found) << "Expected a rename entry to c.txt";
}

TEST_F(RepoToolsTest, GitStatusRenameDestHasArrow) {
    // Machine-safe porcelain parsing must preserve the destination path even
    // when the target filename contains the arrow substring.
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);

    create_file("old_plain.txt", "content\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add . >/dev/null 2>&1"), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git -c user.email=t@t.com -c user.name=T commit -m init >/dev/null 2>&1"), 0);
    // Rename: original path is plain, destination contains ' -> '.
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git mv 'old_plain.txt' 'new -> name.txt' >/dev/null 2>&1"), 0);

    const auto result = git_status(test_workspace, 10);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];

    bool found = false;
    for (const auto& entry : result["entries"]) {
        const std::string path = entry["path"].get<std::string>();
        if (path.find("new -> name.txt") != std::string::npos ||
            path.find("new") != std::string::npos) {
            found = true;
            ASSERT_TRUE(entry.contains("orig_path")) << "Rename entry must carry orig_path";
            EXPECT_EQ(entry["orig_path"].get<std::string>(), "old_plain.txt")
                << "orig_path must be the original filename without the dest arrow";
        }
    }
    EXPECT_TRUE(found) << "Expected a rename entry for the destination with ' -> '";
}

TEST_F(RepoToolsTest, ListFilesBoundedExecutorEnforcesOutputLimit) {
    // When the ToolRegistry executor for list_files_bounded is invoked with a
    // tiny max_tool_output_bytes, the repo tool must stop appending entries and
    // return valid truncated JSON.
    // Creates enough files that the raw JSON would exceed 100 bytes.
    for (int i = 0; i < 10; ++i) {
        create_file("file" + std::to_string(i) + ".cpp", "int x = " + std::to_string(i) + ";\n");
    }

    ToolCall call;
    call.name = "list_files_bounded";
    call.arguments = nlohmann::json::object();

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_tool_output_bytes = 100; // forces truncation

    const std::string raw = get_default_tool_registry().execute(call, config);
    const auto result = nlohmann::json::parse(raw);
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>())
        << "Expected truncated=true when output_limit is tiny; got: " << result.dump();
}

TEST_F(RepoToolsTest, RgSearchExecutorEnforcesOutputLimit) {
    for (int i = 0; i < 8; ++i) {
        create_file("src/file" + std::to_string(i) + ".cpp", "int needle = 42;\n");
    }
    set_rg_binary_for_testing(create_fake_rg_script());

    ToolCall call;
    call.name = "rg_search";
    call.arguments = {
        {"query", "needle"},
        {"directory", "src"}
    };

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_tool_output_bytes = 150;

    const std::string raw = get_default_tool_registry().execute(call, config);
    const auto result = nlohmann::json::parse(raw);
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_TRUE(result.contains("matches"));
    EXPECT_TRUE(result["matches"].is_array());
    EXPECT_EQ(result["returned"].get<size_t>(), result["matches"].size());
}

TEST_F(RepoToolsTest, GitStatusExecutorEnforcesOutputLimit) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    for (int i = 0; i < 8; ++i) {
        create_file("file" + std::to_string(i) + ".txt", "content\n");
    }

    ToolCall call;
    call.name = "git_status";
    call.arguments = nlohmann::json::object();

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_tool_output_bytes = 170;

    const std::string raw = get_default_tool_registry().execute(call, config);
    const auto result = nlohmann::json::parse(raw);
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_TRUE(result.contains("branch"));
    EXPECT_TRUE(result.contains("entries"));
    EXPECT_TRUE(result["entries"].is_array());
    EXPECT_EQ(result["returned"].get<size_t>(), result["entries"].size());
}

TEST_F(RepoToolsTest, GitAddRejectsEmptyPathspecs) {
    const auto result = git_add(test_workspace, {});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["exit_code"].get<int>(), -1);
    EXPECT_NE(result["error"].get<std::string>().find("pathspec"), std::string::npos);
}

TEST_F(RepoToolsTest, GitAddRejectsEmptyPathStringElements) {
    const auto result = git_add(test_workspace, {""});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["exit_code"].get<int>(), -1);
    EXPECT_EQ(result["error"].get<std::string>(),
              "Argument 'pathspecs' for git_add must not contain empty path strings.");
}

TEST_F(RepoToolsTest, GitAddFailsGracefullyOutsideGitRepo) {
    const auto result = git_add(test_workspace, {"tracked.txt"});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("git repository"), std::string::npos);
}

TEST_F(RepoToolsTest, GitAddStagesOnlyRequestedPathspecs) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    create_file("only.txt", "only\n");
    create_file("other.txt", "other\n");

    const auto add_result = git_add(test_workspace, {"only.txt"});
    ASSERT_TRUE(add_result["ok"].get<bool>()) << add_result.dump();

    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && ! git diff --cached --quiet -- only.txt"), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git diff --cached --quiet -- other.txt"), 0);
}

TEST_F(RepoToolsTest, GitAddHandlesDashPrefixedFilename) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    create_file("-dash.txt", "dash\n");

    const auto result = git_add(test_workspace, {"-dash.txt"});
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && ! git diff --cached --quiet -- -- '-dash.txt'"), 0);
}

TEST_F(RepoToolsTest, GitAddRejectsParentEscapePathspec) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    fs::create_directories(fs::path(test_workspace) / "subdir");
    create_file("root.txt", "root\n");

    const auto result = git_add((fs::path(test_workspace) / "subdir").string(), {"../root.txt"});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("workspace"), std::string::npos);
}

TEST_F(RepoToolsTest, GitAddRejectsAbsolutePath) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    create_file("tracked.txt", "tracked\n");

    const auto result = git_add(test_workspace, {(fs::path(test_workspace) / "tracked.txt").string()});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("workspace"), std::string::npos);
}

TEST_F(RepoToolsTest, GitAddRejectsPathspecMagic) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    create_file("tracked.txt", "tracked\n");

    const auto result = git_add(test_workspace, {":(top)tracked.txt"});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("pathspec magic"), std::string::npos);
}

TEST_F(RepoToolsTest, GitAddRejectsRepoRootPathspecMagic) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    fs::create_directories(fs::path(test_workspace) / "subdir");
    create_file("outside.txt", "outside\n");

    const auto result = git_add((fs::path(test_workspace) / "subdir").string(), {":/outside.txt"});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("pathspec magic"), std::string::npos);
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git diff --cached --quiet -- outside.txt"), 0);
}

TEST_F(RepoToolsTest, GitAddRejectsShortFormPathspecMagic) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    create_file("inside.txt", "inside\n");

    const auto result = git_add(test_workspace, {":!inside.txt"});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("pathspec magic"), std::string::npos);
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git diff --cached --quiet -- inside.txt"), 0);
}

TEST_F(RepoToolsTest, GitAddRejectsBareColonPathspecMagic) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    create_file("inside.txt", "inside\n");

    const auto result = git_add(test_workspace, {":"});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("pathspec magic"), std::string::npos);
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git diff --cached --quiet -- inside.txt"), 0);
}

TEST_F(RepoToolsTest, GitCommitRejectsEmptyMessage) {
    const auto result = git_commit(test_workspace, "");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["exit_code"].get<int>(), -1);
    EXPECT_NE(result["error"].get<std::string>().find("message"), std::string::npos);
}

TEST_F(RepoToolsTest, GitCommitFailsGracefullyOutsideGitRepo) {
    const auto result = git_commit(test_workspace, "msg");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("git repository"), std::string::npos);
}

TEST_F(RepoToolsTest, GitCommitWithoutStagedChangesReturnsReadableError) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'base\\n' > tracked.txt &&"
                       " git add tracked.txt >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1"), 0);
    create_file("tracked.txt", "changed but unstaged\n");

    const auto result = git_commit(test_workspace, "second");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("staged"), std::string::npos);
    EXPECT_TRUE(!result["stdout"].get<std::string>().empty() ||
                !result["stderr"].get<std::string>().empty());
}

TEST_F(RepoToolsTest, GitCommitCreatesCommitFromStagedIndex) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'base\\n' > staged.txt &&"
                       " printf 'keep\\n' > unstaged.txt &&"
                       " git add staged.txt unstaged.txt >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1"), 0);

    create_file("staged.txt", "staged change\n");
    create_file("unstaged.txt", "worktree only\n");
    ASSERT_TRUE(git_add(test_workspace, {"staged.txt"})["ok"].get<bool>());

    const auto result = git_commit(test_workspace, "second");
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();

    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && test \"$(git show HEAD:staged.txt)\" = \"staged change\""), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && test \"$(git show HEAD:unstaged.txt)\" = \"keep\""), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && ! git diff --quiet -- unstaged.txt"), 0);
}

TEST_F(RepoToolsTest, GitCommitReturnsCommitSha) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    create_file("tracked.txt", "hello\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());

    const auto result = git_commit(test_workspace, "init");
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    ASSERT_TRUE(result.contains("commit_sha"));
    const std::string sha = result["commit_sha"].get<std::string>();
    EXPECT_FALSE(sha.empty());

    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git rev-parse HEAD > head.txt"), 0);
    std::ifstream in(fs::path(test_workspace) / "head.txt");
    std::string head;
    std::getline(in, head);
    EXPECT_EQ(sha, head);
}

TEST_F(RepoToolsTest, GitCommitPreservesExplicitHashPrefixedMessageLines) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    create_file("tracked.txt", "hello\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());
    ASSERT_TRUE(git_commit(test_workspace, "init")["ok"].get<bool>());

    create_file("tracked.txt", "next\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());

    const std::string message = "subject\n#kept\nbody";
    const auto result = git_commit(test_workspace, message);
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();

    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git log -1 --format=%B > actual_message.txt &&"
                       " grep -Fx 'subject' actual_message.txt >/dev/null &&"
                       " grep -Fx '#kept' actual_message.txt >/dev/null &&"
                       " grep -Fx 'body' actual_message.txt >/dev/null"), 0);
}

TEST_F(RepoToolsTest, GitCommitRejectsStagedPathsOutsideWorkspace) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    fs::create_directories(fs::path(test_workspace) / "subdir");
    create_file("subdir/inside.txt", "inside\n");
    create_file("outside.txt", "outside\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add subdir/inside.txt outside.txt >/dev/null 2>&1"), 0);
    ASSERT_NE(run_bash("cd '" + test_workspace + "' && git rev-parse HEAD >/dev/null 2>&1"), 0);

    const auto result = git_commit((fs::path(test_workspace) / "subdir").string(), "blocked");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("outside the workspace"), std::string::npos);
    ASSERT_NE(run_bash("cd '" + test_workspace + "' && git rev-parse HEAD >/dev/null 2>&1"), 0);
}

TEST_F(RepoToolsTest, GitCommitSucceedsWhenAllStagedPathsStayInsideWorkspace) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    fs::create_directories(fs::path(test_workspace) / "subdir");
    create_file("subdir/inside.txt", "inside\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add subdir/inside.txt >/dev/null 2>&1"), 0);

    const auto result = git_commit((fs::path(test_workspace) / "subdir").string(), "inside only");
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && test \"$(git show HEAD:subdir/inside.txt)\" = \"inside\""), 0);
}

TEST_F(RepoToolsTest, GitCommitInRepoRootStillWorks) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    create_file("tracked.txt", "hello\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());

    const auto result = git_commit(test_workspace, "root commit");
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
}

TEST_F(RepoToolsTest, GitCommitRejectsCrossBoundaryRenameIntoWorkspace) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    fs::create_directories(fs::path(test_workspace) / "subdir");
    create_file("outside.txt", "outside\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add outside.txt >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1 &&"
                       " git mv outside.txt subdir/inside.txt"), 0);

    const auto before = git_show(test_workspace, "HEAD", false, false, {}, 3);
    ASSERT_TRUE(before["ok"].get<bool>()) << before.dump();

    const auto result = git_commit((fs::path(test_workspace) / "subdir").string(), "rename blocked");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("outside the workspace"), std::string::npos);

    const auto after = git_show(test_workspace, "HEAD", false, false, {}, 3);
    ASSERT_TRUE(after["ok"].get<bool>()) << after.dump();
    EXPECT_EQ(before["stdout"], after["stdout"]);
}

TEST_F(RepoToolsTest, GitCommitRejectsCrossBoundaryRenameOutOfWorkspace) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    fs::create_directories(fs::path(test_workspace) / "subdir");
    create_file("subdir/inside.txt", "inside\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add subdir/inside.txt >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1 &&"
                       " git mv subdir/inside.txt outside.txt"), 0);

    const auto before = git_show(test_workspace, "HEAD", false, false, {}, 3);
    ASSERT_TRUE(before["ok"].get<bool>()) << before.dump();

    const auto result = git_commit((fs::path(test_workspace) / "subdir").string(), "rename blocked");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("outside the workspace"), std::string::npos);

    const auto after = git_show(test_workspace, "HEAD", false, false, {}, 3);
    ASSERT_TRUE(after["ok"].get<bool>()) << after.dump();
    EXPECT_EQ(before["stdout"], after["stdout"]);
}

TEST_F(RepoToolsTest, GitAddFailurePropagatesTruncatedFlag) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    const fs::path filter_script = fs::path(test_workspace) / "broken-clean.sh";
    {
        std::ofstream out(filter_script);
        out << "#!/bin/sh\n";
        out << "i=0\n";
        out << "while [ $i -lt 2000 ]; do printf 'clean-filter-line-%03d\\n' \"$i\" 1>&2; i=$((i+1)); done\n";
        out << "exit 1\n";
    }
    fs::permissions(filter_script,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);
    create_file(".gitattributes", "*.txt filter=broken\n");
    create_file("tracked.txt", "hello\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git config filter.broken.clean '" + filter_script.string() + "' &&"
                       " git config filter.broken.required true"), 0);

    const auto result = git_add(test_workspace, {"tracked.txt"}, 40);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
}

TEST_F(RepoToolsTest, GitAddKilledEarlyByOutputLimitIsReportedAsFailure) {
    const fs::path custom_bin = fs::path(test_workspace) / "custom-bin";
    fs::create_directories(custom_bin);
    const fs::path fake_git = custom_bin / "git";
    {
        std::ofstream out(fake_git);
        out << "#!/bin/sh\n";
        out << "i=0\n";
        out << "while [ $i -lt 200000 ]; do printf 'fake-git-add-line-%06d\\n' \"$i\"; i=$((i+1)); done\n";
        out << "exit 0\n";
    }
    fs::permissions(fake_git,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);
    const char* old_path_cstr = std::getenv("PATH");
    const std::string old_path = old_path_cstr ? old_path_cstr : "";
    ScopedEnvVar scoped_path("PATH", custom_bin.string() + ":" + old_path);

    const auto result = git_add(test_workspace, {"tracked.txt"}, 40);
    EXPECT_FALSE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_EQ(result["error"].get<std::string>(),
              "git add output exceeded the configured limit before the command completed.");
}

TEST_F(RepoToolsTest, GitCommitFailurePropagatesTruncatedFlag) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'hello\\n' > tracked.txt &&"
                       " git add tracked.txt >/dev/null 2>&1"), 0);
    const fs::path hook_dir = fs::path(test_workspace) / ".git" / "hooks";
    const fs::path hook = hook_dir / "commit-msg";
    {
        std::ofstream out(hook);
        out << "#!/bin/sh\n";
        out << "i=0\n";
        out << "while [ $i -lt 2000 ]; do printf 'hook-output-line-%03d\\n' \"$i\"; i=$((i+1)); done\n";
        out << "exit 1\n";
    }
    fs::permissions(hook,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    const auto result = git_commit(test_workspace, "hook fail", 40);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
}

TEST_F(RepoToolsTest, GitCommitKilledEarlyByOutputLimitIsReportedAsFailure) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'base\\n' > tracked.txt &&"
                       " git add tracked.txt >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1"), 0);
    create_file("tracked.txt", "changed\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());

    const fs::path hook = fs::path(test_workspace) / ".git" / "hooks" / "pre-commit";
    {
        std::ofstream out(hook);
        out << "#!/bin/sh\n";
        out << "i=0\n";
        out << "while [ $i -lt 200000 ]; do printf 'hook-output-line-%06d\\n' \"$i\"; i=$((i+1)); done\n";
        out << "exit 1\n";
    }
    fs::permissions(hook,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git rev-parse HEAD > before_head.txt"), 0);
    const auto result = git_commit(test_workspace, "killed", 40);
    EXPECT_FALSE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["commit_sha"].get<std::string>().empty()) << result.dump();
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git rev-parse HEAD > after_head.txt"), 0);
    std::ifstream before_in(fs::path(test_workspace) / "before_head.txt");
    std::ifstream after_in(fs::path(test_workspace) / "after_head.txt");
    std::string before_head;
    std::string after_head;
    std::getline(before_in, before_head);
    std::getline(after_in, after_head);
    EXPECT_EQ(after_head, before_head) << result.dump();
}

TEST_F(RepoToolsTest, GitCommitDoesNotReturnOldHeadWhenCommandWasKilledEarly) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'base\\n' > tracked.txt &&"
                       " git add tracked.txt >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1 &&"
                       " git rev-parse HEAD > before_head.txt"), 0);
    create_file("tracked.txt", "changed\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());

    const fs::path hook = fs::path(test_workspace) / ".git" / "hooks" / "pre-commit";
    {
        std::ofstream out(hook);
        out << "#!/bin/sh\n";
        out << "i=0\n";
        out << "while [ $i -lt 200000 ]; do printf 'hook-output-line-%06d\\n' \"$i\"; i=$((i+1)); done\n";
        out << "exit 0\n";
    }
    fs::permissions(hook,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    const auto result = git_commit(test_workspace, "killed", 40);
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    std::ifstream before_in(fs::path(test_workspace) / "before_head.txt");
    std::string before_head;
    std::getline(before_in, before_head);
    EXPECT_NE(result["commit_sha"].get<std::string>(), before_head) << result.dump();
}

TEST_F(RepoToolsTest, GitCommitStderrOnlyFailureDoesNotPrefixErrorWithNewline) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'hello\\n' > tracked.txt &&"
                       " git add tracked.txt >/dev/null 2>&1"), 0);
    const fs::path hook = fs::path(test_workspace) / ".git" / "hooks" / "commit-msg";
    {
        std::ofstream out(hook);
        out << "#!/bin/sh\n";
        out << "printf 'stderr-only-hook-failure\\n' 1>&2\n";
        out << "exit 1\n";
    }
    fs::permissions(hook,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    const auto result = git_commit(test_workspace, "stderr only");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["error"].get<std::string>(), "stderr-only-hook-failure");
}

TEST_F(RepoToolsTest, GitCommitIgnoresHooksWhenEnforcingWorkspaceOnlyCommits) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T"), 0);
    fs::create_directories(fs::path(test_workspace) / "subdir");
    create_file("subdir/inside.txt", "inside\n");
    create_file("outside.txt", "outside\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add subdir/inside.txt >/dev/null 2>&1"), 0);

    const fs::path hook = fs::path(test_workspace) / ".git" / "hooks" / "pre-commit";
    {
        std::ofstream out(hook);
        out << "#!/bin/sh\n";
        out << "printf 'mutated by hook\\n' > outside.txt\n";
        out << "git add outside.txt\n";
        out << "exit 0\n";
    }
    fs::permissions(hook,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    const auto result = git_commit((fs::path(test_workspace) / "subdir").string(), "inside only");
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && test \"$(git show HEAD:subdir/inside.txt)\" = \"inside\""), 0);
    ASSERT_NE(run_bash("cd '" + test_workspace + "' && git show HEAD:outside.txt >/dev/null 2>&1"), 0);
}

TEST_F(RepoToolsTest, GitCommitRespectsRepositorySigningPolicy) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " git config commit.gpgSign true &&"
                       " git config gpg.program /definitely/missing/gpg"), 0);
    create_file("tracked.txt", "hello\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());

    const auto result = git_commit(test_workspace, "signed commit");
    EXPECT_FALSE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["commit_sha"].get<std::string>().empty()) << result.dump();
    EXPECT_NE(result["error"].get<std::string>().find("gpg"), std::string::npos) << result.dump();
    ASSERT_NE(run_bash("cd '" + test_workspace + "' && git rev-parse HEAD >/dev/null 2>&1"), 0);
}

TEST_F(RepoToolsTest, GitCommitKilledEarlyAfterHeadAdvanceReturnsSuccess) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'base\\n' > tracked.txt &&"
                       " git add tracked.txt >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1 &&"
                       " git rev-parse HEAD > before_head.txt"), 0);
    create_file("tracked.txt", "changed\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());

    const fs::path hook = fs::path(test_workspace) / ".git" / "hooks" / "post-commit";
    {
        std::ofstream out(hook);
        out << "#!/bin/sh\n";
        out << "i=0\n";
        out << "while [ $i -lt 200000 ]; do printf 'post-commit-line-%06d\\n' \"$i\"; i=$((i+1)); done\n";
        out << "exit 0\n";
    }
    fs::permissions(hook,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    const auto result = git_commit(test_workspace, "post noisy", 40);
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_FALSE(result["commit_sha"].get<std::string>().empty()) << result.dump();

    std::ifstream before_in(fs::path(test_workspace) / "before_head.txt");
    std::string before_head;
    std::getline(before_in, before_head);
    EXPECT_NE(result["commit_sha"].get<std::string>(), before_head) << result.dump();
}

TEST_F(RepoToolsTest, GitCommitReturnsCreatedCommitWhenPostCommitHookAdvancesHead) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com && git config user.name T &&"
                       " printf 'base\\n' > tracked.txt &&"
                       " git add tracked.txt >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1"), 0);
    create_file("tracked.txt", "changed\n");
    ASSERT_TRUE(git_add(test_workspace, {"tracked.txt"})["ok"].get<bool>());

    const fs::path hook = fs::path(test_workspace) / ".git" / "hooks" / "post-commit";
    {
        std::ofstream out(hook);
        out << "#!/bin/sh\n";
        out << "if [ \"${NANO_EXTRA_POST_COMMIT:-}\" = \"1\" ]; then\n";
        out << "  exit 0\n";
        out << "fi\n";
        out << "export NANO_EXTRA_POST_COMMIT=1\n";
        out << "printf 'from-hook\\n' > hook.txt\n";
        out << "git add hook.txt >/dev/null 2>&1 || exit 1\n";
        out << "git commit -m hook-generated >/dev/null 2>&1 || exit 1\n";
        out << "exit 0\n";
    }
    fs::permissions(hook,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                        fs::perms::group_exec | fs::perms::group_read |
                        fs::perms::others_exec | fs::perms::others_read);

    const auto result = git_commit(test_workspace, "main commit");
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    const std::string commit_sha = result["commit_sha"].get<std::string>();
    ASSERT_FALSE(commit_sha.empty()) << result.dump();

    std::ifstream head_in(fs::path(test_workspace) / ".git" / "HEAD");
    (void)head_in;
    const std::string final_head_cmd = "cd '" + test_workspace + "' && git rev-parse HEAD > final_head.txt";
    ASSERT_EQ(run_bash(final_head_cmd), 0);
    std::ifstream final_head_in(fs::path(test_workspace) / "final_head.txt");
    std::string final_head;
    std::getline(final_head_in, final_head);
    EXPECT_NE(commit_sha, final_head) << result.dump();

    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && test \"$(git show " + commit_sha + ":tracked.txt)\" = \"changed\""), 0);
    ASSERT_NE(run_bash("cd '" + test_workspace + "' && git show " + commit_sha + ":hook.txt >/dev/null 2>&1"), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && test \"$(git show HEAD:hook.txt)\" = \"from-hook\""), 0);
}

// ---------------------------------------------------------------------------
// GitDiffTest: git_diff tool
// ---------------------------------------------------------------------------

class GitDiffTest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        test_workspace = (fs::temp_directory_path() /
                          ("nano_git_diff_" + std::to_string(tick))).string();
        fs::create_directories(test_workspace);
    }

    void TearDown() override {
        fs::remove_all(test_workspace);
    }

    void create_file(const std::string& rel_path, const std::string& content) {
        fs::path p = fs::path(test_workspace) / rel_path;
        fs::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary);
        out << content;
    }

    void init_repo_with_commit(const std::string& filename = "base.txt",
                                const std::string& content  = "initial content\n") {
        ASSERT_EQ(run_bash("cd '" + test_workspace +
                           "' && git init -b main >/dev/null 2>&1 &&"
                           " git config user.email t@t.com &&"
                           " git config user.name T"), 0);
        create_file(filename, content);
        ASSERT_EQ(run_bash("cd '" + test_workspace +
                           "' && git add . >/dev/null 2>&1 &&"
                           " git commit -m init >/dev/null 2>&1"), 0);
    }
};

TEST_F(GitDiffTest, NonRepoReturnsStructuredError) {
    // Plain temp directory, no .git → git diff must fail cleanly
    const auto result = git_diff(test_workspace, false, {}, 3);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("git repository"),
              std::string::npos)
        << "error: " << result["error"];
}

TEST_F(GitDiffTest, CleanRepoReturnsOkWithEmptyDiff) {
    init_repo_with_commit();
    const auto result = git_diff(test_workspace, false, {}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_EQ(result["stdout"].get<std::string>(), "")
        << "Clean repo diff should be empty";
    EXPECT_FALSE(result["truncated"].get<bool>());
}

TEST_F(GitDiffTest, UnstagedModificationShowsPatch) {
    init_repo_with_commit();
    // Overwrite the tracked file without staging
    create_file("base.txt", "modified content\n");

    const auto result = git_diff(test_workspace, false, {}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string& out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("---"), std::string::npos) << "Expected unified diff header";
    EXPECT_NE(out.find("+++"), std::string::npos) << "Expected unified diff header";
}

TEST_F(GitDiffTest, CachedOnlyShowsStagedPatch) {
    init_repo_with_commit();
    create_file("base.txt", "staged change\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add base.txt"), 0);

    const auto result = git_diff(test_workspace, /*cached=*/true, {}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string& out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("@@"), std::string::npos)
        << "Staged diff should contain hunk header";
}

TEST_F(GitDiffTest, DefaultDiffExcludesStagedChangesWhileCachedShowsOnlyStagedChanges) {
    init_repo_with_commit();
    create_file("staged.txt", "staged base\n");
    create_file("unstaged.txt", "unstaged base\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add . >/dev/null 2>&1 && git commit -m split >/dev/null 2>&1"), 0);

    create_file("staged.txt", "staged after\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add staged.txt"), 0);
    create_file("unstaged.txt", "unstaged after\n");

    const auto unstaged = git_diff(test_workspace, false, {}, 3);
    ASSERT_TRUE(unstaged["ok"].get<bool>()) << unstaged["error"];
    const std::string unstaged_out = unstaged["stdout"].get<std::string>();
    EXPECT_NE(unstaged_out.find("a/unstaged.txt"), std::string::npos);
    EXPECT_EQ(unstaged_out.find("a/staged.txt"), std::string::npos);

    const auto staged = git_diff(test_workspace, true, {}, 3);
    ASSERT_TRUE(staged["ok"].get<bool>()) << staged["error"];
    const std::string staged_out = staged["stdout"].get<std::string>();
    EXPECT_NE(staged_out.find("a/staged.txt"), std::string::npos);
    EXPECT_EQ(staged_out.find("a/unstaged.txt"), std::string::npos);
}

TEST_F(GitDiffTest, PathspecFiltersFiles) {
    // Need two TRACKED files both modified; git diff only shows tracked changes
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com &&"
                       " git config user.name T"), 0);
    create_file("file1.txt", "original file1\n");
    create_file("file2.txt", "original file2\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git add . >/dev/null 2>&1 &&"
                       " git commit -m init >/dev/null 2>&1"), 0);
    // Modify both without staging
    create_file("file1.txt", "changed file1\n");
    create_file("file2.txt", "changed file2\n");

    const auto result = git_diff(test_workspace, false, {"file1.txt"}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string& out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("file1.txt"), std::string::npos);
    EXPECT_EQ(out.find("file2.txt"), std::string::npos)
        << "Pathspec should exclude file2.txt from diff";
}

TEST_F(GitDiffTest, LargeDiffIsBoundedAndMarked) {
    init_repo_with_commit();
    // Generate a large modification
    std::string big;
    big.reserve(5000);
    for (int i = 0; i < 200; ++i) {
        big += "line " + std::to_string(i) + " aaaaaa bbbbbb cccccc dddddd\n";
    }
    create_file("base.txt", big);

    // Use a very small output_limit to force truncation
    const auto result = git_diff(test_workspace, false, {}, 3, 200);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_EQ(result["exit_code"].get<int>(), 0) << result.dump();
    EXPECT_LE(result["stdout"].get<std::string>().size(), 200u) << result.dump();
}

TEST_F(GitDiffTest, LargeSingleLineDiffIsBoundedAndMarked) {
    init_repo_with_commit();
    std::string single_line(12000, 'x');
    single_line.push_back('\n');
    create_file("base.txt", single_line);

    const auto result = git_diff(test_workspace, false, {}, 3, 200);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_EQ(result["exit_code"].get<int>(), 0) << result.dump();
    EXPECT_LE(result["stdout"].get<std::string>().size(), 200u) << result.dump();
}

TEST_F(GitDiffTest, ExactFitOutputLimitDoesNotTruncate) {
    init_repo_with_commit();
    create_file("base.txt", "exact fit line\n");

    const auto baseline = git_diff(test_workspace, false, {}, 3, 0);
    ASSERT_TRUE(baseline["ok"].get<bool>()) << baseline["error"];
    const std::string baseline_out = baseline["stdout"].get<std::string>();
    ASSERT_FALSE(baseline_out.empty());

    const auto exact = git_diff(test_workspace, false, {}, 3, baseline_out.size());
    ASSERT_TRUE(exact["ok"].get<bool>()) << exact["error"];
    EXPECT_FALSE(exact["truncated"].get<bool>()) << exact.dump();
    EXPECT_EQ(exact["stdout"].get<std::string>(), baseline_out) << exact.dump();
}

TEST_F(GitDiffTest, ContextLinesZeroIsHonored) {
    init_repo_with_commit("base.txt", "one\ntwo\nthree\nfour\nfive\n");
    create_file("base.txt", "one\nTWO\nthree\nfour\nfive\n");

    const auto zero_ctx = git_diff(test_workspace, false, {}, 0);
    const auto default_ctx = git_diff(test_workspace, false, {}, 3);
    ASSERT_TRUE(zero_ctx["ok"].get<bool>()) << zero_ctx["error"];
    ASSERT_TRUE(default_ctx["ok"].get<bool>()) << default_ctx["error"];

    const std::string zero_out = zero_ctx["stdout"].get<std::string>();
    const std::string default_out = default_ctx["stdout"].get<std::string>();
    EXPECT_NE(zero_out.find("@@ -2 +2 @@"), std::string::npos) << zero_out;
    EXPECT_EQ(zero_out.find("\n one\n"), std::string::npos) << zero_out;
    EXPECT_NE(default_out.find("\n one\n"), std::string::npos) << default_out;
}

TEST_F(GitDiffTest, DirectHelperClampsHugeContextLines) {
    init_repo_with_commit("base.txt", "one\ntwo\nthree\nfour\nfive\n");
    create_file("base.txt", "one\nTWO\nthree\nfour\nfive\n");

    const auto huge_ctx = git_diff(test_workspace, false, {}, 1000000);
    const auto capped_ctx = git_diff(test_workspace, false, {}, 1000);
    ASSERT_TRUE(huge_ctx["ok"].get<bool>()) << huge_ctx["error"];
    ASSERT_TRUE(capped_ctx["ok"].get<bool>()) << capped_ctx["error"];
    EXPECT_EQ(huge_ctx, capped_ctx);
}

TEST_F(GitDiffTest, ExternalDiffAndTextconvAreDisabled) {
    init_repo_with_commit();
    const fs::path helper = fs::path(test_workspace) / "fake-external-diff.sh";
    {
        std::ofstream out(helper);
        out << "#!/bin/sh\n";
        out << "printf 'external helper ran\\n'\n";
    }
    fs::permissions(helper,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    create_file("base.txt", "changed via worktree\n");
    ScopedEnvVar scoped_external_diff("GIT_EXTERNAL_DIFF", helper.string());
    const auto result = git_diff(test_workspace, false, {}, 3);

    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string out = result["stdout"].get<std::string>();
    EXPECT_EQ(out.find("external helper ran"), std::string::npos) << out;
    EXPECT_NE(out.find("diff --git"), std::string::npos) << out;
}

// ---------------------------------------------------------------------------
// GitShowTest: git_show tool
// ---------------------------------------------------------------------------

class GitShowTest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        test_workspace = (fs::temp_directory_path() /
                          ("nano_git_show_" + std::to_string(tick))).string();
        fs::create_directories(test_workspace);
    }

    void TearDown() override {
        fs::remove_all(test_workspace);
    }

    void create_file(const std::string& rel_path, const std::string& content) {
        fs::path p = fs::path(test_workspace) / rel_path;
        fs::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary);
        out << content;
    }

    void init_repo_with_commit() {
        ASSERT_EQ(run_bash("cd '" + test_workspace +
                           "' && git init -b main >/dev/null 2>&1 &&"
                           " git config user.email t@t.com &&"
                           " git config user.name T"), 0);
        create_file("hello.txt", "hello world\n");
        ASSERT_EQ(run_bash("cd '" + test_workspace +
                           "' && git add . >/dev/null 2>&1 &&"
                           " git commit -m init >/dev/null 2>&1"), 0);
    }
};

TEST_F(GitShowTest, NonRepoReturnsStructuredError) {
    const auto result = git_show(test_workspace, "HEAD", true, true, {}, 3);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["error"].get<std::string>(), "Current workspace is not a git repository.");
}

TEST_F(GitShowTest, MissingRevReturnsStructuredError) {
    init_repo_with_commit();
    const auto result = git_show(test_workspace, "", true, true, {}, 3);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("rev"), std::string::npos)
        << "error: " << result["error"];
}

TEST_F(GitShowTest, HeadCommitShowsMetadataAndPatch) {
    init_repo_with_commit();
    const auto result = git_show(test_workspace, "HEAD", true, true, {}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string& out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("commit"), std::string::npos)
        << "git show HEAD should include commit line";
}

TEST_F(GitShowTest, InvalidRevReturnsStructuredError) {
    init_repo_with_commit();
    const auto result = git_show(test_workspace, "deadbeef00000000000000000000000000000001",
                                 true, true, {}, 3);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_FALSE(result["error"].get<std::string>().empty())
        << "Invalid rev must set error field";
    EXPECT_FALSE(result["stderr"].get<std::string>().empty())
        << "Invalid rev should preserve raw stderr output";
}

TEST_F(GitShowTest, DashPrefixedRevIsRejectedBeforeGitOptionParsing) {
    init_repo_with_commit();
    const auto result = git_show(test_workspace, "--ext-diff", true, false, {}, 3);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["exit_code"].get<int>(), -1);
    EXPECT_EQ(result["error"].get<std::string>(),
              "Argument 'rev' for git_show must not start with '-'.");
    EXPECT_TRUE(result["stderr"].get<std::string>().empty());
}

TEST_F(GitShowTest, PatchCanBeDisabled) {
    init_repo_with_commit();
    const auto result = git_show(test_workspace, "HEAD", /*patch=*/false, /*stat=*/true, {}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string& out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("1 file changed"), std::string::npos)
        << "stat=true should preserve diffstat when patch=false; got: " << out;
    // With --no-patch the unified diff lines starting with --- must be absent
    EXPECT_EQ(out.find("\n---"), std::string::npos)
        << "patch=false should suppress unified diff output; got: " << out;
}

TEST_F(GitShowTest, PatchDisabledStatEnabledPreservesHeaderAndStatWithoutPatch) {
    init_repo_with_commit();
    const auto result = git_show(test_workspace, "HEAD", /*patch=*/false, /*stat=*/true, {}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string& out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("commit "), std::string::npos) << out;
    EXPECT_NE(out.find("\n\n hello.txt | 1 +"), std::string::npos) << out;
    EXPECT_NE(out.find("1 file changed"), std::string::npos) << out;
    EXPECT_EQ(out.find("diff --git"), std::string::npos) << out;
    EXPECT_EQ(out.find("\n---"), std::string::npos) << out;
}

TEST_F(GitShowTest, PathspecFiltersShownFiles) {
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com &&"
                       " git config user.name T"), 0);
    create_file("alpha.txt", "alpha content\n");
    create_file("beta.txt",  "beta content\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git add . >/dev/null 2>&1 &&"
                       " git commit -m two-files >/dev/null 2>&1"), 0);

    const auto result = git_show(test_workspace, "HEAD", true, false, {"alpha.txt"}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string& out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("alpha.txt"), std::string::npos);
    EXPECT_EQ(out.find("beta.txt"), std::string::npos)
        << "pathspec should exclude beta.txt from git show output";
}

TEST_F(GitShowTest, LargeShowOutputIsBoundedAndMarked) {
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com &&"
                       " git config user.name T"), 0);
    std::string big;
    big.reserve(6000);
    for (int i = 0; i < 250; ++i) {
        big += "line " + std::to_string(i) + " aaaa bbbb cccc dddd eeee ffff\n";
    }
    create_file("big.txt", big);
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git add . >/dev/null 2>&1 &&"
                       " git commit -m big >/dev/null 2>&1"), 0);

    const auto result = git_show(test_workspace, "HEAD", true, false, {}, 3, 300);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_EQ(result["exit_code"].get<int>(), 0) << result.dump();
    EXPECT_LE(result["stdout"].get<std::string>().size(), 300u) << result.dump();
}

TEST_F(GitShowTest, LargeSingleLineShowIsBoundedAndMarked) {
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com &&"
                       " git config user.name T"), 0);
    std::string single_line(12000, 'y');
    single_line.push_back('\n');
    create_file("big.txt", single_line);
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git add . >/dev/null 2>&1 &&"
                       " git commit -m big >/dev/null 2>&1"), 0);

    const auto result = git_show(test_workspace, "HEAD", true, false, {}, 3, 300);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_EQ(result["exit_code"].get<int>(), 0) << result.dump();
    EXPECT_LE(result["stdout"].get<std::string>().size(), 300u) << result.dump();
}

TEST_F(GitShowTest, ExactFitOutputLimitDoesNotTruncate) {
    init_repo_with_commit();

    const auto baseline = git_show(test_workspace, "HEAD", true, false, {}, 3, 0);
    ASSERT_TRUE(baseline["ok"].get<bool>()) << baseline["error"];
    const std::string baseline_out = baseline["stdout"].get<std::string>();
    ASSERT_FALSE(baseline_out.empty());

    const auto exact = git_show(test_workspace, "HEAD", true, false, {}, 3, baseline_out.size());
    ASSERT_TRUE(exact["ok"].get<bool>()) << exact["error"];
    EXPECT_FALSE(exact["truncated"].get<bool>()) << exact.dump();
    EXPECT_EQ(exact["stdout"].get<std::string>(), baseline_out) << exact.dump();
}

TEST_F(GitShowTest, StatCanBeDisabled) {
    init_repo_with_commit();
    const auto result = git_show(test_workspace, "HEAD", true, false, {}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("diff --git"), std::string::npos) << out;
    EXPECT_EQ(out.find("1 file changed"), std::string::npos) << out;
}

TEST_F(GitShowTest, PatchAndStatCanBothBeDisabled) {
    init_repo_with_commit();
    const auto result = git_show(test_workspace, "HEAD", false, false, {}, 3);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string out = result["stdout"].get<std::string>();
    EXPECT_NE(out.find("commit "), std::string::npos) << out;
    EXPECT_EQ(out.find("diff --git"), std::string::npos) << out;
    EXPECT_EQ(out.find("1 file changed"), std::string::npos) << out;
}

TEST_F(GitShowTest, OtherPatchStatCombinationsRemainUnchanged) {
    init_repo_with_commit();

    const auto patch_and_stat = git_show(test_workspace, "HEAD", true, true, {}, 3);
    ASSERT_TRUE(patch_and_stat["ok"].get<bool>()) << patch_and_stat["error"];
    const std::string patch_and_stat_out = patch_and_stat["stdout"].get<std::string>();
    EXPECT_NE(patch_and_stat_out.find("diff --git"), std::string::npos) << patch_and_stat_out;
    EXPECT_NE(patch_and_stat_out.find("1 file changed"), std::string::npos) << patch_and_stat_out;

    const auto patch_only = git_show(test_workspace, "HEAD", true, false, {}, 3);
    ASSERT_TRUE(patch_only["ok"].get<bool>()) << patch_only["error"];
    const std::string patch_only_out = patch_only["stdout"].get<std::string>();
    EXPECT_NE(patch_only_out.find("diff --git"), std::string::npos) << patch_only_out;
    EXPECT_EQ(patch_only_out.find("1 file changed"), std::string::npos) << patch_only_out;

    const auto header_only = git_show(test_workspace, "HEAD", false, false, {}, 3);
    ASSERT_TRUE(header_only["ok"].get<bool>()) << header_only["error"];
    const std::string header_only_out = header_only["stdout"].get<std::string>();
    EXPECT_NE(header_only_out.find("commit "), std::string::npos) << header_only_out;
    EXPECT_EQ(header_only_out.find("diff --git"), std::string::npos) << header_only_out;
    EXPECT_EQ(header_only_out.find("1 file changed"), std::string::npos) << header_only_out;
}

TEST_F(GitShowTest, ContextLinesZeroIsHonored) {
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com &&"
                       " git config user.name T"), 0);
    create_file("ctx.txt", "one\ntwo\nthree\nfour\nfive\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add ctx.txt >/dev/null 2>&1 && git commit -m ctx >/dev/null 2>&1"), 0);
    create_file("ctx.txt", "one\nTWO\nthree\nfour\nfive\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add ctx.txt >/dev/null 2>&1 && git commit -m ctx2 >/dev/null 2>&1"), 0);

    const auto zero_ctx = git_show(test_workspace, "HEAD", true, false, {}, 0);
    const auto default_ctx = git_show(test_workspace, "HEAD", true, false, {}, 3);
    ASSERT_TRUE(zero_ctx["ok"].get<bool>()) << zero_ctx["error"];
    ASSERT_TRUE(default_ctx["ok"].get<bool>()) << default_ctx["error"];

    const std::string zero_out = zero_ctx["stdout"].get<std::string>();
    const std::string default_out = default_ctx["stdout"].get<std::string>();
    EXPECT_NE(zero_out.find("@@ -2 +2 @@"), std::string::npos) << zero_out;
    EXPECT_EQ(zero_out.find("\n one\n"), std::string::npos) << zero_out;
    EXPECT_NE(default_out.find("\n one\n"), std::string::npos) << default_out;
}

TEST_F(GitShowTest, DirectHelperClampsHugeContextLines) {
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git init -b main >/dev/null 2>&1 &&"
                       " git config user.email t@t.com &&"
                       " git config user.name T"), 0);
    create_file("ctx.txt", "one\ntwo\nthree\nfour\nfive\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add ctx.txt >/dev/null 2>&1 && git commit -m ctx >/dev/null 2>&1"), 0);
    create_file("ctx.txt", "one\nTWO\nthree\nfour\nfive\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add ctx.txt >/dev/null 2>&1 && git commit -m ctx2 >/dev/null 2>&1"), 0);

    const auto huge_ctx = git_show(test_workspace, "HEAD", true, false, {}, 1000000);
    const auto capped_ctx = git_show(test_workspace, "HEAD", true, false, {}, 1000);
    ASSERT_TRUE(huge_ctx["ok"].get<bool>()) << huge_ctx["error"];
    ASSERT_TRUE(capped_ctx["ok"].get<bool>()) << capped_ctx["error"];
    EXPECT_EQ(huge_ctx, capped_ctx);
}

TEST_F(GitShowTest, ExternalDiffAndTextconvAreDisabled) {
    init_repo_with_commit();
    const fs::path helper = fs::path(test_workspace) / "fake-show-diff.sh";
    {
        std::ofstream out(helper);
        out << "#!/bin/sh\n";
        out << "printf 'external helper ran\\n'\n";
    }
    fs::permissions(helper,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    ScopedEnvVar scoped_external_diff("GIT_EXTERNAL_DIFF", helper.string());
    const auto result = git_show(test_workspace, "HEAD", true, false, {}, 3);

    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    const std::string out = result["stdout"].get<std::string>();
    EXPECT_EQ(out.find("external helper ran"), std::string::npos) << out;
    EXPECT_NE(out.find("diff --git"), std::string::npos) << out;
}

TEST_F(GitShowTest, DashPrefixedRevCannotBypassExternalDiffHardening) {
    init_repo_with_commit();
    const fs::path helper = fs::path(test_workspace) / "fake-show-diff-bypass.sh";
    {
        std::ofstream out(helper);
        out << "#!/bin/sh\n";
        out << "printf 'external helper ran\\n'\n";
    }
    fs::permissions(helper,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    ScopedEnvVar scoped_external_diff("GIT_EXTERNAL_DIFF", helper.string());
    const auto result = git_show(test_workspace, "--ext-diff", true, false, {}, 3);

    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["error"].get<std::string>(),
              "Argument 'rev' for git_show must not start with '-'.");
    EXPECT_TRUE(result["stderr"].get<std::string>().empty());
}
