#include <gtest/gtest.h>
#include "cli.hpp"
#include "config.hpp"
#include <getopt.h>

class CliTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset getopt_long internal state for each test
        optind = 1;
    }
};

TEST_F(CliTest, ParseHelp) {
    AgentConfig config;
    const char* argv[] = {"agent", "--help"};
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    EXPECT_EQ(cli_parse(argc, const_cast<char**>(argv), config), CliResult::ExitSuccess);
}

TEST_F(CliTest, ParseExecute) {
    AgentConfig config;
    const char* argv[] = {"agent", "-e", "test prompt"};
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    EXPECT_EQ(cli_parse(argc, const_cast<char**>(argv), config), CliResult::Success);
    EXPECT_EQ(config.prompt, "test prompt");
}

TEST_F(CliTest, ParseMissingExecute) {
    AgentConfig config;
    const char* argv[] = {"agent", "--model", "gpt-4"};
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    EXPECT_EQ(cli_parse(argc, const_cast<char**>(argv), config), CliResult::ExitFailure);
}

TEST_F(CliTest, ParseAllOptions) {
    AgentConfig config;
    const char* argv[] = {
        "agent", 
        "-e", "complex task",
        "-w", "/tmp/workspace",
        "--model", "claude-3",
        "--api-key", "sk-12345",
        "--base-url", "https://api.anthropic.com",
        "--debug",
        "--config", "conf.ini"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    EXPECT_EQ(cli_parse(argc, const_cast<char**>(argv), config), CliResult::Success);
    EXPECT_EQ(config.prompt, "complex task");
    EXPECT_EQ(config.workspace, "/tmp/workspace");
    EXPECT_EQ(config.model, "claude-3");
    EXPECT_EQ(config.config_file_path, "conf.ini");
    EXPECT_EQ(config.api_key.value_or(""), "sk-12345");
    EXPECT_EQ(config.base_url, "https://api.anthropic.com");
    EXPECT_TRUE(config.debug_mode);
}

TEST_F(CliTest, ParseApprovalFlags) {
    AgentConfig config;
    const char* argv[] = {
        "agent",
        "-e", "approval task",
        "--allow-mutating-tools",
        "--allow-execution-tools"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    EXPECT_EQ(cli_parse(argc, const_cast<char**>(argv), config), CliResult::Success);
    EXPECT_TRUE(config.allow_mutating_tools);
    EXPECT_TRUE(config.allow_execution_tools);
}

TEST_F(CliTest, ParseRepeatableSkillFlags) {
    AgentConfig config;
    config.enabled_skills = {"from-env"};
    const char* argv[] = {
        "agent",
        "-e", "skill task",
        "--skill", "docgen-reviewer",
        "--skill", "docgen-fact-check"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    EXPECT_EQ(cli_parse(argc, const_cast<char**>(argv), config), CliResult::Success);
    ASSERT_EQ(config.enabled_skills.size(), 2u);
    EXPECT_EQ(config.enabled_skills[0], "docgen-reviewer");
    EXPECT_EQ(config.enabled_skills[1], "docgen-fact-check");
}

TEST_F(CliTest, ParseSessionFile) {
    AgentConfig config;
    const char* argv[] = {
        "agent",
        "-e", "session task",
        "--session-file", "tmp/session.json"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    EXPECT_EQ(cli_parse(argc, const_cast<char**>(argv), config), CliResult::Success);
    ASSERT_TRUE(config.session_file.has_value());
    EXPECT_EQ(*config.session_file, "tmp/session.json");
}

TEST_F(CliTest, ParseRepeatableMcpServerFlags) {
    AgentConfig config;
    config.mcp_servers = {"from-env=python3 old.py"};
    const char* argv[] = {
        "agent",
        "-e", "mcp task",
        "--mcp-server", "alpha=python3 server_a.py",
        "--mcp-server", "beta=python3 server_b.py"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    EXPECT_EQ(cli_parse(argc, const_cast<char**>(argv), config), CliResult::Success);
    ASSERT_EQ(config.mcp_servers.size(), 2u);
    EXPECT_EQ(config.mcp_servers[0], "alpha=python3 server_a.py");
    EXPECT_EQ(config.mcp_servers[1], "beta=python3 server_b.py");
}
