#include <gtest/gtest.h>
#include "config.hpp"
#include "cli.hpp"
#include <fstream>
#include <cstdlib>

class ConfigPrecedenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset environment
        unsetenv("NCA_MODEL");
        unsetenv("NCA_API_KEY");
        unsetenv("NCA_BASE_URL");
        unsetenv("NCA_WORKSPACE");
        unsetenv("NCA_DEBUG");
        unsetenv("NCA_CONFIG");
        unsetenv("NCA_ALLOW_MUTATING_TOOLS");
        unsetenv("NCA_ALLOW_EXECUTION_TOOLS");
        unsetenv("NCA_SKILLS");
        unsetenv("NCA_SESSION_FILE");

        // Create a fake config file
        std::ofstream out("test_conf.ini");
        out << "model = conf-gpt\n";
        out << "api_key= conf-123\n";
        out << "base_url = conf-url\n";
        out << "workspace = /conf/workspace\n";
        out << "debug = true\n";
        out << "allow_mutating_tools = true\n";
        out << "allow_execution_tools = false\n";
        out << "skills = docgen-reviewer, docgen-fact-check\n";
        out << "session_file = conf-session.json\n";
        out.close();
    }

    void TearDown() override {
        std::remove("test_conf.ini");
    }
};

TEST_F(ConfigPrecedenceTest, DefaultsOnly) {
    const char* argv[] = {"agent"};
    int argc = 1;
    AgentConfig cfg = config_init(argc, const_cast<char**>(argv));
    
    EXPECT_EQ(cfg.model, "gpt-4o");
    EXPECT_FALSE(cfg.api_key.has_value());
    EXPECT_EQ(cfg.workspace, ".");
    EXPECT_FALSE(cfg.debug_mode);
    EXPECT_FALSE(cfg.allow_mutating_tools);
    EXPECT_FALSE(cfg.allow_execution_tools);
    EXPECT_TRUE(cfg.enabled_skills.empty());
    EXPECT_FALSE(cfg.session_file.has_value());
}

TEST_F(ConfigPrecedenceTest, ConfigFileOverridesDefaults) {
    setenv("NCA_CONFIG", "test_conf.ini", 1);
    const char* argv[] = {"agent"};
    int argc = 1;
    AgentConfig cfg = config_init(argc, const_cast<char**>(argv));
    
    EXPECT_EQ(cfg.model, "conf-gpt");
    EXPECT_EQ(cfg.api_key.value_or(""), "conf-123");
    EXPECT_EQ(cfg.base_url, "conf-url");
    EXPECT_EQ(cfg.workspace, "/conf/workspace");
    EXPECT_TRUE(cfg.debug_mode);
    EXPECT_TRUE(cfg.allow_mutating_tools);
    EXPECT_FALSE(cfg.allow_execution_tools);
    ASSERT_EQ(cfg.enabled_skills.size(), 2u);
    EXPECT_EQ(cfg.enabled_skills[0], "docgen-reviewer");
    EXPECT_EQ(cfg.enabled_skills[1], "docgen-fact-check");
    ASSERT_TRUE(cfg.session_file.has_value());
    EXPECT_EQ(*cfg.session_file, "conf-session.json");
}

TEST_F(ConfigPrecedenceTest, EnvOverridesConfigFile) {
    setenv("NCA_CONFIG", "test_conf.ini", 1);
    setenv("NCA_MODEL", "env-gpt", 1);
    setenv("NCA_API_KEY", "env-api", 1);
    setenv("NCA_ALLOW_MUTATING_TOOLS", "0", 1);
    setenv("NCA_ALLOW_EXECUTION_TOOLS", "1", 1);
    setenv("NCA_SKILLS", "docgen-overview-writer,docgen-reviewer", 1);
    setenv("NCA_SESSION_FILE", "env-session.json", 1);
    const char* argv[] = {"agent"};
    int argc = 1;
    AgentConfig cfg = config_init(argc, const_cast<char**>(argv));
    
    // Env > Config
    EXPECT_EQ(cfg.model, "env-gpt");
    EXPECT_EQ(cfg.api_key.value_or(""), "env-api");
    // Rest remain from Config
    EXPECT_EQ(cfg.base_url, "conf-url");
    EXPECT_FALSE(cfg.allow_mutating_tools);
    EXPECT_TRUE(cfg.allow_execution_tools);
    ASSERT_EQ(cfg.enabled_skills.size(), 2u);
    EXPECT_EQ(cfg.enabled_skills[0], "docgen-overview-writer");
    EXPECT_EQ(cfg.enabled_skills[1], "docgen-reviewer");
    ASSERT_TRUE(cfg.session_file.has_value());
    EXPECT_EQ(*cfg.session_file, "env-session.json");
}

TEST_F(ConfigPrecedenceTest, CliOverridesEnv) {
    setenv("NCA_CONFIG", "test_conf.ini", 1);
    setenv("NCA_MODEL", "env-gpt", 1);
    setenv("NCA_ALLOW_MUTATING_TOOLS", "0", 1);
    setenv("NCA_SKILLS", "env-skill", 1);
    setenv("NCA_SESSION_FILE", "env-session.json", 1);

    const char* argv[] = {
        "agent", "--model", "cli-gpt", "--allow-mutating-tools", "--skill", "cli-skill-one",
        "--skill", "cli-skill-two", "--session-file", "cli-session.json", "-e", "test"
    };
    int argc = 12;
    
    // Config loader handles defaults, config file, and env
    AgentConfig cfg = config_init(argc, const_cast<char**>(argv));
    
    EXPECT_EQ(cfg.model, "env-gpt"); // Before CLI parse
    EXPECT_FALSE(cfg.allow_mutating_tools);
    ASSERT_EQ(cfg.enabled_skills.size(), 1u);
    EXPECT_EQ(cfg.enabled_skills[0], "env-skill");
    ASSERT_TRUE(cfg.session_file.has_value());
    EXPECT_EQ(cfg.session_file.value(), "env-session.json");

    // Parse CLI options
    cli_parse(argc, const_cast<char**>(argv), cfg);
    
    EXPECT_EQ(cfg.model, "cli-gpt"); // After CLI parse
    EXPECT_EQ(cfg.api_key.value_or(""), "conf-123");
    EXPECT_TRUE(cfg.allow_mutating_tools);
    ASSERT_EQ(cfg.enabled_skills.size(), 2u);
    EXPECT_EQ(cfg.enabled_skills[0], "cli-skill-one");
    EXPECT_EQ(cfg.enabled_skills[1], "cli-skill-two");
    ASSERT_TRUE(cfg.session_file.has_value());
    EXPECT_EQ(cfg.session_file.value(), "cli-session.json");
}
