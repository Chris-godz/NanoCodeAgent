#include <gtest/gtest.h>

#include "agent_loop.hpp"
#include "state.hpp"
#include "state_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

nlohmann::json make_plan_response(
    const std::string& summary,
    const std::vector<std::pair<std::string, std::string>>& steps) {
    nlohmann::json plan_steps = nlohmann::json::array();
    for (const auto& [id, title] : steps) {
        plan_steps.push_back({
            {"id", id},
            {"title", title},
            {"detail", "baseline planning runtime contract step"}
        });
    }

    return nlohmann::json{
        {"role", "assistant"},
        {"content", "structured plan ready"},
        {"plan", {
            {"summary", summary},
            {"steps", plan_steps},
            {"metadata", {
                {"source", "planning-runtime-test"},
                {"style", "baseline"}
            }}
        }}
    };
}

nlohmann::json make_plan_steps_object_response(
    const std::string& summary,
    const std::vector<std::pair<std::string, nlohmann::json>>& steps) {
    nlohmann::json plan_steps = nlohmann::json::object();
    for (const auto& [id, step_json] : steps) {
        plan_steps[id] = step_json;
    }

    return nlohmann::json{
        {"role", "assistant"},
        {"content", "structured plan ready"},
        {"plan", {
            {"summary", summary},
            {"steps", plan_steps},
            {"metadata", {
                {"source", "planning-runtime-test"},
                {"style", "baseline"}
            }}
        }}
    };
}

nlohmann::json make_invalid_plan_steps_shape_response(
    const std::string& summary,
    const std::vector<std::pair<std::string, nlohmann::json>>& steps) {
    nlohmann::json plan_steps = nlohmann::json::object();
    for (const auto& [id, step_json] : steps) {
        plan_steps[id] = step_json;
    }

    return nlohmann::json{
        {"role", "assistant"},
        {"content", "planner contract needs repair"},
        {"plan", {
            {"summary", summary},
            {"steps", plan_steps},
            {"metadata", {
                {"source", "planning-runtime-test"},
                {"style", "repair-invalid"}
            }}
        }}
    };
}

nlohmann::json make_embedded_plan_response(const nlohmann::json& response) {
    return nlohmann::json{
        {"role", "assistant"},
        {"content", response.dump()}
    };
}

nlohmann::json make_tool_call_response() {
    return nlohmann::json{
        {"role", "assistant"},
        {"content", ""},
        {"tool_calls", nlohmann::json::array({
            nlohmann::json{
                {"id", "call_1"},
                {"type", "function"},
                {"function", {
                    {"name", "read_file_safe"},
                    {"arguments", nlohmann::json{{"path", "should-not-run.txt"}}.dump()}
                }}
            }
        })}
    };
}

std::string complex_task_prompt() {
    return "Refactor the planning runtime in multiple steps: inspect the current state model, "
           "then add tests, then wire persistence, and finally verify behavior.";
}

nlohmann::json session_json(const SessionState& session) {
    return session_state_to_json(session);
}

nlohmann::json extract_plan_contract_from_raw_payload(const nlohmann::json& raw_payload) {
    if (!raw_payload.is_object()) {
        return nlohmann::json::object();
    }
    if (raw_payload.contains("plan") && raw_payload.at("plan").is_object()) {
        return nlohmann::json{{"plan", raw_payload.at("plan")}};
    }
    if (raw_payload.contains("content") && raw_payload.at("content").is_string()) {
        try {
            const nlohmann::json parsed = nlohmann::json::parse(raw_payload.at("content").get<std::string>());
            if (parsed.is_object() && parsed.contains("plan") && parsed.at("plan").is_object()) {
                return parsed;
            }
        } catch (const nlohmann::json::exception&) {
        }
    }
    return nlohmann::json::object();
}

std::size_t count_messages_containing(const nlohmann::json& messages, const std::string& needle) {
    if (!messages.is_array()) {
        return 0;
    }

    std::size_t count = 0;
    for (const auto& message : messages) {
        if (!message.is_object() ||
            !message.contains("content") ||
            !message.at("content").is_string()) {
            continue;
        }
        if (message.at("content").get_ref<const std::string&>().find(needle) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

const TraceEvent* find_trace_event(const std::vector<TraceEvent>& trace, const std::string& kind) {
    for (const TraceEvent& event : trace) {
        if (event.kind == kind) {
            return &event;
        }
    }
    return nullptr;
}

std::vector<std::size_t> find_trace_event_indices(const std::vector<TraceEvent>& trace,
                                                  const std::string& kind) {
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < trace.size(); ++index) {
        if (trace[index].kind == kind) {
            indices.push_back(index);
        }
    }
    return indices;
}

nlohmann::json load_planner_fixture(const std::string& fixture_name) {
    const fs::path fixture_path = fs::path(__FILE__).parent_path() / "fixtures" / fixture_name;
    std::ifstream input(fixture_path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Could not open planner fixture: " + fixture_path.string());
    }

    return nlohmann::json::parse(input);
}

}  // namespace

class PlanningRuntimeTest : public ::testing::Test {
protected:
    fs::path workspace;

    void SetUp() override {
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        workspace = fs::temp_directory_path() / ("nano_planning_runtime_" + unique);
        fs::create_directories(workspace);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(workspace, ec);
    }
};

class PlannerContractTest : public PlanningRuntimeTest {};

TEST_F(PlanningRuntimeTest, ComplexTaskProducesStructuredPlan) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_plan_response("Break the runtime work into explicit execution steps",
                                      {
                                          {"step-1", "inspect runtime state"},
                                          {"step-2", "write planning tests"},
                                          {"step-3", "persist active plan state"},
                                      });
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    ASSERT_TRUE(persisted.value("needs_plan", false));
    ASSERT_TRUE(persisted.contains("plan"));
    ASSERT_TRUE(persisted.at("plan").contains("current_step_index"));
    EXPECT_EQ(persisted.at("plan").value("summary", ""),
              "Break the runtime work into explicit execution steps");
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "completed");
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    ASSERT_TRUE(persisted.at("plan_raw_response").is_object());
    ASSERT_FALSE(persisted.at("plan_raw_response").empty());
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    EXPECT_TRUE(persisted.at("plan_validation_errors").empty());
    EXPECT_FALSE(persisted.value("plan_normalization_applied", true));
    ASSERT_EQ(persisted.at("plan").at("steps").size(), 3u);
    EXPECT_EQ(persisted.at("plan").at("steps")[0].value("status", ""), "completed");
    EXPECT_EQ(persisted.at("plan").at("steps")[1].value("status", ""), "completed");
    EXPECT_EQ(persisted.at("plan").at("steps")[2].value("status", ""), "completed");
}

TEST_F(PlanningRuntimeTest, EmbeddedPlanJsonContentProducesStructuredPlan) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"content", make_plan_response("Embedded planner content works",
                                               {
                                                   {"step-1", "inspect runtime state"},
                                                   {"step-2", "advance structured execution"},
                                               }).dump()}
            };
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    ASSERT_TRUE(persisted.value("needs_plan", false));
    EXPECT_EQ(persisted.at("plan").value("summary", ""), "Embedded planner content works");
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "completed");
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    EXPECT_TRUE(persisted.at("plan_validation_errors").empty());
    ASSERT_EQ(persisted.at("plan").at("steps").size(), 2u);
    EXPECT_EQ(persisted.at("plan").at("steps")[0].value("status", ""), "completed");
    EXPECT_EQ(persisted.at("plan").at("steps")[1].value("status", ""), "completed");
}

TEST_F(PlanningRuntimeTest, StepObjectPlanRequiresRepairBeforeExecution) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_plan_steps_object_response(
                "Keyed object steps must trigger repair",
                {
                    {"step-2", nlohmann::json{
                        {"id", "step-2"},
                        {"title", "execute the second repaired step"},
                        {"detail", "keyed object should be rejected pre-repair"}
                    }},
                    {"step-1", nlohmann::json{
                        {"id", "step-1"},
                        {"title", "execute the first repaired step"},
                        {"detail", "keyed object should be rejected pre-repair"}
                    }},
                });
        }
        if (llm_calls == 2) {
            EXPECT_FALSE(messages.empty());
            if (messages.empty()) {
                return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
            }
            EXPECT_TRUE(messages.back().is_object());
            if (!messages.back().is_object()) {
                return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
            }
            EXPECT_EQ(messages.back().value("role", ""), "system");
            EXPECT_NE(messages.back().value("content", "").find("Planner contract repair required."),
                      std::string::npos);
            return make_plan_response("Repaired array steps after keyed object rejection",
                                      {
                                          {"step-1", "execute the first repaired step"},
                                          {"step-2", "execute the second repaired step"},
                                      });
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 4);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(persisted.value("plan_repair_attempts", -1), 1);
    EXPECT_FALSE(persisted.value("plan_normalization_applied", true));
    EXPECT_TRUE(persisted.at("plan_validation_errors").empty());
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    ASSERT_TRUE(persisted.at("plan_validated_artifact").at("plan").contains("steps"));
    ASSERT_EQ(persisted.at("plan_validated_artifact").at("plan").at("steps").size(), 2u);
    EXPECT_EQ(persisted.at("plan_validated_artifact").at("plan").at("steps")[0].value("title", ""),
              "execute the first repaired step");
    EXPECT_EQ(persisted.at("plan_validated_artifact").at("plan").at("steps")[1].value("title", ""),
              "execute the second repaired step");
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "completed");
    EXPECT_EQ(find_trace_event_indices(session.trace, "planner_validation_failed").size(), 1u);
    EXPECT_EQ(find_trace_event_indices(session.trace, "planner_repair_requested").size(), 1u);
    EXPECT_EQ(find_trace_event_indices(session.trace, "planner_repair_succeeded").size(), 1u);
}

TEST_F(PlanningRuntimeTest, InvalidPlannerResponseGetsOneRepairRetry) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 6;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_plan_steps_object_response(
                "Repair this keyed step map",
                {
                    {"step-1", "inspect runtime state"},
                    {"step-2", "write focused contract tests"},
                });
        }
        if (llm_calls == 2) {
            EXPECT_FALSE(messages.empty());
            if (!messages.empty()) {
                EXPECT_TRUE(messages.back().is_object());
                if (messages.back().is_object()) {
                    EXPECT_EQ(messages.back().value("role", ""), "system");
                    EXPECT_NE(messages.back().value("content", "").find("Planner contract repair required."),
                              std::string::npos);
                }
            }
            return make_plan_response("Repair succeeded after one retry",
                                      {
                                          {"step-1", "inspect runtime state"},
                                          {"step-2", "write focused contract tests"},
                                      });
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 4);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_TRUE(persisted.at("plan_validation_errors").empty());
    EXPECT_FALSE(persisted.value("plan_normalization_applied", true));
    ASSERT_TRUE(persisted.at("plan_raw_response").is_object());
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    EXPECT_EQ(persisted.at("plan").value("summary", ""), "Repair succeeded after one retry");

    bool saw_repair_prompt = false;
    for (const auto& message : persisted.at("messages")) {
        if (!message.is_object() || message.value("role", "") != "system") {
            continue;
        }
        if (message.value("content", "").find("Planner contract repair required.") != std::string::npos) {
            saw_repair_prompt = true;
            break;
        }
    }
    EXPECT_TRUE(saw_repair_prompt);
}

TEST_F(PlanningRuntimeTest, SecondInvalidPlannerAttemptFailsClosed) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 5;
    config.max_context_bytes = 20000;
    config.detail_mode = true;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        return make_plan_steps_object_response(
            "Still invalid keyed string map",
            {
                {"step-1", "still not a step object"},
            });
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 2);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_INVALID");
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "failed");
    EXPECT_TRUE(persisted.at("plan").at("steps").empty());
    EXPECT_TRUE(persisted.at("plan_validated_artifact").empty());
    EXPECT_FALSE(persisted.at("plan_validation_errors").empty());
    const TraceEvent* finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->payload.value("outcome", ""), "planner_response_invalid");
}

TEST_F(PlanningRuntimeTest, PlanningPhaseDoesNotExecuteToolCallsBeforePlanReady) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 6;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_tool_call_response();
        }
        if (llm_calls == 2) {
            return make_plan_response("Repair after blocked planning tool call",
                                      {
                                          {"step-1", "continue after repair"},
                                      });
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 3);
    EXPECT_TRUE(persisted.at("tool_calls").empty());
    EXPECT_TRUE(persisted.at("observations").empty());
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "completed");
}

TEST_F(PlanningRuntimeTest, PlanExecutionAdvancesStepStatuses) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    std::vector<nlohmann::json> snapshots;
    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        snapshots.push_back(session_json(session));
        ++llm_calls;
        if (llm_calls == 1) {
            return make_plan_response("Advance through the plan one step at a time",
                                      {
                                          {"step-1", "inspect runtime state"},
                                          {"step-2", "execute next baseline step"},
                                      });
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    ASSERT_GE(snapshots.size(), 3u);
    ASSERT_TRUE(snapshots[1].contains("plan"));
    ASSERT_TRUE(snapshots[1].at("plan").contains("current_step_index"));
    EXPECT_EQ(snapshots[1].value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(snapshots[1].at("plan").value("current_step_index", -1), 0);
    EXPECT_EQ(snapshots[1].at("plan").at("steps")[0].value("status", ""), "in_progress");
    EXPECT_EQ(snapshots[1].at("plan").at("steps")[1].value("status", ""), "pending");

    EXPECT_EQ(snapshots[2].value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(snapshots[2].at("plan").value("current_step_index", -1), 1);
    EXPECT_EQ(snapshots[2].at("plan").at("steps")[0].value("status", ""), "completed");
    EXPECT_EQ(snapshots[2].at("plan").at("steps")[1].value("status", ""), "in_progress");
}

TEST_F(PlanningRuntimeTest, ExecutionPhaseCannotReenterPlanner) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_plan_response("Initial execution plan",
                                      {
                                          {"step-1", "inspect runtime state"},
                                      });
        }
        return make_plan_response("Unexpected replanning attempt",
                                  {
                                      {"step-1", "overwrite the active plan"},
                                  });
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 2);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(persisted.at("plan").value("summary", ""), "Initial execution plan");
    EXPECT_EQ(persisted.at("plan").value("current_step_index", -1), 0);
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "planner_reentry_blocked");
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    EXPECT_TRUE(persisted.at("plan_validation_errors").empty());
    ASSERT_EQ(persisted.at("plan").at("steps").size(), 1u);
    EXPECT_EQ(persisted.at("plan").at("steps")[0].value("status", ""), "failed");
}

TEST_F(PlanningRuntimeTest, EmbeddedPlanJsonContentReentryFailsClosed) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"content", make_plan_response("Initial embedded execution plan",
                                               {
                                                   {"step-1", "inspect runtime state"},
                                               }).dump()}
            };
        }
        return nlohmann::json{
            {"role", "assistant"},
            {"content", make_plan_response("Unexpected embedded replanning attempt",
                                           {
                                               {"step-1", "overwrite the active plan"},
                                           }).dump()}
        };
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 2);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(persisted.at("plan").value("summary", ""), "Initial embedded execution plan");
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "planner_reentry_blocked");
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    EXPECT_TRUE(persisted.at("plan_validation_errors").empty());
    ASSERT_EQ(persisted.at("plan").at("steps").size(), 1u);
    EXPECT_EQ(persisted.at("plan").at("steps")[0].value("status", ""), "failed");
}

TEST_F(PlanningRuntimeTest, FailedStepDoesNotAppearCompleted) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_plan_response("One failing step is enough for the baseline",
                                      {
                                          {"step-1", "execute the failing step"},
                                      });
        }
        throw std::runtime_error("execution blew up");
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 2);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    ASSERT_EQ(persisted.at("plan").at("steps").size(), 1u);
    EXPECT_EQ(persisted.at("plan").at("steps")[0].value("status", ""), "failed");
    EXPECT_NE(persisted.at("plan").at("steps")[0].value("status", ""), "completed");
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "failed");
    EXPECT_EQ(persisted.at("plan").value("current_step_index", -1), 0);
}

TEST_F(PlanningRuntimeTest, PlanStatePersistsAcrossSaveLoad) {
    JsonFileStateStore store((workspace / "planning-session.json").string());

    SessionState session = make_session_state();
    nlohmann::json persisted = session_json(session);
    persisted["needs_plan"] = true;
    persisted["plan"]["generation"] = 7;
    persisted["plan"]["summary"] = "Resume an active multi-step runtime plan";
    persisted["plan"]["steps"] = nlohmann::json::array({
        nlohmann::json{
            {"id", "step-1"},
            {"title", "inspect runtime state"},
            {"status", "completed"},
            {"detail", "done"}
        },
        nlohmann::json{
            {"id", "step-2"},
            {"title", "persist current step"},
            {"status", "in_progress"},
            {"detail", "working"}
        },
        nlohmann::json{
            {"id", "step-3"},
            {"title", "verify save/load"},
            {"status", "pending"},
            {"detail", "next"}
        }
    });
    persisted["plan"]["current_step_index"] = 1;
    persisted["plan"]["outcome"] = "in_progress";
    persisted["plan_state"] = "PLAN_READY";
    persisted["plan_raw_response"] = nlohmann::json::object();
    persisted["plan_validated_artifact"] = nlohmann::json{
        {"plan", {
            {"summary", persisted["plan"]["summary"]},
            {"steps", persisted["plan"]["steps"]},
            {"metadata", nlohmann::json::object()}
        }}
    };
    persisted["plan_validation_errors"] = nlohmann::json::array();
    persisted["plan_normalization_applied"] = false;

    SessionState parsed;
    std::string parse_err;
    ASSERT_TRUE(session_state_from_json(persisted, &parsed, &parse_err)) << parse_err;

    std::string save_err;
    ASSERT_TRUE(store.save(parsed, &save_err)) << save_err;

    const StateStoreLoadResult load_result = store.load();
    ASSERT_EQ(load_result.status, StateStoreLoadStatus::Loaded) << load_result.error;

    const nlohmann::json reloaded = session_json(load_result.session);
    ASSERT_TRUE(reloaded.value("needs_plan", false));
    EXPECT_EQ(reloaded.at("plan").value("generation", -1), 7);
    EXPECT_EQ(reloaded.at("plan").value("summary", ""), "Resume an active multi-step runtime plan");
    EXPECT_EQ(reloaded.at("plan").value("current_step_index", -1), 1);
    EXPECT_EQ(reloaded.at("plan").value("outcome", ""), "in_progress");
    EXPECT_EQ(reloaded.value("plan_state", ""), "PLAN_READY");
    ASSERT_TRUE(reloaded.at("plan_validated_artifact").contains("plan"));
    EXPECT_TRUE(reloaded.at("plan_validation_errors").empty());
    EXPECT_FALSE(reloaded.value("plan_normalization_applied", true));
    ASSERT_EQ(reloaded.at("plan").at("steps").size(), 3u);
    EXPECT_EQ(reloaded.at("plan").at("steps")[0].value("status", ""), "completed");
    EXPECT_EQ(reloaded.at("plan").at("steps")[1].value("status", ""), "in_progress");
    EXPECT_EQ(reloaded.at("plan").at("steps")[2].value("status", ""), "pending");
}

TEST_F(PlannerContractTest, ObjectStepsTriggerRepairInsteadOfSilentNormalization) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_plan_steps_object_response(
                "Keyed object steps should require repair",
                {
                    {"step-2", nlohmann::json{
                        {"id", "step-2"},
                        {"title", "execute the second repaired step"},
                        {"detail", "keyed object should be rejected pre-repair"}
                    }},
                    {"step-1", nlohmann::json{
                        {"id", "step-1"},
                        {"title", "execute the first repaired step"},
                        {"detail", "keyed object should be rejected pre-repair"}
                    }},
                });
        }
        if (llm_calls == 2) {
            EXPECT_FALSE(messages.empty());
            if (messages.empty()) {
                return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
            }
            EXPECT_TRUE(messages.back().is_object());
            if (!messages.back().is_object()) {
                return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
            }
            EXPECT_EQ(messages.back().value("role", ""), "system");
            EXPECT_NE(messages.back().value("content", "").find("Planner contract repair required."),
                      std::string::npos);
            return make_plan_response("Repair succeeded for keyed steps",
                                      {
                                          {"step-1", "execute the first repaired step"},
                                          {"step-2", "execute the second repaired step"},
                                      });
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    ASSERT_EQ(llm_calls, 4);
    EXPECT_EQ(persisted.value("plan_repair_attempts", -1), 1);
    EXPECT_FALSE(persisted.value("plan_normalization_applied", true));
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    ASSERT_EQ(persisted.at("plan_validated_artifact").at("plan").at("steps").size(), 2u);
    EXPECT_EQ(persisted.at("plan_validated_artifact").at("plan").at("steps")[0].value("title", ""),
              "execute the first repaired step");
    EXPECT_EQ(persisted.at("plan_validated_artifact").at("plan").at("steps")[1].value("title", ""),
              "execute the second repaired step");

    const TraceEvent* received = find_trace_event(session.trace, "planner_response_received");
    ASSERT_NE(received, nullptr);
    ASSERT_TRUE(received->payload.contains("planner_raw_response"));
    EXPECT_EQ(received->payload.value("plan_repair_attempts", -1), 0);
    EXPECT_EQ(received->payload.value("plan_state", ""), "AWAITING_PLAN");

    const TraceEvent* ready = find_trace_event(session.trace, "plan_ready");
    ASSERT_NE(ready, nullptr);
    ASSERT_TRUE(ready->payload.contains("plan_validated_artifact"));
    EXPECT_FALSE(ready->payload.value("normalization_applied", true));
    EXPECT_EQ(ready->payload.value("plan_repair_attempts", -1), 1);
    EXPECT_EQ(ready->payload.value("plan_state", ""), "PLAN_READY");

    EXPECT_NE(find_trace_event(session.trace, "planner_validation_failed"), nullptr);
    EXPECT_NE(find_trace_event(session.trace, "planner_repair_requested"), nullptr);
    EXPECT_NE(find_trace_event(session.trace, "planner_repair_succeeded"), nullptr);
}

TEST_F(PlannerContractTest, InvalidStepsShapeTriggersSingleRepairRetry) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 6;
    config.max_context_bytes = 20000;

    const nlohmann::json fixture = load_planner_fixture("planner_contract_repair_then_execute.json");

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    std::vector<nlohmann::json> snapshots;
    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        snapshots.push_back(session_json(session));
        return fixture.at(llm_calls++);
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    ASSERT_EQ(llm_calls, 4);
    EXPECT_EQ(persisted.value("plan_repair_attempts", -1), 1);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_FALSE(persisted.value("plan_normalization_applied", true));
    EXPECT_TRUE(persisted.at("plan_validation_errors").empty());
    EXPECT_EQ(count_messages_containing(persisted.at("messages"),
                                        "Planner contract repair required."),
              1u);

    ASSERT_GE(snapshots.size(), 4u);
    EXPECT_EQ(snapshots[0].value("plan_state", ""), "AWAITING_PLAN");
    EXPECT_TRUE(snapshots[0].at("plan").at("steps").empty());
    EXPECT_TRUE(snapshots[0].at("tool_calls").empty());
    EXPECT_TRUE(snapshots[0].at("observations").empty());
    EXPECT_EQ(snapshots[1].value("plan_state", ""), "AWAITING_REPAIR");
    EXPECT_TRUE(snapshots[1].at("plan").at("steps").empty());
    EXPECT_TRUE(snapshots[1].at("tool_calls").empty());
    EXPECT_TRUE(snapshots[1].at("observations").empty());
    EXPECT_EQ(snapshots[2].value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(snapshots[2].at("plan").value("current_step_index", -1), 0);
    EXPECT_EQ(snapshots[2].at("plan").at("steps")[0].value("status", ""), "in_progress");

    const std::vector<std::size_t> received = find_trace_event_indices(session.trace, "planner_response_received");
    const std::vector<std::size_t> failed = find_trace_event_indices(session.trace, "planner_validation_failed");
    const std::vector<std::size_t> requested = find_trace_event_indices(session.trace, "planner_repair_requested");
    const std::vector<std::size_t> succeeded = find_trace_event_indices(session.trace, "planner_repair_succeeded");
    const std::vector<std::size_t> ready = find_trace_event_indices(session.trace, "plan_ready");

    ASSERT_EQ(received.size(), 2u);
    ASSERT_EQ(failed.size(), 1u);
    ASSERT_EQ(requested.size(), 1u);
    ASSERT_EQ(succeeded.size(), 1u);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_LT(received[0], failed[0]);
    EXPECT_LT(failed[0], requested[0]);
    EXPECT_LT(requested[0], received[1]);
    EXPECT_LT(received[1], succeeded[0]);
    EXPECT_LT(succeeded[0], ready[0]);

    const TraceEvent& validation_failed = session.trace[failed[0]];
    ASSERT_TRUE(validation_failed.payload.contains("validation_errors"));
    EXPECT_FALSE(validation_failed.payload.at("validation_errors").empty());
    EXPECT_FALSE(validation_failed.payload.value("normalization_applied", true));
    EXPECT_EQ(validation_failed.payload.value("plan_state", ""), "AWAITING_REPAIR");
    EXPECT_EQ(validation_failed.payload.value("plan_repair_attempts", -1), 1);
    EXPECT_FALSE(validation_failed.payload.value("needs_plan_decision", "").empty());
    EXPECT_FALSE(validation_failed.payload.value("planner_repair_effective_mode", "").empty());
    EXPECT_FALSE(validation_failed.payload.value("planner_repair_mode_reason", "").empty());

    const TraceEvent& repair_requested = session.trace[requested[0]];
    ASSERT_TRUE(repair_requested.payload.contains("validation_errors"));
    EXPECT_EQ(repair_requested.payload.value("plan_state", ""), "AWAITING_REPAIR");
    EXPECT_EQ(repair_requested.payload.value("plan_repair_attempts", -1), 1);
    EXPECT_FALSE(repair_requested.payload.value("planner_repair_effective_mode", "").empty());
    EXPECT_FALSE(repair_requested.payload.value("planner_repair_mode_reason", "").empty());

    const TraceEvent& repair_succeeded = session.trace[succeeded[0]];
    ASSERT_TRUE(repair_succeeded.payload.contains("plan_validated_artifact"));
    EXPECT_EQ(repair_succeeded.payload.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(repair_succeeded.payload.value("plan_repair_attempts", -1), 1);
    EXPECT_FALSE(repair_succeeded.payload.value("needs_plan_decision", "").empty());
    EXPECT_FALSE(repair_succeeded.payload.value("planner_repair_effective_mode", "").empty());
    EXPECT_FALSE(repair_succeeded.payload.value("planner_repair_mode_reason", "").empty());

    const TraceEvent& plan_ready = session.trace[ready[0]];
    ASSERT_TRUE(plan_ready.payload.contains("plan_validated_artifact"));
    EXPECT_EQ(plan_ready.payload.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(plan_ready.payload.value("plan_repair_attempts", -1), 1);
    EXPECT_FALSE(plan_ready.payload.value("needs_plan_decision", "").empty());
    EXPECT_FALSE(plan_ready.payload.value("planner_repair_effective_mode", "").empty());
    EXPECT_FALSE(plan_ready.payload.value("planner_repair_mode_reason", "").empty());
}

TEST_F(PlannerContractTest, MissingStepFieldsRequireRepairWithoutAutofill) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"plan", {
                    {"summary", "Missing required title should trigger repair"},
                    {"steps", nlohmann::json::array({
                        nlohmann::json{
                            {"id", "step-1"},
                            {"detail", "title is intentionally missing"}
                        }
                    })},
                    {"metadata", nlohmann::json::object()}
                }}
            };
        }
        if (llm_calls == 2) {
            EXPECT_FALSE(messages.empty());
            if (messages.empty()) {
                return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
            }
            EXPECT_TRUE(messages.back().is_object());
            if (!messages.back().is_object()) {
                return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
            }
            EXPECT_EQ(messages.back().value("role", ""), "system");
            EXPECT_NE(messages.back().value("content", "").find("Planner contract repair required."),
                      std::string::npos);
            return make_plan_response("Repair restored required fields",
                                      {
                                          {"step-1", "repair provided explicit title"},
                                      });
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 3);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(persisted.value("plan_repair_attempts", -1), 1);
    EXPECT_FALSE(persisted.value("plan_normalization_applied", true));
    EXPECT_TRUE(persisted.at("plan_validation_errors").empty());
    ASSERT_EQ(persisted.at("plan").at("steps").size(), 1u);
    EXPECT_EQ(persisted.at("plan").at("steps")[0].value("title", ""),
              "repair provided explicit title");

    const TraceEvent* validation_failed = find_trace_event(session.trace, "planner_validation_failed");
    ASSERT_NE(validation_failed, nullptr);
    ASSERT_TRUE(validation_failed->payload.contains("validation_errors"));
    ASSERT_TRUE(validation_failed->payload.at("validation_errors").is_array());
    ASSERT_FALSE(validation_failed->payload.at("validation_errors").empty());
    EXPECT_NE(validation_failed->payload.at("validation_errors")[0].get<std::string>().find(".title"),
              std::string::npos);
}

TEST_F(PlannerContractTest, PlanDoesNotEnterExecutionBeforePlanReady) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 6;
    config.max_context_bytes = 20000;

    const nlohmann::json fixture = load_planner_fixture("planner_contract_repair_then_execute.json");

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    std::vector<nlohmann::json> snapshots;
    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        snapshots.push_back(session_json(session));
        return fixture.at(llm_calls++);
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    ASSERT_GE(snapshots.size(), 4u);
    EXPECT_EQ(snapshots[0].value("plan_state", ""), "AWAITING_PLAN");
    EXPECT_TRUE(snapshots[0].at("plan").at("steps").empty());
    EXPECT_EQ(snapshots[1].value("plan_state", ""), "AWAITING_REPAIR");
    EXPECT_TRUE(snapshots[1].at("plan").at("steps").empty());
    EXPECT_EQ(snapshots[1].at("plan").value("current_step_index", -1), -1);
    EXPECT_TRUE(snapshots[1].at("tool_calls").empty());
    EXPECT_TRUE(snapshots[1].at("observations").empty());
    EXPECT_EQ(snapshots[2].value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(snapshots[2].at("plan").value("current_step_index", -1), 0);
    EXPECT_EQ(snapshots[2].at("plan").value("outcome", ""), "in_progress");
    EXPECT_EQ(snapshots[2].at("plan").at("steps")[0].value("status", ""), "in_progress");
    EXPECT_EQ(snapshots[3].value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(snapshots[3].at("plan").at("steps")[0].value("status", ""), "completed");
}

TEST_F(PlannerContractTest, RawAndValidatedPlanBothPersisted) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 6;
    config.max_context_bytes = 20000;

    JsonFileStateStore store((workspace / "planner-contract-evidence.json").string());

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_invalid_plan_steps_shape_response(
                "Force a repair before accepting the contract",
                {
                    {"alpha", nlohmann::json{
                        {"id", "step-a"},
                        {"title", "inspect runtime state"},
                        {"detail", "invalid key shape"}
                    }},
                    {"beta", nlohmann::json{
                        {"id", "step-b"},
                        {"title", "write focused contract tests"},
                        {"detail", "invalid key shape"}
                    }},
                });
        }
        if (llm_calls == 2) {
            return make_embedded_plan_response(
                make_plan_response("Repair succeeded with embedded array payload",
                                   {
                                       {"step-1", "inspect runtime state"},
                                       {"step-2", "write focused contract tests"},
                                   }));
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(persisted.value("plan_repair_attempts", -1), 1);
    ASSERT_TRUE(persisted.at("plan_raw_response").contains("content"));
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    EXPECT_NE(persisted.at("plan_raw_response").dump(), persisted.at("plan_validated_artifact").dump());

    std::string save_err;
    ASSERT_TRUE(store.save(session, &save_err)) << save_err;

    const StateStoreLoadResult load_result = store.load();
    ASSERT_EQ(load_result.status, StateStoreLoadStatus::Loaded) << load_result.error;

    const nlohmann::json reloaded = session_json(load_result.session);
    EXPECT_EQ(reloaded.value("plan_repair_attempts", -1), 1);
    EXPECT_EQ(reloaded.at("plan_raw_response"), persisted.at("plan_raw_response"));
    EXPECT_EQ(reloaded.at("plan_validated_artifact"), persisted.at("plan_validated_artifact"));

    const TraceEvent* repair_succeeded = find_trace_event(load_result.session.trace, "planner_repair_succeeded");
    ASSERT_NE(repair_succeeded, nullptr);
    ASSERT_TRUE(repair_succeeded->payload.contains("planner_raw_response"));
    ASSERT_TRUE(repair_succeeded->payload.contains("plan_validated_artifact"));
    EXPECT_EQ(repair_succeeded->payload.value("plan_repair_attempts", -1), 1);

    const TraceEvent* ready = find_trace_event(load_result.session.trace, "plan_ready");
    ASSERT_NE(ready, nullptr);
    ASSERT_TRUE(ready->payload.contains("plan_validated_artifact"));
    EXPECT_EQ(ready->payload.value("plan_state", ""), "PLAN_READY");
}

TEST_F(PlannerContractTest, CanonicalPassPersistsRawPlannerPayload) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_embedded_plan_response(
                make_plan_response("Canonical embedded payload plan",
                                   {
                                       {"step-1", "inspect runtime state"},
                                       {"step-2", "write focused assertions"},
                                   }));
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "step executed"}};
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 3);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(persisted.value("plan_repair_attempts", -1), 0);
    EXPECT_FALSE(persisted.value("plan_normalization_applied", true));
    ASSERT_TRUE(persisted.at("plan_raw_response").contains("content"));
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));

    const nlohmann::json raw_contract =
        extract_plan_contract_from_raw_payload(persisted.at("plan_raw_response"));
    ASSERT_TRUE(raw_contract.contains("plan"));
    ASSERT_TRUE(raw_contract.at("plan").at("steps").is_array());
    EXPECT_EQ(raw_contract.at("plan").at("steps"),
              persisted.at("plan_validated_artifact").at("plan").at("steps"));

    const TraceEvent* received = find_trace_event(session.trace, "planner_response_received");
    ASSERT_NE(received, nullptr);
    ASSERT_TRUE(received->payload.contains("planner_raw_response"));
    EXPECT_EQ(received->payload.at("planner_raw_response"), persisted.at("plan_raw_response"));
    EXPECT_EQ(find_trace_event_indices(session.trace, "planner_repair_requested").size(), 0u);
    EXPECT_EQ(find_trace_event_indices(session.trace, "planner_repair_succeeded").size(), 0u);
}

TEST_F(PlannerContractTest, RepairFailureEndsAsPlannerResponseInvalid) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        return make_invalid_plan_steps_shape_response(
            "Still invalid keyed map",
            {
                {"alpha", nlohmann::json{
                    {"id", "step-a"},
                    {"title", "still invalid"},
                    {"detail", "repair should fail closed"}
                }},
            });
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 2);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_INVALID");
    EXPECT_EQ(persisted.value("plan_repair_attempts", -1), 1);
    EXPECT_TRUE(persisted.at("plan_validated_artifact").empty());
    EXPECT_FALSE(persisted.at("plan_validation_errors").empty());

    const std::vector<std::size_t> failed = find_trace_event_indices(session.trace, "planner_validation_failed");
    EXPECT_EQ(failed.size(), 2u);
    EXPECT_EQ(find_trace_event(session.trace, "planner_repair_succeeded"), nullptr);
    EXPECT_EQ(find_trace_event(session.trace, "plan_ready"), nullptr);

    const TraceEvent* finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->payload.value("outcome", ""), "planner_response_invalid");
    EXPECT_EQ(finished->payload.value("final_plan_state", ""), "PLAN_INVALID");
}

TEST_F(PlannerContractTest, ExecutionPhaseStillBlocksPlannerReentry) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_plan_response("Initial execution plan",
                                      {
                                          {"step-1", "inspect runtime state"},
                                      });
        }
        return make_plan_response("Unexpected replanning attempt",
                                  {
                                      {"step-1", "overwrite the active plan"},
                                  });
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 2);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(persisted.value("plan_repair_attempts", -1), 0);
    EXPECT_EQ(persisted.at("plan").value("summary", ""), "Initial execution plan");
    EXPECT_EQ(persisted.at("plan_raw_response").at("plan").value("summary", ""), "Initial execution plan");
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    EXPECT_EQ(persisted.at("plan_validated_artifact").at("plan").value("summary", ""),
              "Initial execution plan");

    EXPECT_EQ(find_trace_event_indices(session.trace, "planner_response_received").size(), 1u);

    const TraceEvent* finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->payload.value("outcome", ""), "planner_reentry_blocked");
    EXPECT_EQ(finished->payload.value("final_plan_state", ""), "PLAN_READY");
}

TEST_F(PlannerContractTest, ReentryDoesNotMutateActiveValidatedPlan) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 5;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    prepare_session_state(session, {"planning-runtime"}, make_active_rules_snapshot(config));

    int llm_calls = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++llm_calls;
        if (llm_calls == 1) {
            return make_embedded_plan_response(
                make_plan_response("Initial embedded execution plan",
                                   {
                                       {"step-1", "inspect runtime state"},
                                   }));
        }
        return make_embedded_plan_response(
            make_plan_response("Unexpected replanning attempt",
                               {
                                   {"step-1", "overwrite the active plan"},
                               }));
    };

    agent_run(config,
              "system",
              complex_task_prompt(),
              nlohmann::json::array(),
              mock_llm,
              &session);

    const nlohmann::json persisted = session_json(session);
    EXPECT_EQ(llm_calls, 2);
    EXPECT_EQ(persisted.value("plan_state", ""), "PLAN_READY");
    EXPECT_EQ(persisted.at("plan").value("summary", ""), "Initial embedded execution plan");
    EXPECT_EQ(persisted.at("plan").value("outcome", ""), "planner_reentry_blocked");

    const nlohmann::json raw_contract =
        extract_plan_contract_from_raw_payload(persisted.at("plan_raw_response"));
    ASSERT_TRUE(raw_contract.contains("plan"));
    EXPECT_EQ(raw_contract.at("plan").value("summary", ""), "Initial embedded execution plan");
    ASSERT_TRUE(persisted.at("plan_validated_artifact").contains("plan"));
    EXPECT_EQ(persisted.at("plan_validated_artifact").at("plan").value("summary", ""),
              "Initial embedded execution plan");

    EXPECT_EQ(find_trace_event_indices(session.trace, "planner_response_received").size(), 1u);
    EXPECT_EQ(find_trace_event_indices(session.trace, "planner_repair_requested").size(), 0u);

    const TraceEvent* finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->payload.value("outcome", ""), "planner_reentry_blocked");
    EXPECT_EQ(finished->payload.value("final_plan_state", ""), "PLAN_READY");
}
