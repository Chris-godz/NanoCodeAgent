#include "harness.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace {

bool load_json_file(const fs::path& path, nlohmann::json* out) {
    if (out == nullptr || !fs::is_regular_file(path)) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    try {
        input >> *out;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string relative_to(const fs::path& base, const fs::path& path) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, base, ec);
    return ec ? path.lexically_normal().generic_string() : relative.generic_string();
}

void copy_file_if_exists(const fs::path& from,
                         const fs::path& to,
                         const fs::path& run_dir,
                         std::vector<std::string>* copied_files) {
    if (!fs::is_regular_file(from)) {
        return;
    }
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    if (ec) {
        throw std::runtime_error("Could not create artifact directory '" + to.parent_path().string() + "': " +
                                 ec.message());
    }
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        throw std::runtime_error("Could not copy artifact '" + from.string() + "' to '" + to.string() +
                                 "': " + ec.message());
    }
    if (copied_files != nullptr) {
        copied_files->push_back(relative_to(run_dir, to));
    }
}

void copy_tree_if_exists(const fs::path& from,
                         const fs::path& to,
                         const fs::path& run_dir,
                         std::vector<std::string>* copied_files) {
    if (!fs::is_directory(from)) {
        return;
    }

    for (fs::recursive_directory_iterator it(from), end; it != end; ++it) {
        const fs::path relative = fs::relative(it->path(), from);
        const fs::path target = to / relative;
        std::error_code ec;
        if (it->is_directory()) {
            fs::create_directories(target, ec);
            if (ec) {
                throw std::runtime_error("Could not create artifact directory '" + target.string() + "': " +
                                         ec.message());
            }
            continue;
        }
        if (!it->is_regular_file()) {
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            throw std::runtime_error("Could not create artifact directory '" + target.parent_path().string() + "': " +
                                     ec.message());
        }
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("Could not copy artifact '" + it->path().string() + "' to '" + target.string() +
                                     "': " + ec.message());
        }
        if (copied_files != nullptr) {
            copied_files->push_back(relative_to(run_dir, target));
        }
    }
}

std::vector<std::string> collect_targets_from_scope(const nlohmann::json& scope) {
    std::vector<std::string> targets;
    if (scope.contains("approved_targets") && scope.at("approved_targets").is_array()) {
        for (const auto& item : scope.at("approved_targets")) {
            if (item.is_string()) {
                targets.push_back(item.get<std::string>());
            }
        }
    }
    if (!targets.empty()) {
        return targets;
    }

    if (scope.value("update_readme", false)) {
        targets.push_back("README.md");
    }
    if (scope.value("update_book", false) && scope.contains("book_dirs_or_chapters") &&
        scope.at("book_dirs_or_chapters").is_array()) {
        for (const auto& item : scope.at("book_dirs_or_chapters")) {
            if (item.is_string()) {
                targets.push_back(item.get<std::string>());
            }
        }
    }
    return targets;
}

class DocAutomationScenario final : public ScenarioAdapter {
public:
    std::string id() const override { return "doc_automation"; }

    std::string description() const override {
        return "Run the existing closed-loop doc automation pipeline and archive verifier-ready evidence.";
    }

    std::string executor_name() const override { return "harness_runner"; }

    std::string verifier_name() const override { return "doc_automation_verifier"; }

    ScenarioPlanDefinition definition() const override {
        return {
            "Run the closed-loop doc automation pipeline and record verifier-ready evidence.",
            {
                ScenarioStepDefinition{
                    "run-doc-automation",
                    "Run the closed-loop doc automation pipeline",
                    "Invoke the existing deterministic + orchestration doc pipeline without rewriting its internals.",
                    "bash scripts/docgen/run_docgen_e2e_closed.sh",
                },
            },
            nlohmann::json{
                {"scenario", id()},
                {"adapter", "DocAutomationScenario"},
            },
        };
    }

    bool archive_artifacts(const ScenarioContext& context,
                           nlohmann::json* artifact_index,
                           std::string* err) const override {
        try {
            std::vector<std::string> copied_generated_files;
            std::vector<std::string> copied_target_files;
            const fs::path generated_source = context.workspace_root / "docs" / "generated";
            const fs::path generated_dest = context.scenario_artifacts_dir / "generated";
            const fs::path targets_dest = context.scenario_artifacts_dir / "targets";

            copy_tree_if_exists(generated_source, generated_dest, context.run_dir, &copied_generated_files);

            nlohmann::json scope;
            const fs::path scope_path = generated_source / "doc_scope_decision.json";
            const bool has_scope = load_json_file(scope_path, &scope);
            const std::vector<std::string> targets = has_scope ? collect_targets_from_scope(scope)
                                                               : std::vector<std::string>{};
            for (const std::string& target : targets) {
                const fs::path source_path = context.workspace_root / fs::path(target);
                if (fs::is_regular_file(source_path)) {
                    copy_file_if_exists(source_path,
                                        targets_dest / fs::path(target),
                                        context.run_dir,
                                        &copied_target_files);
                } else if (fs::is_directory(source_path)) {
                    copy_tree_if_exists(source_path,
                                        targets_dest / fs::path(target),
                                        context.run_dir,
                                        &copied_target_files);
                }
            }

            if (artifact_index != nullptr) {
                *artifact_index = nlohmann::json{
                    {"scenario", id()},
                    {"generated_root", fs::path("artifacts/scenario/generated").generic_string()},
                    {"targets_root", fs::path("artifacts/scenario/targets").generic_string()},
                    {"copied_generated_files", copied_generated_files},
                    {"copied_target_files", copied_target_files},
                    {"approved_targets", targets},
                    {"core_reports", {
                        {"verify_report", fs::path("artifacts/scenario/generated/verify_report.json").generic_string()},
                        {"loop_state", fs::path("artifacts/scenario/generated/e2e_loop_state.json").generic_string()},
                        {"run_evidence", fs::path("artifacts/scenario/generated/e2e_run_evidence.json").generic_string()},
                        {"summary", fs::path("artifacts/scenario/generated/docgen_e2e_summary.md").generic_string()},
                        {"failure_report", fs::path("artifacts/scenario/generated/e2e_failure_report.md").generic_string()},
                    }},
                };
            }
            return true;
        } catch (const std::exception& e) {
            if (err != nullptr) {
                *err = e.what();
            }
            return false;
        }
    }

    VerificationResult verify(const ScenarioContext& context,
                              const nlohmann::json& artifact_index,
                              std::string* err) const override {
        (void)context;
        VerificationResult result;
        result.status = "failed";
        result.acceptance = {
            "terminal_failure",
            "Doc automation verification did not pass.",
            nlohmann::json::object(),
        };

        const auto report_path = [](const nlohmann::json& index, const char* key) -> fs::path {
            if (!index.contains("core_reports") || !index.at("core_reports").is_object()) {
                return {};
            }
            const auto& reports = index.at("core_reports");
            if (!reports.contains(key) || !reports.at(key).is_string()) {
                return {};
            }
            return fs::path(reports.at(key).get<std::string>());
        };

        const fs::path generated_root = context.run_dir / "artifacts" / "scenario" / "generated";
        const fs::path verify_report_path = context.run_dir / report_path(artifact_index, "verify_report");
        const fs::path loop_state_path = context.run_dir / report_path(artifact_index, "loop_state");
        const fs::path run_evidence_path = context.run_dir / report_path(artifact_index, "run_evidence");
        const fs::path summary_path = context.run_dir / report_path(artifact_index, "summary");
        const fs::path failure_report_path = context.run_dir / report_path(artifact_index, "failure_report");

        nlohmann::json verify_report = nlohmann::json::object();
        nlohmann::json loop_state = nlohmann::json::object();
        nlohmann::json run_evidence = nlohmann::json::object();
        const bool has_verify_report = load_json_file(verify_report_path, &verify_report);
        const bool has_loop_state = load_json_file(loop_state_path, &loop_state);
        const bool has_run_evidence = load_json_file(run_evidence_path, &run_evidence);
        const bool has_summary = fs::is_regular_file(summary_path);
        const bool has_failure_report = fs::is_regular_file(failure_report_path);

        const bool blocking_passed = has_verify_report && verify_report.value("blocking_passed", false);
        const std::string phase = has_loop_state ? loop_state.value("phase", "") : "";
        const bool phase_passed = phase == "Passed";

        result.details = nlohmann::json{
            {"generated_root", generated_root.generic_string()},
            {"verify_report_present", has_verify_report},
            {"loop_state_present", has_loop_state},
            {"run_evidence_present", has_run_evidence},
            {"summary_present", has_summary},
            {"failure_report_present", has_failure_report},
            {"blocking_passed", blocking_passed},
            {"phase", phase},
            {"verify_report_path", verify_report_path.generic_string()},
            {"loop_state_path", loop_state_path.generic_string()},
            {"run_evidence_path", run_evidence_path.generic_string()},
            {"summary_path", summary_path.generic_string()},
            {"failure_report_path", failure_report_path.generic_string()},
        };

        if (blocking_passed && phase_passed && has_summary && !has_failure_report) {
            result.status = "passed";
            result.summary = "Doc automation verification passed with blocking verify, Passed loop phase, and summary evidence.";
            result.acceptance = {
                "success",
                "Doc automation artifacts satisfy the acceptance contract.",
                result.details,
            };
            return result;
        }

        std::vector<std::string> failures;
        if (!has_verify_report) {
            failures.push_back("verify_report.json missing");
        } else if (!blocking_passed) {
            failures.push_back("blocking_passed is false");
        }
        if (!has_loop_state) {
            failures.push_back("e2e_loop_state.json missing");
        } else if (!phase_passed) {
            failures.push_back("phase is '" + phase + "'");
        }
        if (!has_run_evidence) {
            failures.push_back("e2e_run_evidence.json missing");
        }
        if (!has_summary) {
            failures.push_back("docgen_e2e_summary.md missing");
        }
        if (has_failure_report) {
            failures.push_back("e2e_failure_report.md present");
        }

        std::string summary = "Doc automation verification failed.";
        if (!failures.empty()) {
            summary += " Reasons: ";
            for (std::size_t index = 0; index < failures.size(); ++index) {
                if (index > 0) {
                    summary += "; ";
                }
                summary += failures[index];
            }
            summary += ".";
        }
        result.summary = summary;
        result.details["failures"] = failures;
        result.acceptance = {
            "terminal_failure",
            summary,
            result.details,
        };
        if (err != nullptr) {
            *err = "";
        }
        return result;
    }
};

}  // namespace

std::unique_ptr<ScenarioAdapter> create_scenario_adapter(std::string_view id) {
    if (id == "doc_automation") {
        return std::make_unique<DocAutomationScenario>();
    }
    return nullptr;
}

std::vector<std::string> list_scenario_ids() {
    return {"doc_automation"};
}
