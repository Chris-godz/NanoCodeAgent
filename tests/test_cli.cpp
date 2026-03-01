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
