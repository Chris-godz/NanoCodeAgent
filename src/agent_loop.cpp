#include "agent_loop.hpp"
#include "agent_utils.hpp"
#include "agent_tools.hpp"
#include "llm.hpp"
#include "tool_call.hpp"
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
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
constexpr std::string_view kPlanStateIdle = "IDLE";
constexpr std::string_view kPlanStateAwaitingPlan = "AWAITING_PLAN";
constexpr std::string_view kPlanStateAwaitingRepair = "AWAITING_REPAIR";
constexpr std::string_view kPlanStatePlanReady = "PLAN_READY";
constexpr std::string_view kPlanStatePlanInvalid = "PLAN_INVALID";

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

bool session_has_active_plan(const SessionState* session_state) {
    if (session_state == nullptr) {
        return false;
    }
    const Plan& plan = session_state->plan;
    return !plan.steps.empty() &&
           plan.current_step_index >= 0 &&
           plan.current_step_index < static_cast<int>(plan.steps.size()) &&
           plan.outcome == "in_progress";
}

bool session_plan_ready_for_execution(const SessionState* session_state) {
    return session_has_active_plan(session_state) &&
           session_state->plan_state == kPlanStatePlanReady;
}

bool session_waiting_on_plan_contract(const SessionState* session_state) {
    if (session_state == nullptr || !session_state->needs_plan) {
        return false;
    }
    return session_state->plan_state == kPlanStateAwaitingPlan ||
           session_state->plan_state == kPlanStateAwaitingRepair;
}

void clear_planner_contract_fields(SessionState* session_state) {
    if (session_state == nullptr) {
        return;
    }
    session_state->plan_raw_response = nlohmann::json::object();
    session_state->plan_validated_artifact = nlohmann::json::object();
    session_state->plan_validation_errors.clear();
    session_state->plan_normalization_applied = false;
    session_state->plan_repair_attempts = 0;
}

void set_plan_state(SessionState* session_state, std::string_view plan_state) {
    if (session_state == nullptr) {
        return;
    }
    session_state->plan_state = std::string(plan_state);
}

void begin_planner_contract(SessionState* session_state) {
    if (session_state == nullptr) {
        return;
    }
    clear_planner_contract_fields(session_state);
    set_plan_state(session_state, kPlanStateAwaitingPlan);
}

std::string lower_copy(const std::string& text) {
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

std::string normalize_planner_repair_prompt_version(std::string value) {
    const std::string lowered = lower_copy(value);
    if (lowered == "v1" || lowered == "v2" || lowered == "v3" ||
        lowered == "auto" || lowered.empty() ||
        lowered == "provider_profile" || lowered == "provider-profile") {
        return lowered;
    }
    return "auto";
}

struct PlannerRepairPromptVersionSelection {
    std::string version;
    std::string reason;
};

PlannerRepairPromptVersionSelection resolve_planner_repair_prompt_version(const AgentConfig& config) {
    const std::string normalized = normalize_planner_repair_prompt_version(
        config.planner_repair_prompt_version);
    const bool deepseek_profile = llm_provider_profile_is_deepseek(config);

    if (normalized == "v1" || normalized == "v2" || normalized == "v3") {
        return PlannerRepairPromptVersionSelection{
            .version = normalized,
            .reason = "explicit_prompt_version"
        };
    }

    if (deepseek_profile) {
        return PlannerRepairPromptVersionSelection{
            .version = "v2",
            .reason = "provider_profile_default_deepseek_v2"
        };
    }

    return PlannerRepairPromptVersionSelection{
        .version = "v3",
        .reason = "provider_profile_default_v3_non_deepseek"
    };
}

void append_plan_repair_validation_errors(std::string* prompt,
                                          const std::vector<std::string>& errors) {
    if (prompt == nullptr) {
        return;
    }

    *prompt += "Validation errors:\n";
    if (errors.empty()) {
        *prompt += "- previous planner response violated the runtime contract\n";
        return;
    }
    for (const std::string& error : errors) {
        *prompt += "- " + error + "\n";
    }
}

std::string build_plan_repair_prompt_v1(const std::vector<std::string>& errors) {
    std::string prompt =
        "Planner contract repair required.\n"
        "Return ONLY one JSON object.\n"
        "No markdown.\n"
        "No explanation.\n"
        "No tool calls.\n"
        "The top-level object must contain key \"plan\".\n"
        "plan.steps must be a non-empty array.\n"
        "Each item in plan.steps must be an object with string fields id, title, and detail.\n"
        "plan.summary may be a string.\n"
        "plan.metadata may be an object.\n";
    append_plan_repair_validation_errors(&prompt, errors);
    return prompt;
}

std::string build_plan_repair_prompt_v2(const std::vector<std::string>& errors) {
    std::string prompt =
        "Planner contract repair required.\n"
        "Return ONLY one JSON object.\n"
        "No markdown.\n"
        "No explanation.\n"
        "No tool calls.\n"
        "The top-level object must contain key \"plan\".\n"
        "plan.steps must be a non-empty array.\n"
        "Each item in plan.steps must be an object with string fields id, title, and detail.\n"
        "plan.summary may be a string.\n"
        "plan.metadata may be an object.\n"
        "Example valid shape:\n"
        "{\"plan\":{\"summary\":\"short summary\",\"steps\":[{\"id\":\"step-1\",\"title\":\"first task\",\"detail\":\"do the first task\"}],\"metadata\":{}}}\n";
    append_plan_repair_validation_errors(&prompt, errors);
    return prompt;
}

std::string build_plan_repair_prompt_v3(const std::vector<std::string>& errors) {
    std::string prompt =
        "Planner contract repair required.\n"
        "Return ONLY one JSON object.\n"
        "No markdown.\n"
        "No explanation.\n"
        "No tool calls.\n"
        "Repair the previous invalid planner response using the errors below.\n"
        "The top-level object must contain key \"plan\".\n"
        "plan.steps must be a non-empty array, never an object.\n"
        "Each item in plan.steps must be an object with string fields id, title, and detail.\n"
        "plan.summary may be a string.\n"
        "plan.metadata may be an object.\n";
    append_plan_repair_validation_errors(&prompt, errors);
    prompt +=
        "Minimal legal template:\n"
        "{\"plan\":{\"summary\":\"<optional summary>\",\"steps\":[{\"id\":\"step-1\",\"title\":\"<title>\",\"detail\":\"<detail>\"}]}}\n";
    return prompt;
}

bool prompt_needs_plan(const std::string& prompt) {
    if (prompt.size() < 80) {
        return false;
    }

    const std::string lowered = lower_copy(prompt);
    int matched_keywords = 0;
    for (const std::string& keyword : {
             std::string("step"),
             std::string("then"),
             std::string("finally"),
             std::string("refactor"),
             std::string("plan"),
             std::string("multi"),
         }) {
        if (lowered.find(keyword) != std::string::npos) {
            ++matched_keywords;
        }
    }

    return matched_keywords >= 2;
}

bool try_parse_embedded_content_object(const nlohmann::json& response_message, nlohmann::json* out) {
    if (out == nullptr ||
        !response_message.contains("content") ||
        !response_message.at("content").is_string()) {
        return false;
    }

    const std::string& content = response_message.at("content").get_ref<const std::string&>();
    if (content.empty()) {
        return false;
    }

    try {
        nlohmann::json parsed = nlohmann::json::parse(content);
        if (!parsed.is_object()) {
            return false;
        }
        *out = std::move(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_step_key(const std::string& key, int* order) {
    if (!key.starts_with("step-")) {
        return false;
    }
    const std::string numeric_part = key.substr(5);
    if (numeric_part.empty()) {
        return false;
    }
    for (unsigned char ch : numeric_part) {
        if (!std::isdigit(ch)) {
            return false;
        }
    }
    int parsed_order = 0;
    try {
        parsed_order = std::stoi(numeric_part);
    } catch (...) {
        return false;
    }
    if (parsed_order <= 0) {
        return false;
    }
    if (order != nullptr) {
        *order = parsed_order;
    }
    return true;
}

void add_plan_validation_error(std::vector<std::string>* errors, std::string message) {
    if (errors != nullptr) {
        errors->push_back(std::move(message));
    }
}

bool try_extract_plan_contract_candidate(const nlohmann::json& response_message,
                                         nlohmann::json* out,
                                         std::vector<std::string>* errors = nullptr) {
    if (out == nullptr) {
        add_plan_validation_error(errors, "Planner contract extraction requires a non-null output object.");
        return false;
    }

    if (response_message.contains("plan")) {
        if (!response_message.at("plan").is_object()) {
            add_plan_validation_error(errors, "Planner response field 'plan' must be an object.");
            return false;
        }
        *out = nlohmann::json{{"plan", response_message.at("plan")}};
        return true;
    }

    nlohmann::json embedded = nlohmann::json::object();
    if (!try_parse_embedded_content_object(response_message, &embedded)) {
        add_plan_validation_error(errors,
                                  "Planner response must provide a top-level object field 'plan' or JSON object content containing one.");
        return false;
    }
    if (!embedded.contains("plan")) {
        add_plan_validation_error(errors,
                                  "Planner response content must include a top-level object field 'plan'.");
        return false;
    }
    if (!embedded.at("plan").is_object()) {
        add_plan_validation_error(errors, "Planner response field 'plan' must be an object.");
        return false;
    }

    *out = nlohmann::json{{"plan", embedded.at("plan")}};
    return true;
}

bool response_contains_plan_contract(const nlohmann::json& response_message) {
    nlohmann::json unused = nlohmann::json::object();
    return try_extract_plan_contract_candidate(response_message, &unused, nullptr);
}

bool validate_plan_contract_candidate(const nlohmann::json& response_message,
                                      const nlohmann::json& contract,
                                      std::vector<std::string>* errors) {
    if (response_message.contains("tool_calls") &&
        response_message.at("tool_calls").is_array() &&
        !response_message.at("tool_calls").empty()) {
        add_plan_validation_error(errors, "Planner response must not include tool calls while awaiting a plan contract.");
    }

    if (!contract.is_object() || !contract.contains("plan") || !contract.at("plan").is_object()) {
        add_plan_validation_error(errors, "Planner contract must be a JSON object containing an object field 'plan'.");
        return errors == nullptr || errors->empty();
    }

    const auto& plan_json = contract.at("plan");
    if (plan_json.contains("summary") && !plan_json.at("summary").is_string()) {
        add_plan_validation_error(errors, "Planner response field 'plan.summary' must be a string when present.");
    }
    if (plan_json.contains("metadata") && !plan_json.at("metadata").is_object()) {
        add_plan_validation_error(errors, "Planner response field 'plan.metadata' must be an object when present.");
    }
    if (!plan_json.contains("steps")) {
        add_plan_validation_error(errors, "Planner response must include field 'plan.steps'.");
        return errors == nullptr || errors->empty();
    }

    const auto& steps = plan_json.at("steps");
    if (steps.is_array()) {
        if (steps.empty()) {
            add_plan_validation_error(errors, "Planner response field 'plan.steps' must be a non-empty array.");
        }
        return errors == nullptr || errors->empty();
    }
    if (steps.is_object()) {
        add_plan_validation_error(errors,
                                  "Planner response field 'plan.steps' must be a non-empty array; keyed objects require repair.");
        return errors == nullptr || errors->empty();
    }
    add_plan_validation_error(errors,
                              "Planner response field 'plan.steps' must be a non-empty array.");
    return errors == nullptr || errors->empty();
}

bool normalize_plan_contract(nlohmann::json* contract, bool* normalization_applied) {
    if (normalization_applied != nullptr) {
        *normalization_applied = false;
    }
    if (contract == nullptr ||
        !contract->is_object() ||
        !contract->contains("plan") ||
        !contract->at("plan").is_object() ||
        !contract->at("plan").contains("steps") ||
        !contract->at("plan").at("steps").is_object()) {
        return true;
    }

    std::vector<std::pair<int, nlohmann::json>> ordered_steps;
    const auto& step_object = contract->at("plan").at("steps");
    ordered_steps.reserve(step_object.size());
    for (auto it = step_object.begin(); it != step_object.end(); ++it) {
        int order = 0;
        if (!parse_step_key(it.key(), &order)) {
            return false;
        }
        ordered_steps.push_back({order, it.value()});
    }

    std::sort(ordered_steps.begin(), ordered_steps.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    nlohmann::json normalized_steps = nlohmann::json::array();
    for (const auto& [order, value] : ordered_steps) {
        (void)order;
        normalized_steps.push_back(value);
    }
    (*contract)["plan"]["steps"] = std::move(normalized_steps);
    if (normalization_applied != nullptr) {
        *normalization_applied = true;
    }
    return true;
}

bool validate_normalized_plan_contract(const nlohmann::json& contract,
                                       std::vector<std::string>* errors) {
    if (!contract.is_object() || !contract.contains("plan") || !contract.at("plan").is_object()) {
        add_plan_validation_error(errors, "Planner contract must be a JSON object containing an object field 'plan'.");
        return errors == nullptr || errors->empty();
    }

    const auto& plan_json = contract.at("plan");
    if (plan_json.contains("summary") && !plan_json.at("summary").is_string()) {
        add_plan_validation_error(errors, "Planner response field 'plan.summary' must be a string when present.");
    }
    if (plan_json.contains("metadata") && !plan_json.at("metadata").is_object()) {
        add_plan_validation_error(errors, "Planner response field 'plan.metadata' must be an object when present.");
    }
    if (!plan_json.contains("steps") || !plan_json.at("steps").is_array() || plan_json.at("steps").empty()) {
        add_plan_validation_error(errors, "Planner response field 'plan.steps' must be a non-empty array.");
        return errors == nullptr || errors->empty();
    }

    const auto& steps = plan_json.at("steps");
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const auto& step_json = steps.at(index);
        if (!step_json.is_object()) {
            add_plan_validation_error(errors,
                                      "Planner response field 'plan.steps[" + std::to_string(index) + "]' must be an object.");
            continue;
        }
        const std::string index_text = std::to_string(index);
        for (const char* required_field : {"id", "title", "detail"}) {
            if (!step_json.contains(required_field) || !step_json.at(required_field).is_string()) {
                add_plan_validation_error(
                    errors,
                    "Planner response field 'plan.steps[" + index_text + "]." + required_field +
                        "' must be a string.");
            }
        }
        try {
            (void)step_json.get<PlanStep>();
        } catch (const std::exception& e) {
            add_plan_validation_error(errors,
                                      "Planner response field 'plan.steps[" + std::to_string(index) +
                                          "]' is invalid: " + e.what());
        }
    }

    return errors == nullptr || errors->empty();
}

std::string build_plan_repair_prompt_for_config(const AgentConfig& config,
                                                const std::vector<std::string>& errors) {
    const PlannerRepairPromptVersionSelection prompt_selection =
        resolve_planner_repair_prompt_version(config);
    const std::string& version = prompt_selection.version;
    if (version == "v1") {
        return build_plan_repair_prompt_v1(errors);
    }
    if (version == "v2") {
        return build_plan_repair_prompt_v2(errors);
    }
    return build_plan_repair_prompt_v3(errors);
}

void apply_usage_to_trace_payload(nlohmann::json* payload, const nlohmann::json& usage) {
    if (payload == nullptr || !payload->is_object() || !usage.is_object()) {
        return;
    }

    for (const char* key : {"prompt_tokens", "completion_tokens", "total_tokens"}) {
        if (usage.contains(key) && usage.at(key).is_number_integer()) {
            (*payload)[key] = usage.at(key);
        }
    }
}

void append_planner_state_to_trace_payload(nlohmann::json* payload,
                                           const SessionState* session_state,
                                           std::string_view needs_plan_decision,
                                           const PlannerRepairModeSelection& repair_mode_selection) {
    if (payload == nullptr || !payload->is_object() || session_state == nullptr) {
        return;
    }
    (*payload)["plan_state"] = session_state->plan_state;
    (*payload)["plan_repair_attempts"] = session_state->plan_repair_attempts;
    (*payload)["needs_plan_decision"] = std::string(needs_plan_decision);
    (*payload)["planner_repair_effective_mode"] = repair_mode_selection.effective_mode;
    (*payload)["planner_repair_mode_reason"] = repair_mode_selection.reason;
}

void persist_plan_contract_failure(SessionState* session_state,
                                   const nlohmann::json& raw_response,
                                   const std::vector<std::string>& validation_errors,
                                   bool normalization_applied,
                                   std::string_view plan_state) {
    if (session_state == nullptr) {
        return;
    }
    session_state->plan_raw_response = raw_response.is_object() ? raw_response : nlohmann::json::object();
    session_state->plan_validated_artifact = nlohmann::json::object();
    session_state->plan_validation_errors = validation_errors;
    session_state->plan_normalization_applied = normalization_applied;
    set_plan_state(session_state, plan_state);
    touch_session(*session_state);
}

void persist_plan_contract_success(SessionState* session_state,
                                   const nlohmann::json& raw_response,
                                   const nlohmann::json& validated_artifact,
                                   bool normalization_applied) {
    if (session_state == nullptr) {
        return;
    }
    session_state->plan_raw_response = raw_response.is_object() ? raw_response : nlohmann::json::object();
    session_state->plan_validated_artifact =
        validated_artifact.is_object() ? validated_artifact : nlohmann::json::object();
    session_state->plan_validation_errors.clear();
    session_state->plan_normalization_applied = normalization_applied;
    set_plan_state(session_state, kPlanStatePlanReady);
    touch_session(*session_state);
}

bool build_plan_from_validated_artifact(const nlohmann::json& validated_artifact,
                                        int generation,
                                        Plan* out,
                                        std::string* err) {
    if (out == nullptr) {
        if (err != nullptr) {
            *err = "Planner response requires a non-null Plan output.";
        }
        return false;
    }
    if (!validated_artifact.is_object() ||
        !validated_artifact.contains("plan") ||
        !validated_artifact.at("plan").is_object()) {
        if (err != nullptr) {
            *err = "Planner response is missing a top-level object field 'plan'.";
        }
        return false;
    }

    const auto& plan_json = validated_artifact.at("plan");
    if (!plan_json.contains("steps") ||
        !plan_json.at("steps").is_array() ||
        plan_json.at("steps").empty()) {
        if (err != nullptr) {
            *err = "Planner response must include a non-empty array field 'plan.steps'.";
        }
        return false;
    }

    Plan planned;
    planned.generation = generation;
    planned.summary = plan_json.value("summary", "");
    planned.metadata = plan_json.value("metadata", nlohmann::json::object());
    if (!planned.metadata.is_object()) {
        if (err != nullptr) {
            *err = "Planner response field 'plan.metadata' must be an object.";
        }
        return false;
    }

    planned.steps.reserve(plan_json.at("steps").size());
    for (std::size_t index = 0; index < plan_json.at("steps").size(); ++index) {
        PlanStep step;
        try {
            step = plan_json.at("steps").at(index).get<PlanStep>();
        } catch (const std::exception& e) {
            if (err != nullptr) {
                *err = std::string("Planner response field 'plan.steps' is invalid: ") + e.what();
            }
            return false;
        }
        step.status = "pending";
        planned.steps.push_back(std::move(step));
    }

    planned.current_step_index = 0;
    planned.outcome = "in_progress";
    planned.steps.front().status = "in_progress";
    *out = std::move(planned);
    return true;
}

void update_plan_scratchpad(SessionState& session) {
    if (session.plan.outcome == "completed") {
        set_session_scratchpad(session, "plan completed");
        return;
    }
    if (session.plan.outcome == "failed") {
        set_session_scratchpad(session, "plan step failed");
        return;
    }
    if (session.plan.outcome == "planner_reentry_blocked") {
        set_session_scratchpad(session, "planner reentry blocked during execution");
        return;
    }
    if (session.plan.current_step_index >= 0 &&
        session.plan.current_step_index < static_cast<int>(session.plan.steps.size())) {
        const int human_index = session.plan.current_step_index + 1;
        set_session_scratchpad(session,
                               "executing plan step " + std::to_string(human_index) + "/" +
                                   std::to_string(session.plan.steps.size()) + ": " +
                                   session.plan.steps[session.plan.current_step_index].title);
    }
}

void fail_current_plan_step(SessionState& session, const std::string& outcome) {
    if (session.plan.current_step_index >= 0 &&
        session.plan.current_step_index < static_cast<int>(session.plan.steps.size())) {
        session.plan.steps[session.plan.current_step_index].status = "failed";
    }
    session.plan.outcome = outcome;
    touch_session(session);
    update_plan_scratchpad(session);
}

void complete_current_plan_step(SessionState& session) {
    if (session.plan.current_step_index < 0 ||
        session.plan.current_step_index >= static_cast<int>(session.plan.steps.size())) {
        return;
    }

    session.plan.steps[session.plan.current_step_index].status = "completed";
    const int next_index = session.plan.current_step_index + 1;
    if (next_index >= static_cast<int>(session.plan.steps.size())) {
        session.plan.current_step_index = -1;
        session.plan.outcome = "completed";
        touch_session(session);
        update_plan_scratchpad(session);
        return;
    }

    session.plan.current_step_index = next_index;
    session.plan.steps[next_index].status = "in_progress";
    session.plan.outcome = "in_progress";
    touch_session(session);
    update_plan_scratchpad(session);
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

std::string make_plan_repair_prompt(const AgentConfig& config,
                                    const std::vector<std::string>& errors) {
    return build_plan_repair_prompt_for_config(config, errors);
}

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
    std::string needs_plan_decision = "session_not_persisted";
    const PlannerRepairModeSelection planner_repair_mode_selection =
        llm_resolve_planner_repair_mode(config);

    if (session_state != nullptr) {
        if (!messages.is_array()) {
            messages = nlohmann::json::array();
        }
        seed_session_messages_if_empty(*session_state, system_prompt, user_prompt);
        if (session_waiting_on_plan_contract(session_state)) {
            session_state->needs_plan = true;
            needs_plan_decision = "resume_waiting_plan_contract";
        } else if (!session_has_active_plan(session_state) && session_state->plan.steps.empty()) {
            session_state->needs_plan = prompt_needs_plan(user_prompt);
            if (session_state->needs_plan) {
                begin_planner_contract(session_state);
                needs_plan_decision = "heuristic_requires_plan_contract";
            } else {
                clear_planner_contract_fields(session_state);
                set_plan_state(session_state, kPlanStateIdle);
                needs_plan_decision = "heuristic_skips_plan_contract";
            }
            touch_session(*session_state);
        } else if (session_has_active_plan(session_state)) {
            needs_plan_decision = "resume_active_plan_execution";
        } else {
            needs_plan_decision = "preserve_existing_session_plan_state";
        }
    } else {
        if (!system_prompt.empty()) {
            messages.push_back({{"role", "system"}, {"content", system_prompt}});
        }
        messages.push_back({{"role", "user"}, {"content", user_prompt}});
        needs_plan_decision = "stateless_session";
    }

    int turns = session_state != nullptr ? session_state->turn_index : 0;
    int total_tool_calls = session_state != nullptr ? session_state->counters.tool_calls_requested : 0;
    std::unordered_map<std::string, int> failure_retry_counts;
    std::string run_outcome = "completed";
    std::string run_message = "agent run completed";
    nlohmann::json accumulated_usage = nlohmann::json{
        {"prompt_tokens", 0},
        {"completion_tokens", 0},
        {"total_tokens", 0}
    };
    bool saw_usage = false;

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
    run_started_payload["needs_plan_decision"] = needs_plan_decision;
    run_started_payload["planner_repair_effective_mode"] =
        planner_repair_mode_selection.effective_mode;
    run_started_payload["planner_repair_mode_reason"] =
        planner_repair_mode_selection.reason;
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

        const bool planning_turn = session_waiting_on_plan_contract(session_state);
        const bool execution_turn = session_plan_ready_for_execution(session_state);

        // Execute LLM Step
        nlohmann::json response_message;
        try {
            response_message = llm_func(config, messages, tools_registry);
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("LLM Execution failed: ") + e.what());
            std::cerr << "[Agent Error] LLM request failed: " << e.what() << "\n";
            if (session_state != nullptr) {
                if (execution_turn) {
                    fail_current_plan_step(*session_state, "failed");
                }
                set_session_scratchpad(*session_state,
                                       "turn " + std::to_string(turns) + ": llm request failed");
            }
            run_outcome = "llm_request_failed";
            run_message = std::string("llm request failed: ") + e.what();
            break;
        }

        nlohmann::json llm_payload = build_trace_payload(turns);
        if (response_message.contains("usage") && response_message.at("usage").is_object()) {
            const auto& usage = response_message.at("usage");
            apply_usage_to_trace_payload(&llm_payload, usage);
            if (usage.contains("prompt_tokens") && usage.at("prompt_tokens").is_number_integer()) {
                accumulated_usage["prompt_tokens"] =
                    accumulated_usage.value("prompt_tokens", 0) + usage.at("prompt_tokens").get<int>();
                saw_usage = true;
            }
            if (usage.contains("completion_tokens") && usage.at("completion_tokens").is_number_integer()) {
                accumulated_usage["completion_tokens"] =
                    accumulated_usage.value("completion_tokens", 0) + usage.at("completion_tokens").get<int>();
                saw_usage = true;
            }
            if (usage.contains("total_tokens") && usage.at("total_tokens").is_number_integer()) {
                accumulated_usage["total_tokens"] =
                    accumulated_usage.value("total_tokens", 0) + usage.at("total_tokens").get<int>();
                saw_usage = true;
            }
        }
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

        if (planning_turn && session_state != nullptr) {
            const bool repair_turn = session_state->plan_state == kPlanStateAwaitingRepair;
            nlohmann::json planner_response_payload = build_trace_payload(turns);
            planner_response_payload["planner_raw_response"] =
                response_message.is_object() ? response_message : nlohmann::json::object();
            append_planner_state_to_trace_payload(&planner_response_payload,
                                                  session_state,
                                                  needs_plan_decision,
                                                  planner_repair_mode_selection);
            emit_trace("planner_response_received",
                       "planner response received",
                       std::move(planner_response_payload));

            nlohmann::json extracted_contract = nlohmann::json::object();
            std::vector<std::string> validation_errors;
            if (try_extract_plan_contract_candidate(response_message, &extracted_contract, &validation_errors)) {
                validate_plan_contract_candidate(response_message, extracted_contract, &validation_errors);
            }

            bool normalization_applied = false;
            nlohmann::json validated_artifact = extracted_contract;
            if (validation_errors.empty()) {
                if (!normalize_plan_contract(&validated_artifact, &normalization_applied)) {
                    validation_errors.push_back(
                        "Planner response field 'plan.steps' may only use keys shaped like step-1, step-2, ....");
                } else {
                    validate_normalized_plan_contract(validated_artifact, &validation_errors);
                }
            }

            if (validation_errors.empty()) {
                Plan planned;
                std::string plan_err;
                if (!build_plan_from_validated_artifact(validated_artifact,
                                                        session_state->plan.generation,
                                                        &planned,
                                                        &plan_err)) {
                    validation_errors.push_back(plan_err);
                } else {
                    persist_plan_contract_success(session_state,
                                                  response_message,
                                                  validated_artifact,
                                                  normalization_applied);
                    session_state->plan = std::move(planned);
                    session_state->needs_plan = true;
                    touch_session(*session_state);
                    update_plan_scratchpad(*session_state);
                    if (repair_turn) {
                        nlohmann::json repair_succeeded_payload = build_trace_payload(turns);
                        repair_succeeded_payload["planner_raw_response"] = session_state->plan_raw_response;
                        repair_succeeded_payload["plan_validated_artifact"] =
                            session_state->plan_validated_artifact;
                        repair_succeeded_payload["normalization_applied"] =
                            session_state->plan_normalization_applied;
                        append_planner_state_to_trace_payload(&repair_succeeded_payload,
                                                              session_state,
                                                              needs_plan_decision,
                                                              planner_repair_mode_selection);
                        emit_trace("planner_repair_succeeded",
                                   "planner repair succeeded",
                                   std::move(repair_succeeded_payload));
                    }
                    nlohmann::json plan_ready_payload = build_trace_payload(turns);
                    plan_ready_payload["plan_validated_artifact"] =
                        session_state->plan_validated_artifact;
                    plan_ready_payload["normalization_applied"] =
                        session_state->plan_normalization_applied;
                    append_planner_state_to_trace_payload(&plan_ready_payload,
                                                          session_state,
                                                          needs_plan_decision,
                                                          planner_repair_mode_selection);
                    emit_trace("plan_ready", "plan ready", std::move(plan_ready_payload));
                    continue;
                }
            }

            if (repair_turn) {
                const std::string combined_errors = validation_errors.empty()
                    ? std::string("Planner response invalid.")
                    : validation_errors.front();
                LOG_ERROR("Planner response invalid after repair: {}", combined_errors);
                std::cerr << "[Agent Error] Planner response invalid: " << combined_errors << "\n";
                persist_plan_contract_failure(session_state,
                                              response_message,
                                              validation_errors,
                                              normalization_applied,
                                              kPlanStatePlanInvalid);
                nlohmann::json validation_failed_payload = build_trace_payload(turns);
                validation_failed_payload["planner_raw_response"] = session_state->plan_raw_response;
                validation_failed_payload["validation_errors"] = session_state->plan_validation_errors;
                validation_failed_payload["normalization_applied"] =
                    session_state->plan_normalization_applied;
                append_planner_state_to_trace_payload(&validation_failed_payload,
                                                      session_state,
                                                      needs_plan_decision,
                                                      planner_repair_mode_selection);
                emit_trace("planner_validation_failed",
                           "planner validation failed",
                           std::move(validation_failed_payload));
                session_state->plan.outcome = "failed";
                touch_session(*session_state);
                set_session_scratchpad(*session_state, "planner response invalid");
                run_outcome = "planner_response_invalid";
                run_message = "planner response invalid";
                break;
            }

            const std::string combined_errors = validation_errors.empty()
                ? std::string("Planner response invalid.")
                : validation_errors.front();
            LOG_ERROR("Planner response invalid: {}", combined_errors);
            session_state->plan_repair_attempts = 1;
            persist_plan_contract_failure(session_state,
                                          response_message,
                                          validation_errors,
                                          normalization_applied,
                                          kPlanStateAwaitingRepair);
            nlohmann::json validation_failed_payload = build_trace_payload(turns);
            validation_failed_payload["planner_raw_response"] = session_state->plan_raw_response;
            validation_failed_payload["validation_errors"] = session_state->plan_validation_errors;
            validation_failed_payload["normalization_applied"] =
                session_state->plan_normalization_applied;
            append_planner_state_to_trace_payload(&validation_failed_payload,
                                                  session_state,
                                                  needs_plan_decision,
                                                  planner_repair_mode_selection);
            emit_trace("planner_validation_failed",
                       "planner validation failed",
                       std::move(validation_failed_payload));
            messages.push_back(nlohmann::json{
                {"role", "system"},
                {"content", make_plan_repair_prompt(config, validation_errors)}
            });
            touch_session(*session_state);
            set_session_scratchpad(*session_state, "planner response invalid, requesting repair");
            nlohmann::json repair_requested_payload = build_trace_payload(turns);
            repair_requested_payload["validation_errors"] = session_state->plan_validation_errors;
            append_planner_state_to_trace_payload(&repair_requested_payload,
                                                  session_state,
                                                  needs_plan_decision,
                                                  planner_repair_mode_selection);
            emit_trace("planner_repair_requested",
                       "planner repair requested",
                       std::move(repair_requested_payload));
            continue;
        }

        if (execution_turn && response_contains_plan_contract(response_message) && session_state != nullptr) {
            LOG_ERROR("Planner reentry blocked during execution.");
            std::cerr << "[Agent Error] Planner reentry blocked during execution.\n";
            fail_current_plan_step(*session_state, "planner_reentry_blocked");
            run_outcome = "planner_reentry_blocked";
            run_message = "planner reentry blocked during execution";
            break;
        }

        if (!response_message.contains("tool_calls") || response_message["tool_calls"].empty()) {
            LOG_INFO("No tool calls. Agent loop complete.");
            std::cout << "[Agent Complete] " << response_message.value("content", "") << "\n";
            if (session_state != nullptr) {
                if (execution_turn) {
                    complete_current_plan_step(*session_state);
                    if (!session_has_active_plan(session_state) &&
                        !response_message.value("content", "").empty()) {
                        set_session_scratchpad(*session_state, response_message.value("content", ""));
                    }
                    if (session_has_active_plan(session_state)) {
                        continue;
                    }
                }
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
        bool recoverable_failure_encountered = false;

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
                    if (execution_turn) {
                        fail_current_plan_step(*session_state, "failed");
                    }
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
                    if (execution_turn) {
                        fail_current_plan_step(*session_state, "failed");
                    }
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
            recoverable_failure_encountered = true;
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

        if (execution_turn && session_state != nullptr &&
            !state_contaminated && !recoverable_failure_encountered) {
            complete_current_plan_step(*session_state);
            if (session_has_active_plan(session_state)) {
                continue;
            }
            if (session_state->plan.outcome == "completed") {
                run_outcome = "completed";
                run_message = "agent run completed";
                break;
            }
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
    run_finished_payload["final_plan_state"] =
        session_state != nullptr ? session_state->plan_state : std::string(kPlanStateIdle);
    run_finished_payload["needs_plan_decision"] = needs_plan_decision;
    run_finished_payload["planner_repair_effective_mode"] =
        planner_repair_mode_selection.effective_mode;
    run_finished_payload["planner_repair_mode_reason"] =
        planner_repair_mode_selection.reason;
    run_finished_payload["turn_count"] = turns;
    run_finished_payload["tool_calls_requested"] = total_tool_calls;
    if (saw_usage) {
        apply_usage_to_trace_payload(&run_finished_payload, accumulated_usage);
    }
    emit_trace("run_finished", run_message, std::move(run_finished_payload));
}
