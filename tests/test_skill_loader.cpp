#include <gtest/gtest.h>

#include "agent_loop.hpp"
#include "agent_tools.hpp"
#include "config.hpp"
#include "skill_loader.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

std::vector<std::string> schema_tool_names(const nlohmann::json& schema) {
    std::vector<std::string> names;
    for (const auto& tool : schema) {
        names.push_back(tool["function"]["name"].get<std::string>());
    }
    return names;
}

bool contains_name(const std::vector<std::string>& values, const std::string& name) {
    return std::find(values.begin(), values.end(), name) != values.end();
}

bool message_content_contains(const nlohmann::json& messages,
                              const std::string& role,
                              const std::string& needle) {
    for (const auto& message : messages) {
        if (message.value("role", "") != role) {
            continue;
        }
        if (message.value("content", "").find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

nlohmann::json runtime_schema(const AgentConfig& config) {
    return get_default_tool_registry().to_openai_schema(config);
}

} // namespace

class SkillLoaderTest : public ::testing::Test {
protected:
    std::string workspace;

    void SetUp() override {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        workspace = (std::filesystem::temp_directory_path() /
                     ("nano_skill_loader_" + std::to_string(unique))).string();
        std::filesystem::create_directories(workspace);
    }

    void TearDown() override {
        std::filesystem::remove_all(workspace);
    }

    void write_file(const std::string& rel_path, const std::string& content) {
        const auto path = std::filesystem::path(workspace) / rel_path;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    static std::string skill_with_list(const std::string& name,
                                       const std::string& description,
                                       const std::vector<std::string>& tools,
                                       const std::string& body) {
        std::string content;
        content += "---\n";
        content += "name: " + name + "\n";
        content += "description: " + description + "\n";
        if (!tools.empty()) {
            content += "allowed-tools:\n";
            for (const std::string& tool : tools) {
                content += "  - " + tool + "\n";
            }
        }
        content += "---\n\n";
        content += body;
        if (content.back() != '\n') {
            content += '\n';
        }
        return content;
    }

    static std::string skill_with_raw_allowed_tools(const std::string& name,
                                                    const std::string& description,
                                                    const std::string& allowed_tools_line,
                                                    const std::string& body) {
        std::string content;
        content += "---\n";
        content += "name: " + name + "\n";
        content += "description: " + description + "\n";
        content += allowed_tools_line + "\n";
        content += "---\n\n";
        content += body;
        if (content.back() != '\n') {
            content += '\n';
        }
        return content;
    }
};

TEST_F(SkillLoaderTest, SkillAllowedToolsDoNotBlockExecution) {
    write_file(".agents/skills/reviewer/SKILL.md",
               skill_with_list("reviewer",
                                "Read-only review skill",
                               {"Bash", "Read"},
                               "# Purpose\n\nReview the repository before answering.\n"));

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"reviewer"}, &context, &err)) << err;

    AgentConfig config;
    config.workspace_abs = workspace;
    config.max_turns = 3;
    config.allow_mutating_tools = true;

    std::string system_prompt;
    ASSERT_TRUE(loader.build_system_prompt("Base system prompt.",
                                           context,
                                           config.max_skill_prompt_bytes,
                                           &system_prompt,
                                           &err)) << err;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "write_ok"},
                    {"function", {
                        {"name", "write_file_safe"},
                        {"arguments", "{\"path\":\"blocked.txt\",\"content\":\"x\"}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, system_prompt, "Inspect the repository", runtime_schema(config), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(workspace) / "blocked.txt"));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"ok\":true"));
}

TEST_F(SkillLoaderTest, SkillAllowedToolsDoNotFilterSchema) {
    write_file(".agents/skills/reviewer/SKILL.md",
               skill_with_list("reviewer",
                               "Read-only review skill",
                               {"Read"},
                               "# Purpose\n\nReview the repository before answering.\n"));

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"reviewer"}, &context, &err)) << err;

    AgentConfig config;
    config.allow_mutating_tools = true;
    const auto schema = runtime_schema(config);
    EXPECT_TRUE(contains_name(schema_tool_names(schema), "read_file_safe"));
    EXPECT_TRUE(contains_name(schema_tool_names(schema), "write_file_safe"));
}

TEST_F(SkillLoaderTest, AdvisoryAllowedToolsAreInjectedIntoPrompt) {
    write_file(".agents/skills/reviewer/SKILL.md",
               skill_with_list("reviewer",
                               "Read-only review skill",
                               {"read_file", "grep"},
                               "# Purpose\n\nReview the repository before answering.\n"));

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"reviewer"}, &context, &err)) << err;

    std::string prompt;
    ASSERT_TRUE(loader.build_system_prompt("Base.", context, 4096, &prompt, &err)) << err;
    EXPECT_NE(prompt.find("Preferred tools for this skill: read_file, grep"), std::string::npos);
    EXPECT_EQ(prompt.find("must use"), std::string::npos);
}

TEST_F(SkillLoaderTest, EmptyAllowedToolsMeansNoPreference) {
    write_file(".agents/skills/reviewer/SKILL.md",
               skill_with_raw_allowed_tools("reviewer",
                                            "Read-only review skill",
                                            "allowed-tools: []",
                                            "# Purpose\n\nReview the repository before answering.\n"));

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"reviewer"}, &context, &err)) << err;
    ASSERT_EQ(context.enabled_skills.size(), 1u);
    ASSERT_TRUE(context.enabled_skills[0].manifest.allowed_tools.has_value());
    EXPECT_TRUE(context.enabled_skills[0].manifest.allowed_tools->empty());

    std::string prompt;
    ASSERT_TRUE(loader.build_system_prompt("Base.", context, 4096, &prompt, &err)) << err;
    EXPECT_EQ(prompt.find("Preferred tools for this skill:"), std::string::npos);
}

TEST_F(SkillLoaderTest, MalformedAllowedToolsScalarWarnsAndContinues) {
    write_file(".agents/skills/bad-scalar/SKILL.md",
               "---\n"
                "name: bad-scalar\n"
               "description: Bad scalar\n"
               "allowed-tools: Bash, Read\n"
               "---\n\n"
               "# Purpose\n\nBroken.\n");

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"bad-scalar"}, &context, &err)) << err;
    ASSERT_EQ(context.enabled_skills.size(), 1u);
    ASSERT_EQ(context.enabled_skills[0].warnings.size(), 1u);
    EXPECT_NE(context.enabled_skills[0].warnings[0].find("Malformed allowed-tools"), std::string::npos);
    EXPECT_NE(context.enabled_skills[0].warnings[0].find("Ignoring this advisory field"), std::string::npos);

    std::string prompt;
    ASSERT_TRUE(loader.build_system_prompt("Base.", context, 4096, &prompt, &err)) << err;
    EXPECT_NE(prompt.find("[Skill: bad-scalar]"), std::string::npos);
    EXPECT_NE(prompt.find("# Purpose\n\nBroken."), std::string::npos);
    EXPECT_EQ(prompt.find("Preferred tools for this skill:"), std::string::npos);
}

TEST_F(SkillLoaderTest, MalformedAllowedToolsListWarnsAndContinues) {
    write_file(".agents/skills/bad-indent/SKILL.md",
                "---\n"
                "name: bad-indent\n"
               "description: Bad indent\n"
               "allowed-tools:\n"
               "- Read\n"
               "---\n\n"
               "# Purpose\n\nBroken.\n");

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"bad-indent"}, &context, &err)) << err;
    ASSERT_EQ(context.enabled_skills.size(), 1u);
    ASSERT_EQ(context.enabled_skills[0].warnings.size(), 1u);
    EXPECT_NE(context.enabled_skills[0].warnings[0].find("expected indented YAML list items"), std::string::npos);
}

TEST_F(SkillLoaderTest, NearMissAllowedToolsKeyWarnsAndContinues) {
    write_file(".agents/skills/bad-typo/SKILL.md",
                "---\n"
                "name: bad-typo\n"
               "description: Bad typo\n"
               "allowed_tool:\n"
               "  - Read\n"
               "---\n\n"
               "# Purpose\n\nBroken.\n");

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"bad-typo"}, &context, &err)) << err;
    ASSERT_EQ(context.enabled_skills.size(), 1u);
    ASSERT_EQ(context.enabled_skills[0].warnings.size(), 1u);
    EXPECT_NE(context.enabled_skills[0].warnings[0].find("ignored allowed-tools typo"), std::string::npos);
}

TEST_F(SkillLoaderTest, WorkspaceSubdirectoryStillFindsRepoRootSkills) {
    write_file(".git/HEAD", "ref: refs/heads/main\n");
    write_file(".agents/skills/reviewer/SKILL.md",
                skill_with_list("reviewer",
                                "Read-only review skill",
                               {"Read"},
                               "# Purpose\n\nReview the repository before answering.\n"));
    const auto nested_workspace = std::filesystem::path(workspace) / "src" / "nested" / "module";
    std::filesystem::create_directories(nested_workspace);

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(nested_workspace.string(), {"reviewer"}, &context, &err)) << err;

    EXPECT_EQ(context.repo_root, workspace);
    EXPECT_FALSE(context.used_workspace_fallback);
    ASSERT_EQ(context.enabled_skills.size(), 1u);
    EXPECT_EQ(context.enabled_skills[0].manifest.name, "reviewer");
}

TEST_F(SkillLoaderTest, GitRepoRootIsPreferredOverAgentsSignal) {
    write_file(".git/HEAD", "ref: refs/heads/main\n");
    write_file("AGENTS.md", "# Root guidance\n");
    write_file(".agents/skills/root-skill/SKILL.md",
               skill_with_list("root-skill",
                               "Root skill",
                               {"Read"},
                               "# Purpose\n\nUse root skill.\n"));

    const auto nested_workspace = std::filesystem::path(workspace) / "src" / "nested" / "module";
    std::filesystem::create_directories(nested_workspace);

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(nested_workspace.string(), {"root-skill"}, &context, &err)) << err;
    EXPECT_EQ(context.repo_root, workspace);
    EXPECT_FALSE(context.used_workspace_fallback);
}

TEST_F(SkillLoaderTest, RepoRootDiscoveryIgnoresNestedSkillsDirectory) {
    write_file(".git/HEAD", "ref: refs/heads/main\n");
    write_file(".agents/skills/root-skill/SKILL.md",
                skill_with_list("root-skill",
                                "Root skill",
                               {"Read"},
                               "# Purpose\n\nUse root skill.\n"));
    write_file("src/nested/.agents/skills/nested-skill/SKILL.md",
               skill_with_list("nested-skill",
                               "Nested skill",
                               {"Read"},
                               "# Purpose\n\nDo not use nested skill.\n"));

    const auto nested_workspace = std::filesystem::path(workspace) / "src" / "nested" / "module";
    std::filesystem::create_directories(nested_workspace);

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(nested_workspace.string(), {"root-skill"}, &context, &err)) << err;
    EXPECT_FALSE(contains_name(context.discovered_skill_names, "nested-skill"));

    EXPECT_FALSE(loader.load_runtime_context(nested_workspace.string(), {"nested-skill"}, &context, &err));
    EXPECT_NE(err.find("Requested skill 'nested-skill' was not found under .agents/skills/."), std::string::npos);
}

TEST_F(SkillLoaderTest, WrongDotAgentPathIsNotDiscovered) {
    write_file(".agent/skills/wrong/SKILL.md",
               skill_with_list("wrong",
                               "Wrong path skill",
                               {"Read"},
                               "# Purpose\n\nWrong path.\n"));

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {}, &context, &err)) << err;
    EXPECT_TRUE(context.discovered_skill_names.empty());

    EXPECT_FALSE(loader.load_runtime_context(workspace, {"wrong"}, &context, &err));
    EXPECT_NE(err.find("Requested skill 'wrong' was not found under .agents/skills/."), std::string::npos);
}

TEST_F(SkillLoaderTest, UnenabledBrokenSkillDoesNotAffectRun) {
    write_file(".agents/skills/good/SKILL.md",
               skill_with_list("good",
                               "Good skill",
                               {"Read"},
                               "# Purpose\n\nSafe skill.\n"));
    write_file(".agents/skills/bad/SKILL.md",
               "---\n"
               "name: bad\n"
               "description: Broken\n"
               "allowed-tools: Bash, Read\n"
               "---\n\n"
               "# Purpose\n\nBroken.\n");

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;

    ASSERT_TRUE(loader.load_runtime_context(workspace, {}, &context, &err)) << err;
    EXPECT_TRUE(context.enabled_skills.empty());

    ASSERT_TRUE(loader.load_runtime_context(workspace, {"good"}, &context, &err)) << err;
    ASSERT_EQ(context.enabled_skills.size(), 1u);
    EXPECT_EQ(context.enabled_skills[0].manifest.name, "good");
}

TEST_F(SkillLoaderTest, MultiSkillOrderIsStableAndDuplicateRequestsAreRejected) {
    write_file(".agents/skills/first/SKILL.md",
               skill_with_list("first",
                               "First skill",
                               {"Read"},
                               "# Purpose\n\nFirst body.\n"));
    write_file(".agents/skills/second/SKILL.md",
               skill_with_list("second",
                               "Second skill",
                               {"Write"},
                               "# Purpose\n\nSecond body.\n"));

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"second", "first"}, &context, &err)) << err;
    ASSERT_EQ(context.enabled_skills.size(), 2u);
    EXPECT_EQ(context.enabled_skills[0].manifest.name, "second");
    EXPECT_EQ(context.enabled_skills[1].manifest.name, "first");

    std::string prompt;
    ASSERT_TRUE(loader.build_system_prompt("Base.", context, 4096, &prompt, &err)) << err;
    const std::size_t second_pos = prompt.find("[Skill: second]");
    const std::size_t first_pos = prompt.find("[Skill: first]");
    ASSERT_NE(second_pos, std::string::npos);
    ASSERT_NE(first_pos, std::string::npos);
    EXPECT_LT(second_pos, first_pos);

    EXPECT_FALSE(loader.load_runtime_context(workspace, {"first", "first"}, &context, &err));
    EXPECT_NE(err.find("Duplicate requested skill 'first'"), std::string::npos);
}

TEST_F(SkillLoaderTest, TooManyRequestedSkillsFailsBeforeInjection) {
    for (std::size_t i = 0; i < SkillLoader::kMaxEnabledSkills + 1; ++i) {
        const std::string name = "skill-" + std::to_string(i);
        write_file(".agents/skills/" + name + "/SKILL.md",
                   skill_with_list(name,
                                   "Generated skill",
                                   {"Read"},
                                   "# Purpose\n\nGenerated.\n"));
    }

    std::vector<std::string> requested;
    for (std::size_t i = 0; i < SkillLoader::kMaxEnabledSkills + 1; ++i) {
        requested.push_back("skill-" + std::to_string(i));
    }

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    EXPECT_FALSE(loader.load_runtime_context(workspace, requested, &context, &err));
    EXPECT_NE(err.find("runtime limit is"), std::string::npos);
}

TEST_F(SkillLoaderTest, CombinedInjectedPromptSizeIsCapped) {
    write_file(".agents/skills/first/SKILL.md",
               skill_with_list("first",
                               "First skill",
                               {"Read"},
                               std::string("# Purpose\n\n") + std::string(300, 'a') + "\n"));
    write_file(".agents/skills/second/SKILL.md",
               skill_with_list("second",
                               "Second skill",
                               {"Read"},
                               std::string("# Purpose\n\n") + std::string(300, 'b') + "\n"));

    SkillLoader loader(get_default_tool_registry());
    SkillRuntimeContext context;
    std::string err;
    ASSERT_TRUE(loader.load_runtime_context(workspace, {"first", "second"}, &context, &err)) << err;

    std::string prompt;
    EXPECT_FALSE(loader.build_system_prompt("Base.", context, 128, &prompt, &err));
    EXPECT_NE(err.find("max_skill_prompt_bytes"), std::string::npos);
}
