#include <gtest/gtest.h>
#include "agent_loop.hpp"
#include "agent_utils.hpp"
#include "config.hpp"
#include "logger.hpp"
#include <filesystem>
#include <chrono>

class AgentLoopLimitsTest : public ::testing::Test {
protected:
    std::string test_workspace;
    void SetUp() override {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        test_workspace = (std::filesystem::temp_directory_path() / ("nano_limits_" + std::to_string(now))).string();
        std::filesystem::create_directories(test_workspace);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_workspace);
    }
};

TEST_F(AgentLoopLimitsTest, MaxTurnsHalt) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 3;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 50;
    config.max_context_bytes = 100000;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // To avoid fail-fast killing the loop, we must return a real tool with valid arguments
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {
                    {"id", "call_" + std::to_string(call_count)},
                    {"type", "function"},
                    {"function", {
                        {"name", "bash_execute_safe"},
                        {"arguments", "{\"command\":\"echo ok\",\"timeout_ms\":1000}"}
                    }}
                }
            }}
        };
    };
    
    agent_run(config, "System", "Prompt", nlohmann::json::array(), mock_llm);
    
    // Should successfully cycle and hit max_turns limit instead of failing fast on turn 1
    EXPECT_EQ(call_count, 3);
}

TEST_F(AgentLoopLimitsTest, MaxToolCallsPerTurn) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 2; // Limit 2
    config.max_total_tool_calls = 50;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // Return 3 tools, which exceeds the per-turn limit
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {{"id", "1"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}},
                {{"id", "2"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 2\"}"}}}},
                {{"id", "3"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 3\"}"}}}}
            }}
        };
    };
    
    agent_run(config, "", "", nlohmann::json::array(), mock_llm);
    
    // Fails fast at Turn 1 constraint checking, before executing
    EXPECT_EQ(call_count, 1);
}

TEST_F(AgentLoopLimitsTest, MaxTotalToolCalls) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 7;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // 4 valid tools per turn
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {{"id", "1"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}},
                {{"id", "2"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}},
                {{"id", "3"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}},
                {{"id", "4"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}}
            }}
        };
    };
    
    agent_run(config, "", "", nlohmann::json::array(), mock_llm);
    
    // Turn 1: +4 tools -> total 4 (Passes check, executes perfectly). Loop continues.
    // Turn 2: +4 tools -> total 8 (> 7). Fails check globally, aborts turn 2 before executing.
    EXPECT_EQ(call_count, 2);
}

TEST_F(AgentLoopLimitsTest, ToolOutputTruncation) {
    std::string huge(20000, 'A');
    std::string out = truncate_tool_output(huge, 100);
    
    std::string expected_marker = "\n<TRUNCATED: original=20000 bytes, kept=100 bytes>";
    
    // Validate exact hard limit sizes combined with marker padding
    EXPECT_EQ(out.size(), 100 + expected_marker.size());
    EXPECT_NE(out.find("<TRUNCATED: original="), std::string::npos);
}

TEST_F(AgentLoopLimitsTest, ContextSlidingWindow) {
    nlohmann::json messages = {
        {{"role", "system"}, {"content", "sys"}},
        {{"role", "user"}, {"content", "req"}},
        {{"role", "tool"}, {"content", std::string(1000, 'B')}}, // 1000 bytes block
        {{"role", "tool"}, {"content", std::string(1000, 'C')}}  // 1000 bytes block
    };
    
    // Apply highly constrained limit (~500 bytes)
    enforce_context_limits(messages, 500);
    
    // Should shrink physical size
    EXPECT_LE(messages.dump().size(), 500);
    
    // Must drop both previous tool histories replacing completely:
    EXPECT_EQ(messages[2]["content"], "<DROPPED old tool outputs to fit context>");
    EXPECT_EQ(messages[3]["content"], "<DROPPED old tool outputs to fit context>");
}

TEST_F(AgentLoopLimitsTest, FailFastOnToolError) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 10;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // Attempt to read a non-existent file, which will fail the tool and should break the loop instantly
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {{"id", "fail_1"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"does_not_exist_xyz.txt\"}"}}}}
            }}
        };
    };
    
    agent_run(config, "", "", nlohmann::json::array(), mock_llm);
    
    // Expect the agent to stop immediately after turn 1 due to State Contamination / fail-fast
    EXPECT_EQ(call_count, 1);
}

TEST_F(AgentLoopLimitsTest, CombinedTruncationAndContextLimit) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 2; // Need a second turn to test context clipping
    config.max_tool_output_bytes = 50; // Severely truncate individual tools
    config.max_context_bytes = 600;    // Tightly clamp total context
    
    int call_count = 0;
    size_t msgs_size_turn_2 = 0;
    
    LLMStreamFunc mock_llm = [&](const AgentConfig& cfg, const nlohmann::json& msgs, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        if (call_count == 1) {
            // First turn: generate lots of tools with heavy output
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {{"id", "t1"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"for i in $(seq 1 100); do echo A; done\"}"}}}},
                    {{"id", "t2"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"for i in $(seq 1 100); do echo B; done\"}"}}}},
                    {{"id", "t3"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"for i in $(seq 1 100); do echo C; done\"}"}}}}
                }}
            };
        } else {
            // Second turn: analyze sliding window results
            msgs_size_turn_2 = msgs.dump().size();
            return nlohmann::json{{"role", "assistant"}, {"content", "Stop!"}};
        }
    };
    
    agent_run(config, "Sys", "Req", nlohmann::json::array(), mock_llm);
    
    // Expect agent went to turn 2
    EXPECT_EQ(call_count, 2);
    
    // Validate output shrinkage actually cascaded nicely below global max limitation
    EXPECT_LE(msgs_size_turn_2, config.max_context_bytes);
    EXPECT_GT(msgs_size_turn_2, 0);
}
