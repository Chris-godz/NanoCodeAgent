#include <gtest/gtest.h>
#include "workspace.hpp"
#include "config.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class WorkspaceTest : public ::testing::Test {
protected:
    std::string temp_dir = "./test_ws_tmp";

    void SetUp() override {
        fs::remove_all(temp_dir);
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }
};

TEST_F(WorkspaceTest, InitCreatesAndCanonicalizes) {
    AgentConfig cfg;
    cfg.workspace = temp_dir + "/subdir";
    
    std::string err;
    EXPECT_TRUE(workspace_init(&cfg, &err));
    EXPECT_TRUE(fs::exists(cfg.workspace));
    
    fs::path expected_abs = fs::weakly_canonical(cfg.workspace);
    EXPECT_EQ(cfg.workspace_abs, expected_abs.string());
}

TEST_F(WorkspaceTest, ResolveValidPaths) {
    AgentConfig cfg;
    cfg.workspace = temp_dir;
    std::string err;
    ASSERT_TRUE(workspace_init(&cfg, &err));

    std::string out;
    EXPECT_TRUE(workspace_resolve(cfg, "foo.txt", &out, &err));
    
    fs::path expected = fs::path(cfg.workspace_abs) / "foo.txt";
    EXPECT_EQ(out, expected.lexically_normal().string());
    
    EXPECT_TRUE(workspace_resolve(cfg, "sub/bar.cpp", &out, &err));
}

TEST_F(WorkspaceTest, ResolveRejectsAbsolute) {
    AgentConfig cfg;
    cfg.workspace = temp_dir;
    std::string err;
    ASSERT_TRUE(workspace_init(&cfg, &err));

    std::string out;
#ifdef _WIN32
    EXPECT_FALSE(workspace_resolve(cfg, "C:\\Windows\\System32", &out, &err));
#else
    EXPECT_FALSE(workspace_resolve(cfg, "/etc/passwd", &out, &err));
#endif
    EXPECT_NE(err.find("absolute"), std::string::npos);
}

TEST_F(WorkspaceTest, ResolveRejectsEscape) {
    AgentConfig cfg;
    cfg.workspace = temp_dir;
    std::string err;
    ASSERT_TRUE(workspace_init(&cfg, &err));

    std::string out;
    // Basic escape
    EXPECT_FALSE(workspace_resolve(cfg, "../evil.sh", &out, &err));
    EXPECT_NE(err.find("escapes"), std::string::npos);
    
    // Complex escape
    EXPECT_FALSE(workspace_resolve(cfg, "sub/../../evil.sh", &out, &err));
}

TEST_F(WorkspaceTest, ResolveAllowsInternalDotDot) {
    AgentConfig cfg;
    cfg.workspace = temp_dir;
    std::string err;
    ASSERT_TRUE(workspace_init(&cfg, &err));

    std::string out;
    // Moving up but still inside
    EXPECT_TRUE(workspace_resolve(cfg, "sub/../foo.txt", &out, &err));
}
