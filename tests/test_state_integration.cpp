#include <gtest/gtest.h>

#include "agent_loop.hpp"
#include "state_store.hpp"
#include "trace.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

class FailAfterCommittedTraceSink : public TraceSink {
public:
    explicit FailAfterCommittedTraceSink(int successful_writes_before_failure)
        : successful_writes_before_failure_(successful_writes_before_failure) {}

    TraceWriteResult write(const TraceEvent& event) override {
        if (successful_writes_before_failure_-- > 0) {
            committed_events.push_back(event);
            return {};
        }
        attempted_events.push_back(event);
        return {
            false,
            "injected trace sink failure",
        };
    }

    std::vector<TraceEvent> committed_events;
    std::vector<TraceEvent> attempted_events;

private:
    int successful_writes_before_failure_ = 0;
};

nlohmann::json trace_events_to_json(const std::vector<TraceEvent>& events) {
    nlohmann::json json_value = nlohmann::json::array();
    for (const TraceEvent& event : events) {
        json_value.push_back(nlohmann::json(event));
    }
    return json_value;
}

}  // namespace

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
    EXPECT_EQ(session.plan.generation, 1);
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

TEST_F(StateIntegrationTest, PrepareSessionStateResetsStalePlanAndBumpsGeneration) {
    AgentConfig config;
    config.workspace_abs = workspace.string();

    SessionState session = make_session_state();
    session.plan.generation = 4;
    session.plan.summary = "stale";
    session.plan.steps.push_back(PlanStep{
        .id = "step-1",
        .title = "old step",
        .status = "in_progress",
        .detail = "remove me",
        .metadata = nlohmann::json{{"phase", "old"}}
    });
    session.plan.metadata = nlohmann::json{{"owner", "planner"}};

    prepare_session_state(session, {"runtime-skill"}, make_active_rules_snapshot(config));

    EXPECT_EQ(session.plan.generation, 5);
    EXPECT_TRUE(session.plan.summary.empty());
    EXPECT_TRUE(session.plan.steps.empty());
    EXPECT_TRUE(session.plan.metadata.is_object());
    EXPECT_TRUE(session.plan.metadata.empty());
}

TEST_F(StateIntegrationTest, TraceFailureStillSavesConsistentSession) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"runtime-skill"}, make_active_rules_snapshot(config));

    FailAfterCommittedTraceSink durable_sink(1);
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    EXPECT_THROW(agent_run(config,
                           "system",
                           "user",
                           nlohmann::json::array(),
                           mock_llm,
                           &session,
                           nullptr,
                           &durable_sink),
                 std::runtime_error);

    const fs::path session_path = workspace / "session.json";
    JsonFileStateStore store(session_path.string());
    std::string save_err;
    ASSERT_TRUE(store.save(session, &save_err)) << save_err;

    const StateStoreLoadResult load_result = store.load();
    ASSERT_EQ(load_result.status, StateStoreLoadStatus::Loaded) << load_result.error;
    EXPECT_EQ(trace_events_to_json(load_result.session.trace),
              trace_events_to_json(durable_sink.committed_events));
    EXPECT_TRUE(std::none_of(load_result.session.trace.begin(),
                             load_result.session.trace.end(),
                             [](const TraceEvent& event) { return event.kind == "run_finished"; }));
}
