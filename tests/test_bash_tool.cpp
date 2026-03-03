#include <gtest/gtest.h>
#include "bash_tool.hpp"
#include <filesystem>

namespace fs = std::filesystem;

class BashToolTest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        test_workspace = fs::absolute(fs::path("test_ws_bash")).string();
        fs::create_directories(test_workspace);
    }

    void TearDown() override {
        fs::remove_all(test_workspace);
    }
};

TEST_F(BashToolTest, BashTimeoutKills) {
    // Attempt standard sleeper sequence heavily breaching bounds
    auto res = bash_execute_safe(test_workspace, "sleep 5", 200); // 200 ms restriction
    
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.timed_out);
    EXPECT_FALSE(res.truncated);
    // killpg should have delivered a hard SIGKILL code (usually exit code via signal sets to 128 + 9 = 137)
    EXPECT_EQ(res.exit_code, 137);
}

TEST_F(BashToolTest, BashStdoutLimitKills) {
    // "yes" command inherently writes "y\n" infinitely triggering MAX_STDOUT_BYTES checks
    auto res = bash_execute_safe(test_workspace, "yes", 5000, 1024); // 1KB threshold limit max_out
    
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.truncated);
    EXPECT_FALSE(res.timed_out);
    
    EXPECT_GE(res.out_bytes, 1024); // Out Bytes precisely matched condition triggering cap
    EXPECT_NE(res.err.find("Stdout bandwidth"), std::string::npos);
}

TEST_F(BashToolTest, BashStderrLimitKills) {
    // Using simple shell loop printing straight to 2 descriptor
    auto res = bash_execute_safe(test_workspace, "sh -lc 'yes 1>&2'", 5000, 1024, 512); // err max is 512 bytes limit

    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.truncated);
    // Ensures accurate accounting isolation to secondary descriptor pipe
    EXPECT_GE(res.err_bytes, 512); 
    EXPECT_NE(res.err.find("Stderr bandwidth"), std::string::npos);
}

TEST_F(BashToolTest, BashCwdLocked) {
    auto res = bash_execute_safe(test_workspace, "pwd");
    
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.exit_code, 0);

    // Standard raw newline stripping technique on read pipelines
    std::string clean_out = res.out_tail;
    if (!clean_out.empty() && clean_out.back() == '\n') {
        clean_out.pop_back();
    }
    
    EXPECT_EQ(clean_out, test_workspace);
}

TEST_F(BashToolTest, BashDualPipeNoDeadlock) {
    // This creates highly contested rapid writes on both ends interleaving outputs
    // Standard synchronous pipelines failing fcntl NON_BLOCK checks inherently freeze here.
    std::string heavy_cmd = "sh -lc 'for i in $(seq 1 5000); do echo o; echo e 1>&2; done'";
    
    auto res = bash_execute_safe(test_workspace, heavy_cmd, 10000, 1024 * 1024, 1024 * 1024);
    
    EXPECT_TRUE(res.ok);
    EXPECT_FALSE(res.truncated);
    EXPECT_FALSE(res.timed_out);
    EXPECT_EQ(res.exit_code, 0);
    
    EXPECT_GT(res.out_bytes, 9000); 
    EXPECT_GT(res.err_bytes, 9000);
}

TEST_F(BashToolTest, DangerousPatternRejected) {
    auto res = bash_execute_safe(test_workspace, "rm -rf / || return 0", 1000, 1024, 1024);
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.err.find("Command rejected: Contains obvious dangerous pattern"), std::string::npos);
    EXPECT_EQ(res.exit_code, -1);
}

TEST_F(BashToolTest, EnvironmentIsCleaned) {
    setenv("LD_PRELOAD", "dummy.so", 1);
    auto res = bash_execute_safe(test_workspace, "echo ${LD_PRELOAD:-EMPTY}", 1000, 1024, 1024);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.out_tail, "EMPTY\n");
    unsetenv("LD_PRELOAD");
}

TEST_F(BashToolTest, KillpgKillsBackground) {
    // Spawns a background sleep and waits. If killpg is bypassed or fails, this script would hang or leak.
    auto res = bash_execute_safe(test_workspace, "sh -lc 'sleep 100 & wait'", 200, 1024, 1024);
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.timed_out);
    EXPECT_NE(res.exit_code, 0);
}
