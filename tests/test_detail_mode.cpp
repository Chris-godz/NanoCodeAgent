#include <gtest/gtest.h>

#include "agent_loop.hpp"
#include "trace.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const TraceEvent* find_trace_event(const std::vector<TraceEvent>& trace,
                                   const std::string& kind,
                                   const char* payload_key = nullptr,
                                   const std::string& payload_value = "") {
    for (const TraceEvent& event : trace) {
        if (event.kind != kind) {
            continue;
        }
        if (payload_key == nullptr) {
            return &event;
        }
        if (event.payload.contains(payload_key) && event.payload.at(payload_key).is_string() &&
            event.payload.at(payload_key).get<std::string>() == payload_value) {
            return &event;
        }
    }
    return nullptr;
}

std::string read_file_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
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

nlohmann::json trace_events_to_json(const std::vector<TraceEvent>& events) {
    nlohmann::json json_value = nlohmann::json::array();
    for (const TraceEvent& event : events) {
        json_value.push_back(nlohmann::json(event));
    }
    return json_value;
}

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

}  // namespace

class DetailModeTest : public ::testing::Test {
protected:
    fs::path workspace;
    fs::path trace_path;

    void SetUp() override {
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        workspace = fs::temp_directory_path() / ("nano_detail_mode_" + unique);
        trace_path = workspace / "trace" / "agent.jsonl";
        fs::create_directories(workspace);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(workspace, ec);
    }
};

TEST_F(DetailModeTest, DetailModeDisabledDoesNotPopulateSessionTrace) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.max_turns = 3;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    EXPECT_TRUE(session.trace.empty());
    EXPECT_EQ(session.scratchpad, "done");
    EXPECT_EQ(session.turn_index, 1);
}

TEST_F(DetailModeTest, DetailModeEnabledGeneratesLifecycleEvents) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    ASSERT_FALSE(session.trace.empty());
    EXPECT_NE(find_trace_event(session.trace, "run_started"), nullptr);
    EXPECT_NE(find_trace_event(session.trace, "turn_started"), nullptr);
    EXPECT_NE(find_trace_event(session.trace, "llm_response_received"), nullptr);
    EXPECT_NE(find_trace_event(session.trace, "run_finished"), nullptr);
    ASSERT_NE(find_trace_event(session.trace, "run_started"), nullptr);
    EXPECT_EQ(find_trace_event(session.trace, "run_started")->payload.value("session_id", ""), session.session_id);
    EXPECT_FALSE(find_trace_event(session.trace, "run_started")->created_at.empty());
}

TEST_F(DetailModeTest, BlockedToolTraceIncludesFailureClassification) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;

    SessionState session = make_session_state();
    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "blocked_write"},
                    {"function", {
                        {"name", "write_file_safe"},
                        {"arguments", "{\"path\":\"blocked.txt\",\"content\":\"x\"}"}
                    }}
                }}}
            };
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    const TraceEvent* finished = find_trace_event(session.trace, "tool_call_finished", "tool_name", "write_file_safe");
    ASSERT_NE(finished, nullptr);
    EXPECT_EQ(finished->payload.value("tool_status", ""), "blocked");
    ASSERT_TRUE(finished->payload.contains("missing_approvals"));
    EXPECT_EQ(finished->payload.at("missing_approvals"), nlohmann::json::array({"mutating"}));

    const TraceEvent* classified = find_trace_event(session.trace, "tool_failure_classified", "tool_name", "write_file_safe");
    ASSERT_NE(classified, nullptr);
    EXPECT_EQ(classified->payload.value("failure_class", ""), "blocked");
}

TEST_F(DetailModeTest, SkippedToolTraceIncludesSkippedFinishEvent) {
    const fs::path target = workspace / "retry.txt";
    std::ofstream output(target, std::ios::binary);
    output << "hello world";
    output.close();

    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    SessionState session = make_session_state();
    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "recoverable_first"},
                        {"function", {
                            {"name", "apply_patch"},
                            {"arguments", "{\"path\":\"retry.txt\",\"old_text\":\"missing\",\"new_text\":\"patched\"}"}
                        }}
                    },
                    {
                        {"id", "skipped_second"},
                        {"function", {
                            {"name", "write_file_safe"},
                            {"arguments", "{\"path\":\"should_not_exist.txt\",\"content\":\"x\"}"}
                        }}
                    }
                }}
            };
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    const TraceEvent* skipped = find_trace_event(session.trace, "tool_call_finished", "tool_name", "write_file_safe");
    ASSERT_NE(skipped, nullptr);
    EXPECT_EQ(skipped->payload.value("tool_status", ""), "skipped");
    EXPECT_EQ(skipped->payload.value("reason", ""), "not executed due to prior tool failure");
    EXPECT_EQ(find_trace_event(session.trace, "tool_call_started", "tool_name", "write_file_safe"), nullptr);
    ASSERT_EQ(session.tool_calls.size(), 2u);
    EXPECT_EQ(session.tool_calls[1].status, "skipped");
    EXPECT_TRUE(session.tool_calls[1].started_at.empty());
    EXPECT_FALSE(session.tool_calls[1].finished_at.empty());
    EXPECT_FALSE(fs::exists(workspace / "should_not_exist.txt"));
}

TEST_F(DetailModeTest, FatalToolTraceIncludesFailureClassificationAndRunOutcome) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.allow_execution_tools = true;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    SessionState session = make_session_state();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {{
                {"id", "bash_fail"},
                {"function", {
                    {"name", "bash_execute_safe"},
                    {"arguments", "{\"command\":\"exit 7\",\"timeout_ms\":1000}"}
                }}
            }}}
        };
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    const TraceEvent* classified =
        find_trace_event(session.trace, "tool_failure_classified", "tool_name", "bash_execute_safe");
    ASSERT_NE(classified, nullptr);
    EXPECT_EQ(classified->payload.value("failure_class", ""), "fatal");

    const TraceEvent* run_finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(run_finished, nullptr);
    EXPECT_EQ(run_finished->payload.value("outcome", ""), "fatal_tool_failure");
}

TEST_F(DetailModeTest, RetryableToolTraceIncludesFailureClassification) {
    const fs::path build_script = workspace / "build.sh";
    std::ofstream build_output(build_script, std::ios::binary);
    build_output << "#!/usr/bin/env bash\n";
    build_output << "sleep 1\n";
    build_output.close();
    fs::permissions(build_script,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);

    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.allow_execution_tools = true;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    SessionState session = make_session_state();
    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                {"id", "build_timeout"},
                {"function", {
                    {"name", "build_project_safe"},
                    {"arguments", "{\"timeout_ms\":50}"}
                }}
            }}}
        };
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    const TraceEvent* classified =
        find_trace_event(session.trace, "tool_failure_classified", "tool_name", "build_project_safe");
    ASSERT_NE(classified, nullptr);
    EXPECT_EQ(classified->payload.value("failure_class", ""), "retryable");

    const TraceEvent* run_finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(run_finished, nullptr);
    EXPECT_EQ(run_finished->payload.value("outcome", ""), "completed");
}

TEST_F(DetailModeTest, LlmExceptionProducesFailedRunTrace) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;

    SessionState session = make_session_state();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        throw std::runtime_error("synthetic llm failure");
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    const TraceEvent* run_finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(run_finished, nullptr);
    EXPECT_EQ(run_finished->payload.value("outcome", ""), "llm_request_failed");
}

TEST_F(DetailModeTest, MaxTurnsProducesAbortOutcomeTrace) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 0;

    SessionState session = make_session_state();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{{"role", "assistant"}, {"content", "should not run"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    const TraceEvent* run_finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(run_finished, nullptr);
    EXPECT_EQ(run_finished->payload.value("outcome", ""), "max_turns_exceeded");
}

TEST_F(DetailModeTest, MaxToolCallsPerTurnProducesAbortOutcomeTrace) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;
    config.max_tool_calls_per_turn = 1;

    SessionState session = make_session_state();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {
                    {"id", "read_one"},
                    {"function", {
                        {"name", "read_file_safe"},
                        {"arguments", "{\"path\":\"a.txt\"}"}
                    }}
                },
                {
                    {"id", "read_two"},
                    {"function", {
                        {"name", "read_file_safe"},
                        {"arguments", "{\"path\":\"b.txt\"}"}
                    }}
                }
            }}
        };
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    const TraceEvent* run_finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(run_finished, nullptr);
    EXPECT_EQ(run_finished->payload.value("outcome", ""), "max_tool_calls_per_turn_exceeded");
}

TEST_F(DetailModeTest, MaxTotalToolCallsProducesAbortOutcomeTrace) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;
    config.max_total_tool_calls = 1;
    config.max_tool_calls_per_turn = 4;

    SessionState session = make_session_state();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {
                    {"id", "read_one"},
                    {"function", {
                        {"name", "read_file_safe"},
                        {"arguments", "{\"path\":\"a.txt\"}"}
                    }}
                },
                {
                    {"id", "read_two"},
                    {"function", {
                        {"name", "read_file_safe"},
                        {"arguments", "{\"path\":\"b.txt\"}"}
                    }}
                }
            }}
        };
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session);

    const TraceEvent* run_finished = find_trace_event(session.trace, "run_finished");
    ASSERT_NE(run_finished, nullptr);
    EXPECT_EQ(run_finished->payload.value("outcome", ""), "max_total_tool_calls_exceeded");
}

TEST_F(DetailModeTest, RuntimeTraceWriteFailureFailsClosed) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    FailAfterCommittedTraceSink sink(1);
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    testing::internal::CaptureStderr();
    EXPECT_THROW(agent_run(config,
                           "system",
                           "user",
                           nlohmann::json::array(),
                           mock_llm,
                           &session,
                           nullptr,
                           &sink),
                 std::runtime_error);
    const std::string stderr_text = testing::internal::GetCapturedStderr();

    EXPECT_NE(stderr_text.find("trace sink write failed"), std::string::npos);
    EXPECT_NE(session.scratchpad.find("trace sink write failed"), std::string::npos);
    EXPECT_EQ(find_trace_event(session.trace, "run_finished"), nullptr);
    EXPECT_EQ(sink.committed_events.size(), 1u);
    EXPECT_EQ(sink.attempted_events.size(), 1u);
}

TEST_F(DetailModeTest, JsonlTraceSinkWritesValidJsonLines) {
    JsonlTraceSink sink(trace_path.string());
    std::string err;
    ASSERT_TRUE(sink.prepare(&err)) << err;

    TraceEvent event = make_trace_event(
        "run_started",
        "agent run started",
        nlohmann::json{{"session_id", "session-1"}, {"mode", "mock"}, {"model", "gpt-4o"}});
    ASSERT_TRUE(sink.write(event).ok);

    const std::string file_text = read_file_text(trace_path);
    ASSERT_FALSE(file_text.empty());

    const nlohmann::json parsed = nlohmann::json::parse(file_text);
    EXPECT_EQ(parsed.value("kind", ""), "run_started");
    EXPECT_EQ(parsed.at("payload").value("session_id", ""), "session-1");
}

TEST_F(DetailModeTest, JsonlIsAuthorityWhenEnabled) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    JsonlTraceSink sink(trace_path.string());
    std::string err;
    ASSERT_TRUE(sink.prepare(&err)) << err;

    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session, nullptr, &sink);

    const std::vector<TraceEvent> jsonl_events = parse_jsonl_trace_file(trace_path);
    ASSERT_FALSE(jsonl_events.empty());
    EXPECT_EQ(trace_events_to_json(jsonl_events), trace_events_to_json(session.trace));
}

TEST_F(DetailModeTest, JsonlMultipleEventsRemainValidJsonl) {
    const fs::path target = workspace / "trace_multi.txt";
    std::ofstream output(target, std::ios::binary);
    output << "hello";
    output.close();

    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.allow_mutating_tools = true;
    config.max_turns = 3;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    JsonlTraceSink sink(trace_path.string());
    std::string err;
    ASSERT_TRUE(sink.prepare(&err)) << err;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "read_call"},
                    {"function", {
                        {"name", "read_file_safe"},
                        {"arguments", "{\"path\":\"trace_multi.txt\"}"}
                    }}
                }}}
            };
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "system", "user", nlohmann::json::array(), mock_llm, &session, nullptr, &sink);

    const std::string file_text = read_file_text(trace_path);
    ASSERT_FALSE(file_text.empty());

    std::istringstream input(file_text);
    std::vector<TraceEvent> jsonl_events;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        jsonl_events.push_back(nlohmann::json::parse(line).get<TraceEvent>());
    }

    ASSERT_GE(jsonl_events.size(), 6u);
    EXPECT_EQ(jsonl_events.front().kind, "run_started");
    EXPECT_EQ(jsonl_events[1].kind, "turn_started");
    EXPECT_EQ(jsonl_events[2].kind, "llm_response_received");
    EXPECT_EQ(jsonl_events[3].kind, "tool_call_started");
    EXPECT_EQ(jsonl_events[4].kind, "tool_call_finished");
    EXPECT_EQ(jsonl_events.back().kind, "run_finished");
    EXPECT_EQ(trace_events_to_json(jsonl_events), trace_events_to_json(session.trace));
}

TEST_F(DetailModeTest, TraceJsonlFailureDoesNotLeaveSessionAheadOfJsonl) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
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

    ASSERT_EQ(durable_sink.committed_events.size(), 1u);
    EXPECT_EQ(trace_events_to_json(session.trace), trace_events_to_json(durable_sink.committed_events));
    EXPECT_EQ(durable_sink.attempted_events.size(), 1u);
    EXPECT_EQ(durable_sink.attempted_events[0].kind, "turn_started");
}

TEST_F(DetailModeTest, RunFinishedIsAbsentEverywhereOnTraceFailure) {
    AgentConfig config;
    config.workspace_abs = workspace.string();
    config.detail_mode = true;
    config.max_turns = 3;
    config.max_context_bytes = 20000;

    SessionState session = make_session_state();
    FailAfterCommittedTraceSink durable_sink(1);
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) {
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    testing::internal::CaptureStderr();
    EXPECT_THROW(agent_run(config,
                           "system",
                           "user",
                           nlohmann::json::array(),
                           mock_llm,
                           &session,
                           nullptr,
                           &durable_sink),
                 std::runtime_error);
    const std::string stderr_text = testing::internal::GetCapturedStderr();

    EXPECT_NE(stderr_text.find("trace sink write failed"), std::string::npos);
    EXPECT_NE(session.scratchpad.find("trace sink write failed"), std::string::npos);
    EXPECT_EQ(find_trace_event(session.trace, "run_finished"), nullptr);
    EXPECT_EQ(find_trace_event(durable_sink.committed_events, "run_finished"), nullptr);
    ASSERT_EQ(durable_sink.attempted_events.size(), 1u);
    EXPECT_EQ(durable_sink.attempted_events[0].kind, "turn_started");
}
