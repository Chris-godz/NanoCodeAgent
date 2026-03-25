#include <gtest/gtest.h>

#include "agent_loop.hpp"

#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

class StateIntegrationTest : public ::testing::Test {
protected:
    fs::path workspace;

    void SetUp() override {
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        workspace = fs::temp_directory_path() / ("nano_state_integration_" + unique);
        fs::create_directories(workspace);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(workspace, ec);
    }
};

TEST_F(StateIntegrationTest, AgentRunUpdatesSessionState) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.allow_mutating_tools = true;
    config.max_turns = 5;
    config.max_tool_calls_per_turn = 4;
    config.max_total_tool_calls = 10;
    config.max_tool_output_bytes = 2048;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"runtime-skill"}, make_active_rules_snapshot(config));

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "call_write"},
                    {"function", {
                        {"name", "write_file_safe"},
                        {"arguments", "{\"path\":\"state.txt\",\"content\":\"hello\"}"}
                    }}
                }}}
            };
        }
        if (turn == 2) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "call_read"},
                    {"function", {
                        {"name", "read_file_safe"},
                        {"arguments", "{\"path\":\"state.txt\"}"}
                    }}
                }}}
            };
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    EXPECT_EQ(turn, 3);
    EXPECT_EQ(session.turn_index, 3);
    EXPECT_EQ(session.counters.llm_turns, 3);
    EXPECT_EQ(session.counters.tool_calls_requested, 2);
    EXPECT_EQ(session.counters.observations, 2);
    EXPECT_EQ(session.scratchpad, "done");
    ASSERT_EQ(session.active_skills.size(), 1u);
    EXPECT_EQ(session.active_skills[0], "runtime-skill");
    ASSERT_EQ(session.tool_calls.size(), 2u);
    EXPECT_EQ(session.tool_calls[0].tool_name, "write_file_safe");
    EXPECT_EQ(session.tool_calls[0].status, "ok");
    EXPECT_EQ(session.tool_calls[1].tool_name, "read_file_safe");
    EXPECT_EQ(session.tool_calls[1].status, "ok");
    ASSERT_EQ(session.observations.size(), 2u);
    EXPECT_NE(session.observations[1].content.find("hello"), std::string::npos);
    ASSERT_TRUE(session.messages.is_array());
    EXPECT_GE(session.messages.size(), 7u);
    EXPECT_TRUE(fs::exists(workspace / "state.txt"));
}

TEST_F(StateIntegrationTest, PreloadedMessagesAreReused) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 3;
    config.max_tool_calls_per_turn = 4;
    config.max_total_tool_calls = 10;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    session.messages = nlohmann::json::array({
        nlohmann::json{{"role", "system"}, {"content", "persisted system"}},
        nlohmann::json{{"role", "user"}, {"content", "persisted user"}}
    });
    session.turn_index = 1;
    session.counters.llm_turns = 1;
    prepare_session_state(session, {}, make_active_rules_snapshot(config));

    nlohmann::json seen_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        seen_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "resumed"}};
    };

    agent_run(config, "new system", "new user", nlohmann::json::array(), mock_llm, &session);

    ASSERT_EQ(seen_messages.size(), 2u);
    EXPECT_EQ(seen_messages[0]["content"], "persisted system");
    EXPECT_EQ(seen_messages[1]["content"], "persisted user");
    EXPECT_EQ(session.turn_index, 2);
    EXPECT_EQ(session.scratchpad, "resumed");
}
