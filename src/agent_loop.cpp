#include "agent_loop.hpp"
#include "agent_utils.hpp"
#include "agent_tools.hpp"
#include "tool_call.hpp"
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

constexpr std::size_t kFailedTestsJoinLimit = 3;
constexpr int kMaxFailureRetries = 1;
constexpr std::size_t kMaxFingerprintArgumentsBytes = 128;

enum class ToolFailureClass {
    None,
    Retryable,
    NeedsInspection,
    Fatal,
    Blocked,
};

struct ToolFailureAnalysis {
    bool is_failure = false;
    ToolFailureClass classification = ToolFailureClass::None;
    std::string guidance;
    std::string fingerprint;
    nlohmann::json missing_approvals = nlohmann::json::array();
};

const char* tool_failure_class_to_string(ToolFailureClass classification) {
    switch (classification) {
        case ToolFailureClass::None:
            return "none";
        case ToolFailureClass::Retryable:
            return "retryable";
        case ToolFailureClass::NeedsInspection:
            return "needs_inspection";
        case ToolFailureClass::Fatal:
            return "fatal";
        case ToolFailureClass::Blocked:
            return "blocked";
    }
    return "unknown";
}

std::string make_turn_id(int turn_index) {
    return "turn-" + std::to_string(turn_index);
}

std::string resolved_tool_call_id(const ToolCall& tc) {
    if (!tc.id.empty()) {
        return tc.id;
    }
    return "call_" + std::to_string(tc.index);
}

nlohmann::json make_trace_payload_base(const AgentConfig& config,
                                       const SessionState* session_state,
                                       int turn_index = 0) {
    nlohmann::json payload = {
        {"session_id", session_state != nullptr ? session_state->session_id : ""},
        {"mode", config.mode},
        {"model", config.model},
        {"prompt_tokens", nullptr},
        {"completion_tokens", nullptr},
        {"total_tokens", nullptr}
    };
    if (turn_index > 0) {
        payload["turn_id"] = make_turn_id(turn_index);
    }
    return payload;
}

TraceWriteResult emit_trace_event(TraceSink* sink,
                                  std::string kind,
                                  std::string message,
                                  nlohmann::json payload) {
    return sink->write(make_trace_event(std::move(kind),
                                        std::move(message),
                                        payload.is_object() ? payload : nlohmann::json::object()));
}

std::string make_trace_failure_message(const std::string& kind, const TraceWriteResult& result) {
    std::string message = "trace sink write failed while emitting " + kind;
    if (!result.error.empty()) {
        message += ": " + result.error;
    }
    return message;
}

std::string join_failed_tests(const nlohmann::json& failed_tests) {
    if (!failed_tests.is_array() || failed_tests.empty()) {
        return "";
    }

    std::string joined;
    const std::size_t limit = std::min<std::size_t>(failed_tests.size(), kFailedTestsJoinLimit);
    std::size_t appended_strings = 0;
    for (std::size_t i = 0; i < limit; ++i) {
        if (!failed_tests[i].is_string()) {
            continue;
        }
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += failed_tests[i].get<std::string>();
        ++appended_strings;
    }

    bool has_hidden_string = false;
    for (std::size_t i = limit; i < failed_tests.size(); ++i) {
        if (failed_tests[i].is_string()) {
            has_hidden_string = true;
            break;
        }
    }

    if (appended_strings > 0 && has_hidden_string) {
        joined += ", ...";
    }
    return joined;
}

struct FingerprintAccumulator {
    std::uint64_t hash = 14695981039346656037ull;
    std::size_t length = 0;
    std::string preview;

    void append_char(char ch) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ull;
        ++length;
        if (preview.size() < kMaxFingerprintArgumentsBytes) {
            preview.push_back(ch);
        }
    }

    void append_literal(const char* text) {
        for (const char* p = text; *p != '\0'; ++p) {
            append_char(*p);
        }
    }

    void append_string(const std::string& text) {
        for (char ch : text) {
            append_char(ch);
        }
    }
};

void append_json_fingerprint(const nlohmann::json& value, FingerprintAccumulator& accumulator);

void append_json_string_fingerprint(const std::string& value, FingerprintAccumulator& accumulator) {
    static constexpr char kHexDigits[] = "0123456789abcdef";

    accumulator.append_char('"');
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                accumulator.append_literal("\\\"");
                break;
            case '\\':
                accumulator.append_literal("\\\\");
                break;
            case '\b':
                accumulator.append_literal("\\b");
                break;
            case '\f':
                accumulator.append_literal("\\f");
                break;
            case '\n':
                accumulator.append_literal("\\n");
                break;
            case '\r':
                accumulator.append_literal("\\r");
                break;
            case '\t':
                accumulator.append_literal("\\t");
                break;
            default:
                if (ch < 0x20) {
                    accumulator.append_literal("\\u00");
                    accumulator.append_char(kHexDigits[(ch >> 4) & 0x0F]);
                    accumulator.append_char(kHexDigits[ch & 0x0F]);
                } else {
                    accumulator.append_char(static_cast<char>(ch));
                }
                break;
        }
    }
    accumulator.append_char('"');
}

void append_json_fingerprint(const nlohmann::json& value, FingerprintAccumulator& accumulator) {
    switch (value.type()) {
        case nlohmann::json::value_t::null:
            accumulator.append_literal("null");
            return;
        case nlohmann::json::value_t::boolean:
            accumulator.append_literal(value.get<bool>() ? "true" : "false");
            return;
        case nlohmann::json::value_t::number_integer:
            accumulator.append_string(std::to_string(value.get<std::int64_t>()));
            return;
        case nlohmann::json::value_t::number_unsigned:
            accumulator.append_string(std::to_string(value.get<std::uint64_t>()));
            return;
        case nlohmann::json::value_t::number_float:
            accumulator.append_string(value.dump());
            return;
        case nlohmann::json::value_t::string:
            append_json_string_fingerprint(value.get_ref<const std::string&>(), accumulator);
            return;
        case nlohmann::json::value_t::array: {
            accumulator.append_char('[');
            bool first = true;
            for (const auto& item : value) {
                if (!first) {
                    accumulator.append_char(',');
                }
                append_json_fingerprint(item, accumulator);
                first = false;
            }
            accumulator.append_char(']');
            return;
        }
        case nlohmann::json::value_t::object: {
            accumulator.append_char('{');
            bool first = true;
            for (const auto& item : value.items()) {
                if (!first) {
                    accumulator.append_char(',');
                }
                append_json_string_fingerprint(item.key(), accumulator);
                accumulator.append_char(':');
                append_json_fingerprint(item.value(), accumulator);
                first = false;
            }
            accumulator.append_char('}');
            return;
        }
        case nlohmann::json::value_t::binary:
            accumulator.append_literal("<binary:");
            accumulator.append_string(std::to_string(value.get_binary().size()));
            accumulator.append_char('>');
            return;
        case nlohmann::json::value_t::discarded:
            accumulator.append_literal("<discarded>");
            return;
    }
}

std::string compact_arguments_fingerprint(const nlohmann::json& arguments) {
    FingerprintAccumulator accumulator;
    append_json_fingerprint(arguments, accumulator);

    if (accumulator.length <= kMaxFingerprintArgumentsBytes) {
        return accumulator.preview;
    }

    return "args[len=" + std::to_string(accumulator.length) + ",hash=" + std::to_string(accumulator.hash) + "]";
}

ToolFailureAnalysis make_failure_analysis(ToolFailureClass classification,
                                          std::string guidance,
                                          std::string fingerprint,
                                          nlohmann::json missing_approvals = nlohmann::json::array()) {
    return {
        true,
        classification,
        std::move(guidance),
        std::move(fingerprint),
        std::move(missing_approvals),
    };
}

ToolFailureAnalysis analyze_apply_patch_result(const ToolCall& tc,
                                               const nlohmann::json& result,
                                               bool ok) {
    if (ok) {
        return {};
    }

    const std::string reject_code = result.value("reject_code", "");
    const int patch_index = result.value("patch_index", -1);
    const std::string arguments_fingerprint = compact_arguments_fingerprint(tc.arguments);
    const std::string fingerprint = "apply_patch:" + reject_code + ":" + std::to_string(patch_index) +
                                    ":" + arguments_fingerprint;

    if (reject_code == "writeback_failure") {
        return make_failure_analysis(ToolFailureClass::Fatal, "", fingerprint);
    }

    if (reject_code == "no_match") {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect the current file content and adjust old_text so it matches exactly once before retrying.",
            fingerprint);
    }
    if (reject_code == "multiple_matches") {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect the file and narrow the patch target to a unique snippet before retrying.",
            fingerprint);
    }
    if (reject_code == "empty_old_text" || reject_code == "invalid_batch_entry") {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: fix the patch arguments before retrying; this failure came from invalid patch input, not writeback.",
            fingerprint);
    }
    if (reject_code == "file_read_failure" || reject_code == "binary_file" || reject_code == "truncated_file") {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect the target path and file type before retrying; the patch did not reach writeback.",
            fingerprint);
    }

    return make_failure_analysis(ToolFailureClass::Fatal, "", fingerprint);
}

ToolFailureAnalysis analyze_build_test_result(const ToolCall& tc,
                                              const nlohmann::json& result,
                                              bool ok,
                                              bool timed_out,
                                              const std::string& status) {
    const std::string joined_failed_tests =
        join_failed_tests(result.value("failed_tests", nlohmann::json::array()));
    const std::string arguments_fingerprint = compact_arguments_fingerprint(tc.arguments);
    const std::string fingerprint = tc.name + ":" + status + ":" + std::to_string(result.value("exit_code", -1)) +
                                    ":" + (timed_out ? "timed_out" : "not_timed_out") + ":" + arguments_fingerprint +
                                    ":" + joined_failed_tests;

    if (ok && status != "timed_out") {
        return {};
    }

    if (timed_out || status == "timed_out") {
        return make_failure_analysis(
            ToolFailureClass::Retryable,
            "Recommended next action: inspect the timeout summary and either narrow the command scope or increase timeout_ms before retrying.",
            fingerprint);
    }

    if (status == "failed" || !ok) {
        if (tc.name == "test_project_safe") {
            std::string guidance =
                "Recommended next action: inspect summary/stdout/stderr and the reported failed tests before choosing the next fix.";
            if (!joined_failed_tests.empty()) {
                guidance += " Reported failed_tests: " + joined_failed_tests + ".";
            }
            return make_failure_analysis(
                ToolFailureClass::NeedsInspection,
                std::move(guidance),
                fingerprint);
        }

        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect summary/stdout/stderr to understand the build failure before changing code or retrying.",
            fingerprint);
    }

    return {};
}

ToolFailureAnalysis analyze_tool_result(const ToolCall& tc,
                                        const nlohmann::json& result,
                                        bool parsed_ok) {
    if (!parsed_ok) {
        return make_failure_analysis(
            ToolFailureClass::Fatal,
            "",
            "fatal:non_json:" + tc.name);
    }

    if (!result.is_object()) {
        return make_failure_analysis(
            ToolFailureClass::Fatal,
            "",
            "fatal:non_object:" + tc.name);
    }

    const bool ok = result.value("ok", true);
    const bool timed_out = result.value("timed_out", false);
    const std::string status = result.value("status", "");

    if (status == "blocked") {
        return make_failure_analysis(
            ToolFailureClass::Blocked,
            "Recommended next action: use an allowed read-only inspection tool or change approval settings outside the run. Do not repeat the blocked call unchanged.",
            "blocked:" + tc.name + ":" + compact_arguments_fingerprint(tc.arguments),
            result.value("missing_approvals", nlohmann::json::array()));
    }

    if (tc.name == "apply_patch") {
        return analyze_apply_patch_result(tc, result, ok);
    }

    if (tc.name == "build_project_safe" || tc.name == "test_project_safe") {
        return analyze_build_test_result(tc, result, ok, timed_out, status);
    }

    if (!ok || timed_out || status == "failed" || status == "timed_out") {
        return make_failure_analysis(
            ToolFailureClass::Fatal,
            "",
            "fatal:" + tc.name + ":" + compact_arguments_fingerprint(tc.arguments));
    }

    return {};
}

std::string make_recovery_guidance_message(const ToolCall& tc, const ToolFailureAnalysis& analysis) {
    return "Tool recovery guidance for " + tc.name + ": classification=" +
           tool_failure_class_to_string(analysis.classification) + ". " + analysis.guidance;
}

std::string make_skipped_tool_output() {
    return nlohmann::json{
        {"ok", false},
        {"status", "skipped"},
        {"reason", "not executed due to prior tool failure"},
    }.dump();
}

void append_tool_message(nlohmann::json& messages,
                         const std::string& tool_call_id,
                         const std::string& name,
                         const std::string& content) {
    messages.push_back({
        {"role", "tool"},
        {"tool_call_id", tool_call_id},
        {"name", name},
        {"content", content},
    });
}

ToolCall parse_tool_call_json(const nlohmann::json& tc_json) {
    ToolCall tc;
    tc.id = tc_json.value("id", "");
    tc.index = tc_json.value("index", 0);

    if (!tc_json.contains("function")) {
        return tc;
    }

    const auto& function_json = tc_json["function"];
    tc.name = function_json.value("name", "");

    if (!function_json.contains("arguments")) {
        tc.raw_arguments = "{}";
        tc.arguments = nlohmann::json::object();
        return tc;
    }

    const auto& raw_arguments = function_json["arguments"];
    try {
        if (raw_arguments.is_string()) {
            tc.raw_arguments = raw_arguments.get<std::string>();
            tc.arguments = nlohmann::json::parse(tc.raw_arguments);
        } else {
            tc.arguments = raw_arguments;
            tc.raw_arguments = raw_arguments.dump();
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Tool JSON parse failed for " + tc.name + ": " + e.what());
        tc.arguments = nlohmann::json::object();
    }

    return tc;
}

bool try_parse_tool_result(const ToolCall& tc, const std::string& raw_output, nlohmann::json* result) {
    if (result == nullptr) {
        return false;
    }

    try {
        *result = nlohmann::json::parse(raw_output);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Tool result JSON parse failed for " + tc.name + ": " + e.what());
        *result = nlohmann::json::object();
        return false;
    }

    return result->is_object();
}

}  // namespace

void agent_run(const AgentConfig& config,
               const std::string& system_prompt,
               const std::string& user_prompt,
               const nlohmann::json& tools_registry,
               LLMStreamFunc llm_func,
               SessionState* session_state,
               const ToolRegistry* tool_registry,
               TraceSink* trace_sink) {
    nlohmann::json local_messages = nlohmann::json::array();
    nlohmann::json& messages = session_state != nullptr ? session_state->messages : local_messages;
    const ToolRegistry& runtime_registry = tool_registry != nullptr ? *tool_registry : get_default_tool_registry();

    if (session_state != nullptr) {
        if (!messages.is_array()) {
            messages = nlohmann::json::array();
        }
        seed_session_messages_if_empty(*session_state, system_prompt, user_prompt);
    } else {
        if (!system_prompt.empty()) {
            messages.push_back({{"role", "system"}, {"content", system_prompt}});
        }
        messages.push_back({{"role", "user"}, {"content", user_prompt}});
    }

    int turns = session_state != nullptr ? session_state->turn_index : 0;
    int total_tool_calls = session_state != nullptr ? session_state->counters.tool_calls_requested : 0;
    std::unordered_map<std::string, int> failure_retry_counts;
    std::string run_outcome = "completed";
    std::string run_message = "agent run completed";

    NullTraceSink null_trace_sink;
    FanoutTraceSink fanout_trace_sink;
    bool has_trace_sink = false;
    SessionTraceSink session_trace_sink(session_state);
    if (trace_sink != nullptr) {
        fanout_trace_sink.add_sink(trace_sink);
        has_trace_sink = true;
    }
    if (config.detail_mode && session_state != nullptr) {
        // Keep the durable trace source authoritative: only mirror into session
        // after the external sink has accepted the event.
        fanout_trace_sink.add_sink(&session_trace_sink);
        has_trace_sink = true;
    }
    TraceSink* active_trace_sink = has_trace_sink
        ? static_cast<TraceSink*>(&fanout_trace_sink)
        : static_cast<TraceSink*>(&null_trace_sink);

    auto build_trace_payload = [&](int turn_index = 0) {
        return make_trace_payload_base(config, session_state, turn_index);
    };
    auto emit_trace = [&](std::string kind, std::string message, nlohmann::json payload) {
        const std::string trace_kind = kind;
        const TraceWriteResult result = emit_trace_event(active_trace_sink,
                                                         std::move(kind),
                                                         std::move(message),
                                                         std::move(payload));
        if (result.ok) {
            return;
        }

        const std::string failure_message = make_trace_failure_message(trace_kind, result);
        LOG_ERROR("{}", failure_message);
        std::cerr << "[Agent Error] " << failure_message << "\n";
        if (session_state != nullptr) {
            set_session_scratchpad(*session_state, failure_message);
        }
        throw std::runtime_error(failure_message);
    };

    nlohmann::json run_started_payload = build_trace_payload();
    run_started_payload["detail_mode"] = config.detail_mode;
    run_started_payload["trace_jsonl_enabled"] = trace_sink != nullptr;
    emit_trace("run_started", "agent run started", std::move(run_started_payload));

    while (true) {
        turns++;
        if (session_state != nullptr) {
            session_state->turn_index = turns;
            session_state->counters.llm_turns = turns;
            set_session_scratchpad(*session_state,
                                   "turn " + std::to_string(turns) + ": awaiting assistant response");
        }
        if (turns > config.max_turns) {
            LOG_ERROR("Broker loop hit max_turns (" + std::to_string(config.max_turns) + "), aborting to prevent infinite loop.");
            std::cerr << "[Agent Error] Exceeded max standard turns.\n";
            if (session_state != nullptr) {
                set_session_scratchpad(*session_state,
                                       "turn " + std::to_string(turns) + ": exceeded max_turns");
            }
            run_outcome = "max_turns_exceeded";
            run_message = "agent run exceeded max_turns";
            break;
        }

        enforce_context_limits(messages, config.max_context_bytes);

        LOG_INFO("Agent Turn " + std::to_string(turns) + " started...");
        emit_trace("turn_started", "agent turn started", build_trace_payload(turns));

        // Execute LLM Step
        nlohmann::json response_message;
        try {
            response_message = llm_func(config, messages, tools_registry);
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("LLM Execution failed: ") + e.what());
            std::cerr << "[Agent Error] LLM request failed: " << e.what() << "\n";
            if (session_state != nullptr) {
                set_session_scratchpad(*session_state,
                                       "turn " + std::to_string(turns) + ": llm request failed");
            }
            run_outcome = "llm_request_failed";
            run_message = std::string("llm request failed: ") + e.what();
            break;
        }

        nlohmann::json llm_payload = build_trace_payload(turns);
        const bool has_content =
            response_message.contains("content") && response_message.at("content").is_string() &&
            !response_message.value("content", "").empty();
        llm_payload["has_content"] = has_content;
        llm_payload["tool_call_count"] =
            response_message.contains("tool_calls") && response_message.at("tool_calls").is_array()
                ? response_message.at("tool_calls").size()
                : 0;
        emit_trace("llm_response_received", "assistant response received", std::move(llm_payload));

        messages.push_back(response_message);
        if (session_state != nullptr) {
            touch_session(*session_state);
        }

        if (!response_message.contains("tool_calls") || response_message["tool_calls"].empty()) {
            LOG_INFO("No tool calls. Agent loop complete.");
            std::cout << "[Agent Complete] " << response_message.value("content", "") << "\n";
            if (session_state != nullptr) {
                const std::string content = response_message.value("content", "");
                set_session_scratchpad(*session_state,
                                       content.empty()
                                           ? "turn " + std::to_string(turns) + ": completed"
                                           : content);
            }
            run_outcome = "completed";
            run_message = "agent run completed";
            break;
        }

        auto tool_calls = response_message["tool_calls"];
        if (tool_calls.size() > config.max_tool_calls_per_turn) {
            LOG_ERROR("Too many tools requested in turn: " + std::to_string(tool_calls.size()));
            std::cerr << "[Agent Error] Tool flood detected, limit " << config.max_tool_calls_per_turn << ". Aborting.\n";
            if (session_state != nullptr) {
                set_session_scratchpad(*session_state,
                                       "turn " + std::to_string(turns) + ": exceeded max_tool_calls_per_turn");
            }
            run_outcome = "max_tool_calls_per_turn_exceeded";
            run_message = "agent run exceeded max_tool_calls_per_turn";
            break;
        }

        total_tool_calls += tool_calls.size();
        if (session_state != nullptr) {
            session_state->counters.tool_calls_requested = total_tool_calls;
            set_session_scratchpad(*session_state,
                                   "turn " + std::to_string(turns) + ": executing " +
                                       std::to_string(tool_calls.size()) + " tool call(s)");
        }
        if (total_tool_calls > config.max_total_tool_calls) {
            LOG_ERROR("Max total tool calls exceeded: " + std::to_string(total_tool_calls));
            std::cerr << "[Agent Error] Global tool call limit hit, aborting.\n";
            if (session_state != nullptr) {
                set_session_scratchpad(*session_state,
                                       "turn " + std::to_string(turns) + ": exceeded max_total_tool_calls");
            }
            run_outcome = "max_total_tool_calls_exceeded";
            run_message = "agent run exceeded max_total_tool_calls";
            break;
        }

        bool state_contaminated = false;

        // Ensure sequentially execute tools
        for (std::size_t tool_index = 0; tool_index < tool_calls.size(); ++tool_index) {
            const auto& tc_json = tool_calls[tool_index];
            ToolCall tc = parse_tool_call_json(tc_json);
            const std::string tool_call_id = resolved_tool_call_id(tc);

            nlohmann::json tool_started_payload = build_trace_payload(turns);
            tool_started_payload["tool_call_id"] = tool_call_id;
            tool_started_payload["tool_name"] = tc.name;
            emit_trace("tool_call_started", "tool call started", std::move(tool_started_payload));

            std::size_t tool_record_index = 0;
            if (session_state != nullptr) {
                tool_record_index = append_tool_call_record(*session_state, turns, tc);
            }

            ToolExecutionContext execution_context;
            if (session_state != nullptr) {
                execution_context.session_state = session_state;
                execution_context.turn_index = turns;
                execution_context.tool_call_id = tool_call_id;
            }

            const std::string raw_output = runtime_registry.execute(
                tc,
                config,
                session_state != nullptr ? &execution_context : nullptr);
            nlohmann::json tool_result = nlohmann::json::object();
            const bool parsed_tool_result = try_parse_tool_result(tc, raw_output, &tool_result);
            const ToolFailureAnalysis analysis = analyze_tool_result(tc, tool_result, parsed_tool_result);
            const std::string tool_status = tool_call_status_from_output(raw_output);
            std::string output = raw_output;

            // Output length guard
            output = truncate_tool_output(output, config.max_tool_output_bytes);

            append_tool_message(messages, tc.id, tc.name, output);
            if (session_state != nullptr) {
                finish_tool_call_record(*session_state, tool_record_index, tool_status);
                append_observation_record(*session_state,
                                          turns,
                                          tool_call_id,
                                          tc.name,
                                          output);
            }

            nlohmann::json tool_finished_payload = build_trace_payload(turns);
            tool_finished_payload["tool_call_id"] = tool_call_id;
            tool_finished_payload["tool_name"] = tc.name;
            tool_finished_payload["tool_status"] = tool_status;
            tool_finished_payload["missing_approvals"] = analysis.missing_approvals;
            if (parsed_tool_result && tool_result.contains("reason") && tool_result.at("reason").is_string()) {
                tool_finished_payload["reason"] = tool_result.at("reason");
            }
            emit_trace("tool_call_finished", "tool call finished", std::move(tool_finished_payload));

            if (!analysis.is_failure) {
                continue;
            }

            nlohmann::json failure_payload = build_trace_payload(turns);
            failure_payload["tool_call_id"] = tool_call_id;
            failure_payload["tool_name"] = tc.name;
            failure_payload["tool_status"] = tool_status;
            failure_payload["failure_class"] = tool_failure_class_to_string(analysis.classification);
            failure_payload["missing_approvals"] = analysis.missing_approvals;
            emit_trace("tool_failure_classified", "tool failure classified", std::move(failure_payload));

            if (analysis.classification == ToolFailureClass::Fatal) {
                LOG_ERROR("Tool failed fatally: " + output);
                std::cerr << "[Agent Error] Tool " << tc.name << " failed fatally: " << output << "\n";
                if (session_state != nullptr) {
                    set_session_scratchpad(*session_state,
                                           "turn " + std::to_string(turns) + ": fatal tool failure in " + tc.name);
                }
                run_outcome = "fatal_tool_failure";
                run_message = "fatal tool failure in " + tc.name;
                state_contaminated = true;
                break;
            }

            const int seen_count = ++failure_retry_counts[analysis.fingerprint];
            if (seen_count > kMaxFailureRetries) {
                LOG_ERROR("Tool failure repeated beyond retry budget: " + output);
                std::cerr << "[Agent Error] Tool " << tc.name
                          << " repeated the same recoverable failure and exceeded the retry budget.\n";
                if (session_state != nullptr) {
                    set_session_scratchpad(*session_state,
                                           "turn " + std::to_string(turns) + ": retry budget exceeded for " + tc.name);
                }
                run_outcome = "retry_budget_exceeded";
                run_message = "retry budget exceeded for " + tc.name;
                state_contaminated = true;
                break;
            }

            LOG_ERROR("Tool failed but is recoverable: " + output);
            messages.push_back({
                {"role", "system"},
                {"content", make_recovery_guidance_message(tc, analysis)}
            });
            if (session_state != nullptr) {
                set_session_scratchpad(*session_state,
                                       "turn " + std::to_string(turns) + ": recoverable tool failure in " + tc.name);
            }
            for (std::size_t skipped_index = tool_index + 1; skipped_index < tool_calls.size(); ++skipped_index) {
                const auto& skipped_tc_json = tool_calls[skipped_index];
                const std::string skipped_tool_call_id = skipped_tc_json.value("id", "");
                std::string skipped_name;
                if (skipped_tc_json.contains("function")) {
                    skipped_name = skipped_tc_json["function"].value("name", "");
                }
                const ToolCall skipped_tc = parse_tool_call_json(skipped_tc_json);
                const std::string resolved_skipped_tool_call_id = resolved_tool_call_id(skipped_tc);

                const std::string skipped_output = make_skipped_tool_output();
                append_tool_message(messages, skipped_tool_call_id, skipped_name, skipped_output);
                if (session_state != nullptr) {
                    append_skipped_tool_call_record(*session_state, turns, skipped_tc);
                    append_observation_record(*session_state,
                                              turns,
                                              resolved_skipped_tool_call_id,
                                              skipped_name,
                                              skipped_output);
                }

                nlohmann::json skipped_result = nlohmann::json::object();
                const bool parsed_skipped_result = try_parse_tool_result(skipped_tc, skipped_output, &skipped_result);
                nlohmann::json skipped_finished_payload = build_trace_payload(turns);
                skipped_finished_payload["tool_call_id"] = resolved_skipped_tool_call_id;
                skipped_finished_payload["tool_name"] = skipped_name;
                skipped_finished_payload["tool_status"] = tool_call_status_from_output(skipped_output);
                if (parsed_skipped_result && skipped_result.contains("reason") &&
                    skipped_result.at("reason").is_string()) {
                    skipped_finished_payload["reason"] = skipped_result.at("reason");
                }
                emit_trace("tool_call_finished", "tool call finished", std::move(skipped_finished_payload));
            }
            break;
        }

        if (state_contaminated) {
            std::cerr << "[Agent Error] Run stopped due to state contamination (tool failure or timeout).\n";
            if (session_state != nullptr && session_state->scratchpad.empty()) {
                set_session_scratchpad(*session_state,
                                       "turn " + std::to_string(turns) + ": state contamination");
            }
            break;
        }
    }

    nlohmann::json run_finished_payload = build_trace_payload(turns > 0 ? turns : 0);
    run_finished_payload["outcome"] = run_outcome;
    run_finished_payload["turn_count"] = turns;
    run_finished_payload["tool_calls_requested"] = total_tool_calls;
    emit_trace("run_finished", run_message, std::move(run_finished_payload));
}
