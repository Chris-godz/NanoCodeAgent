#pragma once

#include "state.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

struct ScenarioStepDefinition {
    std::string id;
    std::string title;
    std::string detail;
    std::string command;
    int timeout_ms = 3600000;
    nlohmann::json metadata = nlohmann::json::object();
};

struct ScenarioPlanDefinition {
    std::string summary;
    std::vector<ScenarioStepDefinition> steps;
    nlohmann::json metadata = nlohmann::json::object();
};

struct StepResult {
    std::string step_id;
    std::string status = "success";
    std::string started_at;
    std::string finished_at;
    std::string summary;
    int exit_code = -1;
    nlohmann::json metadata = nlohmann::json::object();
    nlohmann::json artifacts = nlohmann::json::object();
};

struct AcceptanceDecision {
    std::string status = "success";
    std::string summary;
    nlohmann::json details = nlohmann::json::object();
};

struct VerificationResult {
    std::string status = "failed";
    std::string summary;
    nlohmann::json details = nlohmann::json::object();
    AcceptanceDecision acceptance;
};

struct RunArtifact {
    int schema_version = 1;
    std::string run_id;
    std::string scenario;
    std::string status = "running";
    std::string started_at;
    std::string finished_at;
    std::string executor;
    std::string verifier;
    std::string verification_status = "not_started";
    std::string verification_summary;
    nlohmann::json verification_details = nlohmann::json::object();
    std::string plan_path = "plan.json";
    std::string trace_path = "trace.jsonl";
    std::string artifacts_dir = "artifacts";
    std::vector<StepResult> step_results;
    AcceptanceDecision acceptance;
    nlohmann::json metadata = nlohmann::json::object();
};

struct ScenarioContext {
    std::filesystem::path workspace_root;
    std::filesystem::path run_dir;
    std::filesystem::path artifacts_dir;
    std::filesystem::path scenario_artifacts_dir;
    std::filesystem::path step_artifacts_dir;
    std::string run_id;
    std::string scenario_id;
    std::vector<StepResult> step_results;
};

struct HarnessRunOptions {
    std::filesystem::path workspace_root;
    std::filesystem::path output_root;
    std::optional<std::string> run_id;
};

struct HarnessRunResult {
    RunArtifact run;
    std::filesystem::path run_dir;
    std::filesystem::path run_json_path;
    std::filesystem::path plan_json_path;
    std::filesystem::path trace_jsonl_path;
};

class ScenarioAdapter {
public:
    virtual ~ScenarioAdapter() = default;

    virtual std::string id() const = 0;
    virtual std::string description() const = 0;
    virtual std::string executor_name() const = 0;
    virtual std::string verifier_name() const = 0;
    virtual ScenarioPlanDefinition definition() const = 0;
    virtual bool archive_artifacts(const ScenarioContext& context,
                                   nlohmann::json* artifact_index,
                                   std::string* err) const = 0;
    virtual VerificationResult verify(const ScenarioContext& context,
                                      const nlohmann::json& artifact_index,
                                      std::string* err) const = 0;
};

bool is_valid_harness_step_status(std::string_view status);
bool is_valid_acceptance_status(std::string_view status);
bool is_valid_verification_status(std::string_view status);

void to_json(nlohmann::json& json_value, const StepResult& result);
void from_json(const nlohmann::json& json_value, StepResult& result);
void to_json(nlohmann::json& json_value, const AcceptanceDecision& decision);
void from_json(const nlohmann::json& json_value, AcceptanceDecision& decision);
void to_json(nlohmann::json& json_value, const VerificationResult& result);
void from_json(const nlohmann::json& json_value, VerificationResult& result);
void to_json(nlohmann::json& json_value, const RunArtifact& artifact);
void from_json(const nlohmann::json& json_value, RunArtifact& artifact);

Plan make_plan_from_definition(const ScenarioPlanDefinition& definition);
HarnessRunResult run_harness(const ScenarioAdapter& scenario, const HarnessRunOptions& options);

std::unique_ptr<ScenarioAdapter> create_scenario_adapter(std::string_view id);
std::vector<std::string> list_scenario_ids();
