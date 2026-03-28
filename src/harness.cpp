#include "harness.hpp"

#include "subprocess.hpp"
#include "trace.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

std::atomic<std::uint64_t> g_run_counter{0};

[[noreturn]] void throw_invalid_harness(std::string message) {
    throw std::invalid_argument(std::move(message));
}

bool read_string_field(const nlohmann::json& json_value,
                       const char* key,
                       std::string* out,
                       bool required = false) {
    if (!json_value.contains(key)) {
        return !required;
    }
    if (!json_value.at(key).is_string()) {
        return false;
    }
    if (out != nullptr) {
        *out = json_value.at(key).get<std::string>();
    }
    return true;
}

bool read_int_field(const nlohmann::json& json_value, const char* key, int* out, bool required = false) {
    if (!json_value.contains(key)) {
        return !required;
    }
    if (!json_value.at(key).is_number_integer()) {
        return false;
    }
    if (out != nullptr) {
        *out = json_value.at(key).get<int>();
    }
    return true;
}

std::string make_run_id() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const auto counter = g_run_counter.fetch_add(1, std::memory_order_relaxed);
    return "run-" + std::to_string(millis) + "-" + std::to_string(counter);
}

bool is_safe_path_component(std::string_view value) {
    if (value.empty() || value == "." || value == "..") {
        return false;
    }
    if (value.find('/') != std::string_view::npos || value.find('\\') != std::string_view::npos) {
        return false;
    }

    const fs::path path(value);
    if (path.empty() || path.is_absolute() || path.has_parent_path()) {
        return false;
    }

    const fs::path normalized = path.lexically_normal();
    return !normalized.empty() &&
           normalized == path &&
           normalized.native() != "." &&
           normalized.native() != "..";
}

void validate_safe_path_component_or_throw(std::string_view field_name, std::string_view value) {
    if (!is_safe_path_component(value)) {
        throw_invalid_harness(std::string(field_name) + " must be a single safe path component.");
    }
}

void ensure_directory(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("Could not create directory '" + path.string() + "': " + ec.message());
    }
}

void remove_path_if_exists(const fs::path& path) {
    std::error_code exists_ec;
    const bool exists = fs::exists(path, exists_ec);
    if (exists_ec) {
        throw std::runtime_error("Could not inspect path '" + path.string() + "': " + exists_ec.message());
    }
    if (!exists) {
        return;
    }

    std::error_code remove_ec;
    fs::remove_all(path, remove_ec);
    if (remove_ec) {
        throw std::runtime_error("Could not remove existing path '" + path.string() + "': " + remove_ec.message());
    }
}

void write_text_file(const fs::path& path, const std::string& content) {
    ensure_directory(path.parent_path());
    const auto temp_suffix = g_run_counter.fetch_add(1, std::memory_order_relaxed);
    const fs::path temp_path = path.string() + ".tmp-" + std::to_string(temp_suffix);

    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Could not open temp file '" + temp_path.string() + "' for writing.");
    }
    output << content;
    output.flush();
    if (!output) {
        output.close();
        std::error_code cleanup_ec;
        fs::remove(temp_path, cleanup_ec);
        throw std::runtime_error("Failed while writing file '" + temp_path.string() + "'.");
    }
    output.close();

    std::error_code rename_ec;
    fs::rename(temp_path, path, rename_ec);
    if (rename_ec) {
        std::error_code remove_ec;
        fs::remove(path, remove_ec);
        rename_ec.clear();
        fs::rename(temp_path, path, rename_ec);
    }
    if (rename_ec) {
        std::error_code cleanup_ec;
        fs::remove(temp_path, cleanup_ec);
        throw std::runtime_error("Could not move temp file into place for '" + path.string() +
                                 "': " + rename_ec.message());
    }
}

void write_json_file(const fs::path& path, const nlohmann::json& json_value) {
    write_text_file(path, json_value.dump(2) + "\n");
}

std::string relative_to(const fs::path& base, const fs::path& path) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, base, ec);
    return ec ? path.lexically_normal().generic_string() : relative.generic_string();
}

void validate_json_object_or_throw(const nlohmann::json& value, const char* field_name) {
    if (!value.is_object()) {
        throw_invalid_harness(std::string(field_name) + " must be a JSON object.");
    }
}

void validate_step_result_or_throw(const StepResult& result) {
    if (!is_valid_harness_step_status(result.status)) {
        throw_invalid_harness("StepResult status must be one of: success, retryable_failure, terminal_failure.");
    }
    validate_json_object_or_throw(result.metadata, "StepResult metadata");
    validate_json_object_or_throw(result.artifacts, "StepResult artifacts");
}

void validate_acceptance_or_throw(const AcceptanceDecision& decision) {
    if (!is_valid_acceptance_status(decision.status)) {
        throw_invalid_harness(
            "AcceptanceDecision status must be one of: success, retryable_failure, terminal_failure, not_checked.");
    }
    validate_json_object_or_throw(decision.details, "AcceptanceDecision details");
}

void validate_verification_or_throw(const VerificationResult& result) {
    if (!is_valid_verification_status(result.status)) {
        throw_invalid_harness("VerificationResult status must be one of: passed, failed, not_started.");
    }
    validate_json_object_or_throw(result.details, "VerificationResult details");
    validate_acceptance_or_throw(result.acceptance);
}

void validate_run_artifact_or_throw(const RunArtifact& artifact) {
    if (!is_valid_verification_status(artifact.verification_status)) {
        throw_invalid_harness("RunArtifact verification_status must be one of: passed, failed, not_started.");
    }
    validate_json_object_or_throw(artifact.verification_details, "RunArtifact verification_details");
    validate_json_object_or_throw(artifact.metadata, "RunArtifact metadata");
    for (const StepResult& result : artifact.step_results) {
        validate_step_result_or_throw(result);
    }
    validate_acceptance_or_throw(artifact.acceptance);
}

std::string combine_summaries(const std::string& first, const std::string& second) {
    if (first.empty()) {
        return second;
    }
    if (second.empty() || second == first) {
        return first;
    }
    return first + " " + second;
}

int acceptance_rank(std::string_view status) {
    if (status == "not_checked") {
        return -1;
    }
    if (status == "success") {
        return 0;
    }
    if (status == "retryable_failure") {
        return 1;
    }
    if (status == "terminal_failure") {
        return 2;
    }
    return 3;
}

AcceptanceDecision merge_acceptance(const AcceptanceDecision& base, const AcceptanceDecision& overlay) {
    if (acceptance_rank(overlay.status) > acceptance_rank(base.status)) {
        AcceptanceDecision merged = overlay;
        merged.summary = combine_summaries(base.summary, overlay.summary);
        merged.details = nlohmann::json{
            {"base", base.details},
            {"overlay", overlay.details},
        };
        return merged;
    }

    AcceptanceDecision merged = base;
    merged.summary = combine_summaries(base.summary, overlay.summary);
    merged.details = nlohmann::json{
        {"base", base.details},
        {"overlay", overlay.details},
    };
    return merged;
}

StepResult make_step_result(const ScenarioStepDefinition& step,
                            const SubprocessResult& subprocess_result,
                            const std::string& started_at,
                            const std::string& finished_at,
                            const fs::path& run_dir,
                            const fs::path& command_path,
                            const fs::path& stdout_path,
                            const fs::path& stderr_path,
                            const fs::path& result_path) {
    StepResult result;
    result.step_id = step.id;
    result.started_at = started_at;
    result.finished_at = finished_at;
    result.exit_code = subprocess_result.exit_code;
    result.metadata = nlohmann::json{
        {"command", step.command},
        {"timeout_ms", step.timeout_ms},
        {"stdout_bytes", subprocess_result.stdout_bytes},
        {"stderr_bytes", subprocess_result.stderr_bytes},
        {"timed_out", subprocess_result.timed_out},
        {"truncated", subprocess_result.truncated},
        {"error", subprocess_result.error},
    };
    result.artifacts = nlohmann::json{
        {"command", relative_to(run_dir, command_path)},
        {"stdout", relative_to(run_dir, stdout_path)},
        {"stderr", relative_to(run_dir, stderr_path)},
        {"result", relative_to(run_dir, result_path)},
    };

    if (subprocess_result.ok) {
        result.status = "success";
        result.summary = "Step completed successfully.";
        return result;
    }

    if (subprocess_result.timed_out || subprocess_result.truncated || !subprocess_result.error.empty()) {
        result.status = "retryable_failure";
        result.summary = subprocess_result.error.empty()
                             ? "Step failed in a retryable execution state."
                             : subprocess_result.error;
        return result;
    }

    result.status = "terminal_failure";
    result.summary =
        "Step command exited non-zero with exit code " + std::to_string(subprocess_result.exit_code) + ".";
    return result;
}

AcceptanceDecision base_acceptance_from_steps(const std::vector<StepResult>& results) {
    for (const StepResult& result : results) {
        if (result.status == "success") {
            continue;
        }
        return {
            result.status,
            "Step '" + result.step_id + "' did not complete successfully.",
            nlohmann::json{
                {"step_id", result.step_id},
                {"step_status", result.status},
                {"summary", result.summary},
                {"exit_code", result.exit_code},
            },
        };
    }

    return {
        "success",
        "All planned steps completed successfully.",
        nlohmann::json::object(),
    };
}

VerificationResult verification_from_execution_failure(const AcceptanceDecision& execution_acceptance) {
    VerificationResult verification;
    verification.status = "failed";
    verification.summary = execution_acceptance.summary;
    verification.details = execution_acceptance.details;
    verification.acceptance = execution_acceptance;
    return verification;
}

nlohmann::json subprocess_result_to_json(const SubprocessResult& result) {
    return nlohmann::json{
        {"ok", result.ok},
        {"exit_code", result.exit_code},
        {"stdout_bytes", result.stdout_bytes},
        {"stderr_bytes", result.stderr_bytes},
        {"timed_out", result.timed_out},
        {"truncated", result.truncated},
        {"error", result.error},
    };
}

void write_plan_file(const fs::path& path,
                     const std::string& run_id,
                     const std::string& scenario,
                     const Plan& plan) {
    write_json_file(path,
                    nlohmann::json{
                        {"schema_version", 1},
                        {"run_id", run_id},
                        {"scenario", scenario},
                        {"updated_at", state_now_timestamp()},
                        {"plan", plan},
                    });
}

void write_run_file(const fs::path& path, const RunArtifact& artifact) {
    write_json_file(path, nlohmann::json(artifact));
}

void emit_trace(TraceSink* sink,
                const std::string& kind,
                const std::string& message,
                const nlohmann::json& payload) {
    const TraceWriteResult result =
        sink->write(make_trace_event(kind, message, payload.is_object() ? payload : nlohmann::json::object()));
    if (!result.ok) {
        throw std::runtime_error("Could not write trace event '" + kind + "': " + result.error);
    }
}

nlohmann::json build_trace_payload(const RunArtifact& run, const nlohmann::json& extra = nlohmann::json::object()) {
    nlohmann::json payload = {
        {"run_id", run.run_id},
        {"scenario", run.scenario},
        {"executor", run.executor},
        {"verifier", run.verifier},
    };
    if (extra.is_object()) {
        payload.update(extra);
    }
    return payload;
}

}  // namespace

bool is_valid_harness_step_status(std::string_view status) {
    static constexpr std::array<std::string_view, 3> kAllowed{
        "success",
        "retryable_failure",
        "terminal_failure",
    };
    return std::find(kAllowed.begin(), kAllowed.end(), status) != kAllowed.end();
}

bool is_valid_acceptance_status(std::string_view status) {
    static constexpr std::array<std::string_view, 4> kAllowed{
        "success",
        "retryable_failure",
        "terminal_failure",
        "not_checked",
    };
    return std::find(kAllowed.begin(), kAllowed.end(), status) != kAllowed.end();
}

bool is_valid_verification_status(std::string_view status) {
    static constexpr std::array<std::string_view, 3> kAllowed{
        "passed",
        "failed",
        "not_started",
    };
    return std::find(kAllowed.begin(), kAllowed.end(), status) != kAllowed.end();
}

void to_json(nlohmann::json& json_value, const StepResult& result) {
    validate_step_result_or_throw(result);
    json_value = nlohmann::json{
        {"step_id", result.step_id},
        {"status", result.status},
        {"started_at", result.started_at},
        {"finished_at", result.finished_at},
        {"summary", result.summary},
        {"exit_code", result.exit_code},
        {"metadata", result.metadata},
        {"artifacts", result.artifacts},
    };
}

void from_json(const nlohmann::json& json_value, StepResult& result) {
    if (!json_value.is_object()) {
        throw std::invalid_argument("StepResult must be a JSON object.");
    }

    StepResult parsed;
    if (!read_string_field(json_value, "step_id", &parsed.step_id, true) ||
        !read_string_field(json_value, "status", &parsed.status, true) ||
        !read_string_field(json_value, "started_at", &parsed.started_at) ||
        !read_string_field(json_value, "finished_at", &parsed.finished_at) ||
        !read_string_field(json_value, "summary", &parsed.summary)) {
        throw std::invalid_argument("StepResult contains an invalid string field.");
    }
    if (!is_valid_harness_step_status(parsed.status)) {
        throw std::invalid_argument(
            "StepResult status must be one of: success, retryable_failure, terminal_failure.");
    }
    if (!read_int_field(json_value, "exit_code", &parsed.exit_code)) {
        throw std::invalid_argument("StepResult exit_code must be an integer.");
    }
    if (json_value.contains("metadata")) {
        if (!json_value.at("metadata").is_object()) {
            throw std::invalid_argument("StepResult metadata must be a JSON object.");
        }
        parsed.metadata = json_value.at("metadata");
    }
    if (json_value.contains("artifacts")) {
        if (!json_value.at("artifacts").is_object()) {
            throw std::invalid_argument("StepResult artifacts must be a JSON object.");
        }
        parsed.artifacts = json_value.at("artifacts");
    }

    result = std::move(parsed);
}

void to_json(nlohmann::json& json_value, const AcceptanceDecision& decision) {
    validate_acceptance_or_throw(decision);
    json_value = nlohmann::json{
        {"status", decision.status},
        {"summary", decision.summary},
        {"details", decision.details},
    };
}

void from_json(const nlohmann::json& json_value, AcceptanceDecision& decision) {
    if (!json_value.is_object()) {
        throw std::invalid_argument("AcceptanceDecision must be a JSON object.");
    }

    AcceptanceDecision parsed;
    if (!read_string_field(json_value, "status", &parsed.status, true) ||
        !read_string_field(json_value, "summary", &parsed.summary)) {
        throw std::invalid_argument("AcceptanceDecision contains an invalid string field.");
    }
    if (!is_valid_acceptance_status(parsed.status)) {
        throw std::invalid_argument(
            "AcceptanceDecision status must be one of: success, retryable_failure, terminal_failure, not_checked.");
    }
    if (json_value.contains("details")) {
        if (!json_value.at("details").is_object()) {
            throw std::invalid_argument("AcceptanceDecision details must be a JSON object.");
        }
        parsed.details = json_value.at("details");
    }

    decision = std::move(parsed);
}

void to_json(nlohmann::json& json_value, const VerificationResult& result) {
    validate_verification_or_throw(result);
    json_value = nlohmann::json{
        {"status", result.status},
        {"summary", result.summary},
        {"details", result.details},
        {"acceptance", result.acceptance},
    };
}

void from_json(const nlohmann::json& json_value, VerificationResult& result) {
    if (!json_value.is_object()) {
        throw std::invalid_argument("VerificationResult must be a JSON object.");
    }

    VerificationResult parsed;
    if (!read_string_field(json_value, "status", &parsed.status, true) ||
        !read_string_field(json_value, "summary", &parsed.summary)) {
        throw std::invalid_argument("VerificationResult contains an invalid string field.");
    }
    if (!is_valid_verification_status(parsed.status)) {
        throw std::invalid_argument("VerificationResult status must be one of: passed, failed, not_started.");
    }
    if (json_value.contains("details")) {
        if (!json_value.at("details").is_object()) {
            throw std::invalid_argument("VerificationResult details must be a JSON object.");
        }
        parsed.details = json_value.at("details");
    }
    if (json_value.contains("acceptance")) {
        parsed.acceptance = json_value.at("acceptance").get<AcceptanceDecision>();
    }

    result = std::move(parsed);
}

void to_json(nlohmann::json& json_value, const RunArtifact& artifact) {
    validate_run_artifact_or_throw(artifact);
    json_value = nlohmann::json{
        {"schema_version", artifact.schema_version},
        {"run_id", artifact.run_id},
        {"scenario", artifact.scenario},
        {"status", artifact.status},
        {"started_at", artifact.started_at},
        {"finished_at", artifact.finished_at},
        {"executor", artifact.executor},
        {"verifier", artifact.verifier},
        {"verification_status", artifact.verification_status},
        {"verification_summary", artifact.verification_summary},
        {"verification_details", artifact.verification_details},
        {"plan_path", artifact.plan_path},
        {"trace_path", artifact.trace_path},
        {"artifacts_dir", artifact.artifacts_dir},
        {"step_results", artifact.step_results},
        {"acceptance", artifact.acceptance},
        {"metadata", artifact.metadata},
    };
}

void from_json(const nlohmann::json& json_value, RunArtifact& artifact) {
    if (!json_value.is_object()) {
        throw std::invalid_argument("RunArtifact must be a JSON object.");
    }

    RunArtifact parsed;
    if (!read_int_field(json_value, "schema_version", &parsed.schema_version) ||
        !read_string_field(json_value, "run_id", &parsed.run_id, true) ||
        !read_string_field(json_value, "scenario", &parsed.scenario, true) ||
        !read_string_field(json_value, "status", &parsed.status, true) ||
        !read_string_field(json_value, "started_at", &parsed.started_at) ||
        !read_string_field(json_value, "finished_at", &parsed.finished_at) ||
        !read_string_field(json_value, "executor", &parsed.executor) ||
        !read_string_field(json_value, "verifier", &parsed.verifier) ||
        !read_string_field(json_value, "verification_status", &parsed.verification_status, true) ||
        !read_string_field(json_value, "verification_summary", &parsed.verification_summary) ||
        !read_string_field(json_value, "plan_path", &parsed.plan_path) ||
        !read_string_field(json_value, "trace_path", &parsed.trace_path) ||
        !read_string_field(json_value, "artifacts_dir", &parsed.artifacts_dir)) {
        throw std::invalid_argument("RunArtifact contains an invalid string or integer field.");
    }
    if (!is_valid_verification_status(parsed.verification_status)) {
        throw std::invalid_argument("RunArtifact verification_status must be one of: passed, failed, not_started.");
    }
    if (json_value.contains("verification_details")) {
        if (!json_value.at("verification_details").is_object()) {
            throw std::invalid_argument("RunArtifact verification_details must be a JSON object.");
        }
        parsed.verification_details = json_value.at("verification_details");
    }
    if (json_value.contains("step_results")) {
        if (!json_value.at("step_results").is_array()) {
            throw std::invalid_argument("RunArtifact step_results must be an array.");
        }
        parsed.step_results = json_value.at("step_results").get<std::vector<StepResult>>();
    }
    if (json_value.contains("acceptance")) {
        parsed.acceptance = json_value.at("acceptance").get<AcceptanceDecision>();
    }
    if (json_value.contains("metadata")) {
        if (!json_value.at("metadata").is_object()) {
            throw std::invalid_argument("RunArtifact metadata must be a JSON object.");
        }
        parsed.metadata = json_value.at("metadata");
    }

    artifact = std::move(parsed);
}

Plan make_plan_from_definition(const ScenarioPlanDefinition& definition) {
    if (!definition.metadata.is_object()) {
        throw std::invalid_argument("ScenarioPlanDefinition metadata must be a JSON object.");
    }

    Plan plan;
    plan.generation = 1;
    plan.summary = definition.summary;
    plan.metadata = definition.metadata;
    plan.current_step_index = -1;
    plan.outcome = "pending";

    plan.steps.reserve(definition.steps.size());
    std::unordered_set<std::string> seen_step_ids;
    for (const ScenarioStepDefinition& step_definition : definition.steps) {
        if (!step_definition.metadata.is_object()) {
            throw std::invalid_argument("ScenarioStepDefinition metadata must be a JSON object.");
        }
        validate_safe_path_component_or_throw("ScenarioStepDefinition id", step_definition.id);
        if (!seen_step_ids.insert(step_definition.id).second) {
            throw std::invalid_argument("ScenarioStepDefinition ids must be unique.");
        }
        plan.steps.push_back(PlanStep{
            .id = step_definition.id,
            .title = step_definition.title,
            .status = "pending",
            .detail = step_definition.detail,
            .metadata = step_definition.metadata,
        });
    }
    return plan;
}

HarnessRunResult run_harness(const ScenarioAdapter& scenario, const HarnessRunOptions& options) {
    if (options.workspace_root.empty()) {
        throw std::invalid_argument("HarnessRunOptions workspace_root must not be empty.");
    }

    fs::path workspace_root = options.workspace_root;
    if (!workspace_root.is_absolute()) {
        workspace_root = fs::absolute(workspace_root);
    }
    std::error_code workspace_ec;
    if (!fs::exists(workspace_root, workspace_ec)) {
        ensure_directory(workspace_root);
    }
    workspace_root = fs::weakly_canonical(workspace_root);

    fs::path output_root = options.output_root.empty() ? workspace_root / "tmp" / "harness-runs"
                                                       : options.output_root;
    if (!output_root.is_absolute()) {
        output_root = workspace_root / output_root;
    }
    output_root = output_root.lexically_normal();

    const std::string run_id = options.run_id.value_or(make_run_id());
    validate_safe_path_component_or_throw("HarnessRunOptions run_id", run_id);
    const fs::path run_dir = output_root / run_id;
    const fs::path artifacts_dir = run_dir / "artifacts";
    const fs::path step_artifacts_dir = artifacts_dir / "steps";
    const fs::path scenario_artifacts_dir = artifacts_dir / "scenario";
    const fs::path run_json_path = run_dir / "run.json";
    const fs::path plan_json_path = run_dir / "plan.json";
    const fs::path trace_jsonl_path = run_dir / "trace.jsonl";
    const fs::path scenario_index_path = scenario_artifacts_dir / "index.json";

    const ScenarioPlanDefinition definition = scenario.definition();
    Plan plan = make_plan_from_definition(definition);

    remove_path_if_exists(run_dir);
    ensure_directory(step_artifacts_dir);
    ensure_directory(scenario_artifacts_dir);

    RunArtifact run;
    run.run_id = run_id;
    run.scenario = scenario.id();
    run.status = "running";
    run.started_at = state_now_timestamp();
    run.executor = scenario.executor_name();
    run.verifier = scenario.verifier_name();
    run.verification_status = "not_started";
    run.plan_path = "plan.json";
    run.trace_path = "trace.jsonl";
    run.artifacts_dir = "artifacts";
    run.acceptance = {
        "not_checked",
        "Acceptance has not been evaluated yet.",
        nlohmann::json::object(),
    };
    run.metadata = nlohmann::json{
        {"workspace_root", workspace_root.generic_string()},
        {"run_dir", run_dir.generic_string()},
        {"output_root", output_root.generic_string()},
        {"scenario_description", scenario.description()},
        {"scenario_artifact_index_path", relative_to(run_dir, scenario_index_path)},
    };

    JsonlTraceSink trace_sink(trace_jsonl_path.string());
    std::string trace_error;
    if (!trace_sink.prepare(&trace_error)) {
        throw std::runtime_error(trace_error);
    }

    write_plan_file(plan_json_path, run_id, run.scenario, plan);
    write_run_file(run_json_path, run);

    emit_trace(&trace_sink,
               "run_started",
               "Harness run started.",
               build_trace_payload(run,
                                   {{"run_dir", run.metadata.value("run_dir", "")},
                                    {"workspace_root", run.metadata.value("workspace_root", "")}}));
    emit_trace(&trace_sink,
               "plan_created",
               "Initial plan created.",
               build_trace_payload(run,
                                   {{"plan_path", run.plan_path},
                                    {"plan_summary", plan.summary},
                                    {"step_count", plan.steps.size()}}));

    bool all_steps_succeeded = true;
    for (std::size_t index = 0; index < definition.steps.size(); ++index) {
        const ScenarioStepDefinition& step = definition.steps[index];
        PlanStep& plan_step = plan.steps[index];
        plan.current_step_index = static_cast<int>(index);
        plan.outcome = "in_progress";
        plan_step.status = "in_progress";
        write_plan_file(plan_json_path, run_id, run.scenario, plan);

        const fs::path step_dir = step_artifacts_dir / step.id;
        ensure_directory(step_dir);

        emit_trace(&trace_sink,
                   "step_started",
                   "Plan step started.",
                   build_trace_payload(run,
                                       {{"step_id", step.id},
                                        {"step_title", step.title},
                                        {"step_index", index},
                                        {"artifact_dir", relative_to(run_dir, step_dir)}}));

        const fs::path command_path = step_dir / "command.json";
        const fs::path stdout_path = step_dir / "stdout.txt";
        const fs::path stderr_path = step_dir / "stderr.txt";
        const fs::path result_path = step_dir / "result.json";
        write_json_file(command_path,
                        nlohmann::json{
                            {"step_id", step.id},
                            {"command", step.command},
                            {"timeout_ms", step.timeout_ms},
                            {"metadata", step.metadata},
                        });

        emit_trace(&trace_sink,
                   "tool_invoked",
                   "Scenario command invoked.",
                   build_trace_payload(run,
                                       {{"step_id", step.id},
                                        {"tool_name", "subprocess"},
                                        {"command", step.command},
                                        {"working_directory", workspace_root.generic_string()},
                                        {"command_artifact", relative_to(run_dir, command_path)}}));

        const std::string started_at = state_now_timestamp();
        const SubprocessResult subprocess_result =
            run_subprocess_capture(workspace_root.string(), step.command, step.timeout_ms);
        const std::string finished_at = state_now_timestamp();

        write_text_file(stdout_path, subprocess_result.stdout_text);
        write_text_file(stderr_path, subprocess_result.stderr_text);
        write_json_file(result_path, subprocess_result_to_json(subprocess_result));

        StepResult step_result =
            make_step_result(step, subprocess_result, started_at, finished_at, run_dir, command_path, stdout_path,
                             stderr_path, result_path);

        emit_trace(&trace_sink,
                   "tool_finished",
                   "Scenario command finished.",
                   build_trace_payload(run,
                                       {{"step_id", step.id},
                                        {"tool_name", "subprocess"},
                                        {"command", step.command},
                                        {"exit_code", subprocess_result.exit_code},
                                        {"timed_out", subprocess_result.timed_out},
                                        {"truncated", subprocess_result.truncated},
                                        {"step_status", step_result.status},
                                        {"stdout_artifact", relative_to(run_dir, stdout_path)},
                                        {"stderr_artifact", relative_to(run_dir, stderr_path)},
                                        {"result_artifact", relative_to(run_dir, result_path)}}));

        run.step_results.push_back(step_result);
        plan_step.status = step_result.status == "success" ? "completed" : "failed";
        if (step_result.status != "success") {
            all_steps_succeeded = false;
            plan.outcome = "failed";
        }

        write_plan_file(plan_json_path, run_id, run.scenario, plan);
        write_run_file(run_json_path, run);

        if (step_result.status != "success") {
            break;
        }
    }

    if (all_steps_succeeded) {
        plan.outcome = "completed";
        if (!plan.steps.empty()) {
            plan.current_step_index = static_cast<int>(plan.steps.size()) - 1;
        }
        write_plan_file(plan_json_path, run_id, run.scenario, plan);
    }

    const ScenarioContext context{
        workspace_root,
        run_dir,
        artifacts_dir,
        scenario_artifacts_dir,
        step_artifacts_dir,
        run_id,
        run.scenario,
        run.step_results,
    };

    std::string archive_error_message;
    nlohmann::json artifact_index = nlohmann::json::object();
    bool archived = false;
    try {
        archived = scenario.archive_artifacts(context, &artifact_index, &archive_error_message);
    } catch (const std::exception& e) {
        archived = false;
        archive_error_message = e.what();
    }
    if (!archived) {
        artifact_index = nlohmann::json{
            {"status", "archive_failed"},
            {"error", archive_error_message},
        };
    }
    write_json_file(scenario_index_path, artifact_index);

    emit_trace(&trace_sink,
               "verification_started",
               "Verification phase started.",
               build_trace_payload(run,
                                   {{"artifact_index", relative_to(run_dir, scenario_index_path)},
                                    {"archive_ok", archived}}));

    VerificationResult verification;
    const AcceptanceDecision execution_acceptance = base_acceptance_from_steps(run.step_results);
    if (!archived) {
        verification.status = "failed";
        verification.summary = archive_error_message.empty() ? "Scenario artifact archival failed." : archive_error_message;
        verification.details = nlohmann::json{
            {"artifact_index_path", relative_to(run_dir, scenario_index_path)},
            {"archive_ok", false},
            {"archive_error", archive_error_message},
        };
        verification.acceptance = {
            "terminal_failure",
            verification.summary,
            verification.details,
        };
    } else if (execution_acceptance.status != "success") {
        verification = verification_from_execution_failure(execution_acceptance);
        std::string verify_error;
        VerificationResult scenario_verification;
        try {
            scenario_verification = scenario.verify(context, artifact_index, &verify_error);
        } catch (const std::exception& e) {
            verify_error = e.what();
        }
        if (!verify_error.empty()) {
            verification.summary = combine_summaries(verification.summary, verify_error);
            verification.details["scenario_verify_error"] = verify_error;
            verification.acceptance.details["scenario_verify_error"] = verify_error;
        } else {
            verification.summary = combine_summaries(verification.summary, scenario_verification.summary);
            verification.details["scenario"] = scenario_verification.details;
            verification.acceptance = merge_acceptance(verification.acceptance, scenario_verification.acceptance);
        }
    } else {
        std::string verify_error;
        try {
            verification = scenario.verify(context, artifact_index, &verify_error);
        } catch (const std::exception& e) {
            verify_error = e.what();
        }
        if (!verify_error.empty()) {
            verification.status = "failed";
            verification.summary = verify_error;
            verification.details = nlohmann::json{
                {"artifact_index_path", relative_to(run_dir, scenario_index_path)},
                {"verify_error", verify_error},
            };
            verification.acceptance = {
                "terminal_failure",
                verify_error,
                verification.details,
            };
        }
    }

    run.verification_status = verification.status;
    run.verification_summary = verification.summary;
    run.verification_details = verification.details;

    if (verification.status == "passed") {
        emit_trace(&trace_sink,
                   "verification_passed",
                   "Verification passed.",
                   build_trace_payload(run,
                                       {{"verification_summary", verification.summary},
                                        {"artifact_index", relative_to(run_dir, scenario_index_path)}}));
    } else {
        emit_trace(&trace_sink,
                   "verification_failed",
                   "Verification failed.",
                   build_trace_payload(run,
                                       {{"verification_summary", verification.summary},
                                        {"artifact_index", relative_to(run_dir, scenario_index_path)}}));
    }

    AcceptanceDecision acceptance = merge_acceptance(execution_acceptance, verification.acceptance);
    if (verification.status != "passed" && acceptance.status == "success") {
        acceptance.status = "terminal_failure";
        acceptance.summary = combine_summaries(acceptance.summary, verification.summary);
        acceptance.details["verification"] = verification.details;
    }
    run.acceptance = acceptance;

    emit_trace(&trace_sink,
               "acceptance_checked",
               "Acceptance checked.",
               build_trace_payload(run,
                                   {{"acceptance_status", acceptance.status},
                                    {"acceptance_summary", acceptance.summary},
                                    {"verification_status", verification.status}}));

    run.status = verification.status == "passed" && acceptance.status == "success" ? "succeeded" : "failed";
    run.finished_at = state_now_timestamp();
    write_run_file(run_json_path, run);

    emit_trace(&trace_sink,
               run.status == "succeeded" ? "run_succeeded" : "run_failed",
               run.status == "succeeded" ? "Harness run succeeded." : "Harness run failed.",
               build_trace_payload(run,
                                   {{"run_status", run.status},
                                    {"verification_status", verification.status},
                                    {"acceptance_status", acceptance.status},
                                    {"run_json_path", relative_to(run_dir, run_json_path)},
                                    {"plan_path", relative_to(run_dir, plan_json_path)},
                                    {"trace_path", relative_to(run_dir, trace_jsonl_path)}}));

    return {
        run,
        run_dir,
        run_json_path,
        plan_json_path,
        trace_jsonl_path,
    };
}
