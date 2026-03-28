#include "harness.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void print_help() {
    std::cout << "Usage: harness_runner --scenario <id> [options]\n\n"
              << "Options:\n"
              << "  --scenario <id>      Scenario adapter to run (required)\n"
              << "  --workspace <dir>    Workspace root to run against (default: current directory)\n"
              << "  --output-root <dir>  Root directory for run artifacts (default: tmp/harness-runs)\n"
              << "  --run-id <id>        Optional explicit run id\n"
              << "  --list-scenarios     Print available scenarios and exit\n"
              << "  --help               Show this help message\n";
}

int print_result_and_exit(const nlohmann::json& result, int exit_code) {
    std::cout << result.dump(2) << "\n";
    return exit_code;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::optional<std::string> scenario_id;
    fs::path workspace_root = fs::current_path();
    fs::path output_root = "tmp/harness-runs";
    std::optional<std::string> run_id;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help") {
            print_help();
            return 0;
        }
        if (arg == "--list-scenarios") {
            for (const std::string& id : list_scenario_ids()) {
                std::cout << id << "\n";
            }
            return 0;
        }
        if (arg == "--scenario" && index + 1 < argc) {
            scenario_id = argv[++index];
            continue;
        }
        if (arg == "--workspace" && index + 1 < argc) {
            workspace_root = argv[++index];
            continue;
        }
        if (arg == "--output-root" && index + 1 < argc) {
            output_root = argv[++index];
            continue;
        }
        if (arg == "--run-id" && index + 1 < argc) {
            run_id = argv[++index];
            continue;
        }

        return print_result_and_exit(
            nlohmann::json{
                {"status", "error"},
                {"error", "Unknown or incomplete argument: " + arg},
            },
            1);
    }

    if (!scenario_id.has_value()) {
        print_help();
        return print_result_and_exit(
            nlohmann::json{
                {"status", "error"},
                {"error", "Missing required --scenario argument."},
            },
            1);
    }

    std::unique_ptr<ScenarioAdapter> scenario = create_scenario_adapter(*scenario_id);
    if (!scenario) {
        return print_result_and_exit(
            nlohmann::json{
                {"status", "error"},
                {"error", "Unknown scenario: " + *scenario_id},
            },
            1);
    }

    try {
        const HarnessRunResult result =
            run_harness(*scenario, HarnessRunOptions{workspace_root, output_root, run_id});
        return print_result_and_exit(
            nlohmann::json{
                {"status", result.run.status},
                {"run_id", result.run.run_id},
                {"scenario", result.run.scenario},
                {"verification_status", result.run.verification_status},
                {"verification_summary", result.run.verification_summary},
                {"acceptance_status", result.run.acceptance.status},
                {"acceptance_summary", result.run.acceptance.summary},
                {"run_dir", result.run_dir.generic_string()},
                {"run_json", result.run_json_path.generic_string()},
                {"plan_json", result.plan_json_path.generic_string()},
                {"trace_jsonl", result.trace_jsonl_path.generic_string()},
            },
            result.run.status == "succeeded" ? 0 : 1);
    } catch (const std::exception& e) {
        return print_result_and_exit(
            nlohmann::json{
                {"status", "error"},
                {"error", e.what()},
                {"scenario", *scenario_id},
            },
            1);
    }
}
