#include <gtest/gtest.h>

#include "agent_loop.hpp"
#include "agent_tools.hpp"
#include "config.hpp"
#include "llm.hpp"
#include "net_smoke_artifacts.hpp"
#include "state.hpp"
#include "state_store.hpp"
#include "trace.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

bool net_smoke_enabled() {
    if (const char* env_run = std::getenv("NCA_RUN_NET_TESTS")) {
        return std::string(env_run) == "1";
    }
    return false;
}

AgentConfig make_live_config(const fs::path& workspace) {
    AgentConfig config;
    config.mode = "real";
    config.workspace_abs = workspace.string();
    config.workspace = workspace.string();
    config.detail_mode = true;
    config.max_turns = 6;
    config.max_context_bytes = 20000;
    config.max_tool_output_bytes = 4096;
    config.api_key = std::getenv("NCA_API_KEY");
    if (const char* url = std::getenv("NCA_BASE_URL")) config.base_url = url;
    else config.base_url = "https://api.openai.com/v1";
    if (const char* model = std::getenv("NCA_MODEL")) config.model = model;
    else config.model = "gpt-4o-mini";
    return config;
}

const TraceEvent* find_trace_event(const std::vector<TraceEvent>& trace, const std::string& kind) {
    for (const TraceEvent& event : trace) {
        if (event.kind == kind) {
            return &event;
        }
    }
    return nullptr;
}

std::size_t count_trace_events(const std::vector<TraceEvent>& trace, const std::string& kind) {
    std::size_t count = 0;
    for (const TraceEvent& event : trace) {
        if (event.kind == kind) {
            ++count;
        }
    }
    return count;
}

std::vector<TraceEvent> parse_jsonl_trace_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<TraceEvent> events;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        events.push_back(nlohmann::json::parse(line).get<TraceEvent>());
    }
    return events;
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

nlohmann::json trace_events_to_json(const std::vector<TraceEvent>& events) {
    nlohmann::json json_value = nlohmann::json::array();
    for (const TraceEvent& event : events) {
        json_value.push_back(nlohmann::json(event));
    }
    return json_value;
}

void assert_trace_usage_recorded(const std::vector<TraceEvent>& trace) {
    bool saw_usage = false;
    for (const TraceEvent& event : trace) {
        if (event.kind != "llm_response_received") {
            continue;
        }
        ASSERT_TRUE(event.payload.contains("prompt_tokens"));
        ASSERT_TRUE(event.payload.contains("completion_tokens"));
        ASSERT_TRUE(event.payload.contains("total_tokens"));
        ASSERT_TRUE(event.payload.at("prompt_tokens").is_number_integer());
        ASSERT_TRUE(event.payload.at("completion_tokens").is_number_integer());
        ASSERT_TRUE(event.payload.at("total_tokens").is_number_integer());
        EXPECT_GT(event.payload.at("total_tokens").get<int>(), 0);
        saw_usage = true;
    }
    EXPECT_TRUE(saw_usage);
}

std::string complex_task_prompt() {
    return "Refactor the planning runtime in multiple steps: inspect the current state model, "
           "then add tests, then wire persistence, and finally verify behavior.";
}

std::string current_test_full_name() {
    const ::testing::TestInfo* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    if (test_info == nullptr) {
        return "net_smoke";
    }
    return std::string(test_info->test_suite_name()) + "." + test_info->name();
}

}  // namespace

class NetPlanningSmokeTest : public ::testing::Test {
protected:
    fs::path workspace;
    fs::path session_path;
    fs::path trace_path;

    void SetUp() override {
        if (!net_smoke_enabled()) {
            GTEST_SKIP() << "NCA_RUN_NET_TESTS!=1, skipping real network smoke tests.";
        }

        const char* api_key = std::getenv("NCA_API_KEY");
        if (!api_key || std::string(api_key).empty()) {
            GTEST_SKIP() << "NCA_API_KEY is not set, skipping real network smoke tests.";
        }

        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        workspace = fs::temp_directory_path() / ("nano_net_planning_smoke_" + unique);
        session_path = workspace / "session.json";
        trace_path = workspace / "trace.jsonl";
        fs::create_directories(workspace);
    }

    void TearDown() override {
        std::string cleanup_err;
        fs::path artifact_dir;
        if (!cleanup_net_smoke_workspace(workspace,
                                         session_path,
                                         trace_path,
                                         current_test_full_name(),
                                         &cleanup_err,
                                         &artifact_dir)) {
            ADD_FAILURE() << cleanup_err;
            return;
        }
        if (!artifact_dir.empty()) {
            std::cout << "[net-smoke-artifacts] " << artifact_dir.string() << "\n";
        }
    }
};

TEST(NetSmokeTest, LiveStreamingCompletion) {
    if (!net_smoke_enabled()) {
        GTEST_SKIP() << "NCA_RUN_NET_TESTS not set, skipping real network test.";
    }
    
    const char* api_key = std::getenv("NCA_API_KEY");
    if (!api_key || std::string(api_key).empty()) {
        GTEST_SKIP() << "NCA_API_KEY is not set, skipping real network test.";
    }

    AgentConfig config;
    config.api_key = api_key;
    if (const char* url = std::getenv("NCA_BASE_URL")) config.base_url = url;
    else config.base_url = "https://api.openai.com/v1";
    
    if (const char* model = std::getenv("NCA_MODEL")) config.model = model;
    else config.model = "gpt-4o-mini";

    nlohmann::json msgs = nlohmann::json::array();
    msgs.push_back({
        {"role", "user"},
        {"content", "Say exactly 'hello' and nothing else."}
    });

    nlohmann::json no_tools = nlohmann::json::array();

    bool got_content = false;
    auto on_delta = [&](const std::string& txt) -> bool {
        if (!txt.empty()) {
            got_content = true;
        }
        return true;
    };

    EXPECT_NO_THROW({
        nlohmann::json response = llm_chat_completion_stream(config, msgs, no_tools, on_delta);
        EXPECT_TRUE(response.contains("role"));
        EXPECT_EQ(response["role"], "assistant");
        ASSERT_TRUE(response.contains("usage"));
        ASSERT_TRUE(response["usage"].is_object());
        EXPECT_GT(response["usage"].value("total_tokens", 0), 0);
        if (response.contains("content")) {
            std::string content = response["content"].get<std::string>();
            EXPECT_NE(content, "");
        } else {
            // It could be missing if it only gave a tool call, but we gave no tools here
            // So we expect content.
            EXPECT_TRUE(got_content);
        }
    }) << "The stream should finish without an exception.";
}

TEST_F(NetPlanningSmokeTest, ComplexTaskPlanningStepProgressionSmoke) {
    AgentConfig config = make_live_config(workspace);
    JsonFileStateStore store(session_path.string());
    JsonlTraceSink trace_sink(trace_path.string());
    std::string trace_err;
    ASSERT_TRUE(trace_sink.prepare(&trace_err)) << trace_err;

    SessionState session = make_session_state();
    prepare_session_state(session, {"net-planning-smoke"}, make_active_rules_snapshot(config));

    const ToolRegistry& registry = get_default_tool_registry();
    const nlohmann::json tools = nlohmann::json::array();
    const std::string system_prompt =
        "You are a planning runtime smoke test harness.\n"
        "Follow these rules exactly.\n"
        "1. On your first assistant turn, respond with ONLY a JSON object with a top-level key named plan.\n"
        "2. The ONLY valid first-turn JSON shape is exactly:\n"
        "   {\"plan\":{\"summary\":\"Refactor planning runtime in multiple steps\",\"steps\":[{\"id\":\"step-1\",\"title\":\"Inspect the current state model\",\"detail\":\"...\"},{\"id\":\"step-2\",\"title\":\"Add tests\",\"detail\":\"...\"},{\"id\":\"step-3\",\"title\":\"Wire persistence and verify behavior\",\"detail\":\"...\"}]}}\n"
        "3. plan.steps MUST be an array named steps.\n"
        "   INVALID example: {\"plan\":{\"summary\":\"...\",\"step-1\":{...},\"step-2\":{...},\"step-3\":{...}}}\n"
        "4. Do not place step-1, step-2, or step-3 directly under plan.\n"
        "5. Each step object must contain exactly id, title, detail.\n"
        "6. After you have emitted that first plan, respond on every later assistant turn with exactly the plain text: step executed\n"
        "7. Never emit markdown, never emit code fences, never emit explanations, and never emit tool calls.\n";

    LLMStreamFunc live_llm = [](const AgentConfig& cfg, const nlohmann::json& msgs, const nlohmann::json& tools_schema) {
        return llm_chat_completion_stream(cfg, msgs, tools_schema, [](const std::string&) { return true; });
    };

    ASSERT_NO_THROW(agent_run(config,
                              system_prompt,
                              complex_task_prompt(),
                              tools,
                              live_llm,
                              &session,
                              &registry,
                              &trace_sink));

    std::string save_err;
    ASSERT_TRUE(store.save(session, &save_err)) << save_err;
    const StateStoreLoadResult load_result = store.load();
    ASSERT_EQ(load_result.status, StateStoreLoadStatus::Loaded) << load_result.error;

    const std::vector<TraceEvent> jsonl_events = parse_jsonl_trace_file(trace_path);
    EXPECT_EQ(trace_events_to_json(jsonl_events), trace_events_to_json(load_result.session.trace));
    assert_trace_usage_recorded(jsonl_events);

    EXPECT_TRUE(load_result.session.needs_plan);
    EXPECT_EQ(load_result.session.plan_state, "PLAN_READY");
    EXPECT_EQ(load_result.session.plan_repair_attempts, 0);
    EXPECT_FALSE(load_result.session.plan_normalization_applied);
    EXPECT_TRUE(load_result.session.plan_validation_errors.empty());

    const nlohmann::json raw_contract =
        extract_plan_contract_from_raw_payload(load_result.session.plan_raw_response);
    ASSERT_TRUE(load_result.session.plan_raw_response.is_object());
    ASSERT_FALSE(load_result.session.plan_raw_response.empty());
    ASSERT_TRUE(raw_contract.contains("plan"));
    ASSERT_TRUE(raw_contract.at("plan").contains("steps"));
    ASSERT_TRUE(raw_contract.at("plan").at("steps").is_array());
    ASSERT_TRUE(load_result.session.plan_validated_artifact.contains("plan"));
    ASSERT_TRUE(load_result.session.plan_validated_artifact.at("plan").contains("steps"));
    ASSERT_TRUE(load_result.session.plan_validated_artifact.at("plan").at("steps").is_array());
    EXPECT_EQ(raw_contract.at("plan").at("steps"),
              load_result.session.plan_validated_artifact.at("plan").at("steps"));
    EXPECT_EQ(load_result.session.plan.outcome, "completed");
    ASSERT_EQ(load_result.session.plan.steps.size(), 3u);
    EXPECT_EQ(load_result.session.plan.steps[0].status, "completed");
    EXPECT_EQ(load_result.session.plan.steps[1].status, "completed");
    EXPECT_EQ(load_result.session.plan.steps[2].status, "completed");

    EXPECT_EQ(count_trace_events(jsonl_events, "planner_repair_requested"), 0u);
    EXPECT_EQ(count_trace_events(jsonl_events, "planner_repair_succeeded"), 0u);
    EXPECT_EQ(load_result.session.plan_repair_attempts,
              static_cast<int>(count_trace_events(jsonl_events, "planner_repair_requested")));

    const TraceEvent* planner_received = find_trace_event(jsonl_events, "planner_response_received");
    ASSERT_NE(planner_received, nullptr);
    ASSERT_TRUE(planner_received->payload.contains("planner_raw_response"));
    const nlohmann::json trace_raw_contract =
        extract_plan_contract_from_raw_payload(planner_received->payload.at("planner_raw_response"));
    ASSERT_TRUE(trace_raw_contract.contains("plan"));
    EXPECT_EQ(planner_received->payload.at("planner_raw_response"),
              load_result.session.plan_raw_response);
    EXPECT_EQ(trace_raw_contract.at("plan").at("steps"),
              raw_contract.at("plan").at("steps"));

    const TraceEvent* plan_ready = find_trace_event(jsonl_events, "plan_ready");
    ASSERT_NE(plan_ready, nullptr);
    ASSERT_TRUE(plan_ready->payload.contains("plan_validated_artifact"));
    EXPECT_FALSE(plan_ready->payload.value("normalization_applied", true));
    EXPECT_EQ(plan_ready->payload.value("plan_repair_attempts", -1), 0);
    EXPECT_FALSE(plan_ready->payload.value("needs_plan_decision", "").empty());
    EXPECT_FALSE(plan_ready->payload.value("planner_repair_effective_mode", "").empty());
    EXPECT_FALSE(plan_ready->payload.value("planner_repair_mode_reason", "").empty());
    EXPECT_EQ(plan_ready->payload.at("plan_validated_artifact").at("plan").at("steps"),
              load_result.session.plan_validated_artifact.at("plan").at("steps"));

    const TraceEvent* finished = find_trace_event(jsonl_events, "run_finished");
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->payload.value("outcome", ""), "completed");
    EXPECT_TRUE(finished->payload.at("total_tokens").is_number_integer());
    EXPECT_FALSE(finished->payload.value("needs_plan_decision", "").empty());
    EXPECT_FALSE(finished->payload.value("planner_repair_effective_mode", "").empty());
    EXPECT_FALSE(finished->payload.value("planner_repair_mode_reason", "").empty());
}

TEST_F(NetPlanningSmokeTest, ExecutionFailureFailedStepPersistenceSmoke) {
    AgentConfig config = make_live_config(workspace);
    JsonFileStateStore store(session_path.string());
    JsonlTraceSink trace_sink(trace_path.string());
    std::string trace_err;
    ASSERT_TRUE(trace_sink.prepare(&trace_err)) << trace_err;

    SessionState session = make_session_state();
    prepare_session_state(session, {"net-planning-smoke"}, make_active_rules_snapshot(config));

    const ToolRegistry& registry = get_default_tool_registry();
    const nlohmann::json tools = registry.to_openai_schema(config);
    const std::string system_prompt =
        "You are a planning runtime smoke test harness.\n"
        "Follow these rules exactly.\n"
        "1. On your first assistant turn, respond with ONLY a JSON object with a top-level key named plan.\n"
        "2. That plan must contain summary and exactly one step with id step-1.\n"
        "3. On the next assistant turn, call the tool read_file_safe with exactly this JSON argument object: {\"path\":\"missing-smoke-file.txt\"}.\n"
        "4. Do not invent any other tools and do not repair the failure.\n";

    LLMStreamFunc live_llm = [](const AgentConfig& cfg, const nlohmann::json& msgs, const nlohmann::json& tools_schema) {
        return llm_chat_completion_stream(cfg, msgs, tools_schema, [](const std::string&) { return true; });
    };

    ASSERT_NO_THROW(agent_run(config,
                              system_prompt,
                              complex_task_prompt(),
                              tools,
                              live_llm,
                              &session,
                              &registry,
                              &trace_sink));

    std::string save_err;
    ASSERT_TRUE(store.save(session, &save_err)) << save_err;
    const StateStoreLoadResult load_result = store.load();
    ASSERT_EQ(load_result.status, StateStoreLoadStatus::Loaded) << load_result.error;

    const std::vector<TraceEvent> jsonl_events = parse_jsonl_trace_file(trace_path);
    EXPECT_EQ(trace_events_to_json(jsonl_events), trace_events_to_json(load_result.session.trace));
    assert_trace_usage_recorded(jsonl_events);

    ASSERT_EQ(load_result.session.plan.steps.size(), 1u);
    EXPECT_EQ(load_result.session.plan_state, "PLAN_READY");
    EXPECT_TRUE(load_result.session.plan_validation_errors.empty());
    ASSERT_TRUE(load_result.session.plan_validated_artifact.contains("plan"));
    EXPECT_EQ(load_result.session.plan.steps[0].status, "failed");
    EXPECT_EQ(load_result.session.plan.outcome, "failed");
    EXPECT_EQ(load_result.session.plan.current_step_index, 0);

    const TraceEvent* finished = find_trace_event(jsonl_events, "run_finished");
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->payload.value("outcome", ""), "fatal_tool_failure");
}

TEST_F(NetPlanningSmokeTest, PlannerReentryDuringExecutionFailsClosedSmoke) {
    AgentConfig config = make_live_config(workspace);
    JsonFileStateStore store(session_path.string());
    JsonlTraceSink trace_sink(trace_path.string());
    std::string trace_err;
    ASSERT_TRUE(trace_sink.prepare(&trace_err)) << trace_err;

    SessionState session = make_session_state();
    prepare_session_state(session, {"net-planning-smoke"}, make_active_rules_snapshot(config));

    const ToolRegistry& registry = get_default_tool_registry();
    const nlohmann::json tools = nlohmann::json::array();
    const std::string system_prompt =
        "You are a planning runtime smoke test harness.\n"
        "Follow these rules exactly.\n"
        "1. On your first assistant turn, respond with ONLY a JSON object with a top-level key named plan.\n"
        "2. Your first response must exactly follow this JSON shape:\n"
        "   {\"plan\":{\"summary\":\"initial real api smoke plan\",\"steps\":[{\"id\":\"step-1\",\"title\":\"...\",\"detail\":\"...\"}]}}\n"
        "3. Do not add step-2, step-3, step-4, or any additional step objects.\n"
        "4. Do not use the field name description. Use title and detail exactly.\n"
        "5. On your second assistant turn, intentionally attempt replanning by responding with ONLY this JSON shape:\n"
        "   {\"plan\":{\"summary\":\"unexpected replanning attempt\",\"steps\":[{\"id\":\"step-1\",\"title\":\"Replan Step\",\"detail\":\"...\"}]}}\n"
        "6. Never emit markdown, never emit code fences, never emit explanations, and never emit tool calls.\n";

    LLMStreamFunc live_llm = [](const AgentConfig& cfg, const nlohmann::json& msgs, const nlohmann::json& tools_schema) {
        return llm_chat_completion_stream(cfg, msgs, tools_schema, [](const std::string&) { return true; });
    };

    ASSERT_NO_THROW(agent_run(config,
                              system_prompt,
                              "Create a single-step execution plan for a smoke test and do not break it into multiple steps.",
                              tools,
                              live_llm,
                              &session,
                              &registry,
                              &trace_sink));

    std::string save_err;
    ASSERT_TRUE(store.save(session, &save_err)) << save_err;
    const StateStoreLoadResult load_result = store.load();
    ASSERT_EQ(load_result.status, StateStoreLoadStatus::Loaded) << load_result.error;

    const std::vector<TraceEvent> jsonl_events = parse_jsonl_trace_file(trace_path);
    EXPECT_EQ(trace_events_to_json(jsonl_events), trace_events_to_json(load_result.session.trace));
    assert_trace_usage_recorded(jsonl_events);

    ASSERT_FALSE(load_result.session.plan.steps.empty());
    EXPECT_EQ(load_result.session.plan_state, "PLAN_READY");
    EXPECT_EQ(load_result.session.plan_repair_attempts, 0);
    EXPECT_TRUE(load_result.session.plan_validation_errors.empty());
    ASSERT_TRUE(load_result.session.plan_validated_artifact.contains("plan"));
    const nlohmann::json raw_contract =
        extract_plan_contract_from_raw_payload(load_result.session.plan_raw_response);
    ASSERT_TRUE(raw_contract.contains("plan"));
    EXPECT_EQ(load_result.session.plan.summary, "initial real api smoke plan");
    EXPECT_EQ(load_result.session.plan.steps[0].id, "step-1");
    EXPECT_EQ(raw_contract.at("plan").value("summary", ""), "initial real api smoke plan");
    EXPECT_EQ(load_result.session.plan_validated_artifact.at("plan").value("summary", ""),
              "initial real api smoke plan");
    EXPECT_EQ(load_result.session.plan.steps[0].status, "failed");
    EXPECT_EQ(load_result.session.plan.outcome, "planner_reentry_blocked");
    EXPECT_EQ(count_trace_events(jsonl_events, "planner_response_received"), 1u);

    const TraceEvent* finished = find_trace_event(jsonl_events, "run_finished");
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->payload.value("outcome", ""), "planner_reentry_blocked");
}
