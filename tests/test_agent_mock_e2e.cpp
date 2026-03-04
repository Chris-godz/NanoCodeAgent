#include <gtest/gtest.h>
#include "agent_loop.hpp"
#include "config.hpp"
#include "workspace.hpp"
#include <filesystem>
#include <chrono>

class AgentMockE2ETest : public ::testing::Test {
protected:
    std::string test_workspace;
    void SetUp() override {
        // Safe randomly stamped workspace generation dodging CI collisions
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        test_workspace = (std::filesystem::temp_directory_path() / ("nano_e2e_" + std::to_string(now))).string();
        std::filesystem::create_directories(test_workspace);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_workspace);
    }
};

TEST_F(AgentMockE2ETest, MockFullChain) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 50;
    config.max_tool_output_bytes = 1024;
    config.max_context_bytes = 10000;
    
    int turn = 0;
    
    LLMStreamFunc mock_llm = [&](const AgentConfig& cfg, const nlohmann::json& msgs, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) { // 1. Write file
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "call_write"},
                        {"function", {
                            {"name", "write_file_safe"},
                            {"arguments", "{\"path\":\"hello.txt\",\"content\":\"world\"}"}
                        }}
                    }
                }}
            };
        } else if (turn == 2) { // 2. Bash execute safe
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "call_bash"},
                        {"function", {
                            {"name", "bash_execute_safe"},
                            {"arguments", "{\"command\":\"cat hello.txt\",\"timeout_ms\":2000}"}
                        }}
                    }
                }}
            };
        } else if (turn == 3) { // 3. Read file safe
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "call_read"},
                        {"function", {
                            {"name", "read_file_safe"},
                            {"arguments", "{\"path\":\"hello.txt\"}"}
                        }}
                    }
                }}
            };
        } else { // 4. Final message
            return nlohmann::json{
                {"role", "assistant"},
                {"content", "Task completed"}
            };
        }
    };
    
    agent_run(config, "sys", "DO IT", nlohmann::json::array(), mock_llm);
    
    // Validate final effect
    EXPECT_EQ(turn, 4); // Executed 4 turns
    
    // Assert file actually created via the tools execution safely nested in temporary directory pattern 
    EXPECT_TRUE(std::filesystem::exists(test_workspace + "/hello.txt"));
}
