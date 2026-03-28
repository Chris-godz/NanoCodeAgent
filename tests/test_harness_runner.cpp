#include <gtest/gtest.h>

#include "harness.hpp"
#include "trace.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

void must_write_text_file(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        throw std::runtime_error("Could not create directory '" + path.parent_path().string() + "': " + ec.message());
    }

    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("Could not open file '" + path.string() + "' for writing.");
    }
    output << content;
    if (!output.good()) {
        throw std::runtime_error("Could not write file '" + path.string() + "'.");
    }
}

nlohmann::json load_json_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.is_open()) << path.string();
    nlohmann::json json_value;
    input >> json_value;
    return json_value;
}

std::string read_text_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.is_open()) << path.string();
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<TraceEvent> parse_trace_file(const fs::path& path) {
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

std::size_t find_event_index(const std::vector<TraceEvent>& events, const std::string& kind) {
    for (std::size_t index = 0; index < events.size(); ++index) {
        if (events[index].kind == kind) {
            return index;
        }
    }
    return events.size();
}

const TraceEvent* find_event(const std::vector<TraceEvent>& events, const std::string& kind) {
    const std::size_t index = find_event_index(events, kind);
    return index < events.size() ? &events[index] : nullptr;
}

std::size_t count_events_by_kind(const std::vector<TraceEvent>& events, const std::string& kind) {
    std::size_t count = 0;
    for (const TraceEvent& event : events) {
        if (event.kind == kind) {
            ++count;
        }
    }
    return count;
}

class FakeScenario final : public ScenarioAdapter {
public:
    explicit FakeScenario(std::string command) : command_(std::move(command)) {}

    std::string id() const override { return "fake_scenario"; }

    std::string description() const override { return "A minimal scenario used to test the harness contract."; }

    std::string executor_name() const override { return "harness_runner"; }

    std::string verifier_name() const override { return "fake_verifier"; }

    ScenarioPlanDefinition definition() const override {
        return {
            "Run a single fake step and capture artifacts.",
            {
                ScenarioStepDefinition{
                    "fake-step",
                    "Execute the fake scenario command",
                    "Produce deterministic harness evidence.",
                    command_,
                    30000,
                    nlohmann::json{{"kind", "fake"}},
                },
            },
            nlohmann::json{{"scenario", id()}},
        };
    }

    bool archive_artifacts(const ScenarioContext& context,
                           nlohmann::json* artifact_index,
                           std::string* err) const override {
        (void)err;
        const fs::path proof_path = context.scenario_artifacts_dir / "proof.txt";
        must_write_text_file(proof_path, "proof");
        if (artifact_index != nullptr) {
            *artifact_index = nlohmann::json{
                {"proof_path", fs::path("artifacts/scenario/proof.txt").generic_string()},
                {"step_count", context.step_results.size()},
            };
        }
        return true;
    }

    VerificationResult verify(const ScenarioContext& context,
                              const nlohmann::json& artifact_index,
                              std::string* err) const override {
        (void)context;
        (void)err;
        VerificationResult result;
        const fs::path proof_path = context.run_dir / artifact_index.value("proof_path", "");
        if (fs::is_regular_file(proof_path)) {
            result.status = "passed";
            result.summary = "Fake scenario proof artifact is present.";
            result.details = nlohmann::json{{"proof_path", proof_path.generic_string()}};
            result.acceptance = {
                "success",
                "Fake scenario acceptance passed.",
                result.details,
            };
            return result;
        }

        result.status = "failed";
        result.summary = "Fake scenario proof artifact is missing.";
        result.details = nlohmann::json{{"proof_path", proof_path.generic_string()}};
        result.acceptance = {
            "terminal_failure",
            result.summary,
            result.details,
        };
        return result;
    }

private:
    std::string command_;
};

class ThrowingVerifyScenario final : public ScenarioAdapter {
public:
    std::string id() const override { return "throwing_verify"; }

    std::string description() const override {
        return "A scenario whose verification step throws after execution succeeds.";
    }

    std::string executor_name() const override { return "harness_runner"; }

    std::string verifier_name() const override { return "throwing_verify_verifier"; }

    ScenarioPlanDefinition definition() const override {
        return {
            "Run a single successful step before the verifier crashes.",
            {
                ScenarioStepDefinition{
                    "verify-throws-step",
                    "Execute a successful step",
                    "Leave standard harness artifacts behind before verify throws.",
                    "printf 'step completed before verifier crash\\n'",
                },
            },
            nlohmann::json{{"scenario", id()}},
        };
    }

    bool archive_artifacts(const ScenarioContext& context,
                           nlohmann::json* artifact_index,
                           std::string* err) const override {
        (void)err;
        const fs::path proof_path = context.scenario_artifacts_dir / "proof.txt";
        must_write_text_file(proof_path, "proof-before-verify-crash");
        if (artifact_index != nullptr) {
            *artifact_index = nlohmann::json{
                {"proof_path", fs::path("artifacts/scenario/proof.txt").generic_string()},
            };
        }
        return true;
    }

    VerificationResult verify(const ScenarioContext& context,
                              const nlohmann::json& artifact_index,
                              std::string* err) const override {
        (void)context;
        (void)artifact_index;
        (void)err;
        throw std::runtime_error("Verifier crashed after step success.");
    }
};

class UnsafeStepIdScenario final : public ScenarioAdapter {
public:
    std::string id() const override { return "unsafe_step_id"; }
    std::string description() const override { return "A scenario with an unsafe step id."; }
    std::string executor_name() const override { return "harness_runner"; }
    std::string verifier_name() const override { return "unsafe_step_verifier"; }

    ScenarioPlanDefinition definition() const override {
        return {
            "Reject unsafe step ids before artifact layout is created.",
            {
                ScenarioStepDefinition{
                    "../escape",
                    "Unsafe step id",
                    "This should never execute.",
                    "printf 'should not run\\n'",
                },
            },
            nlohmann::json{{"scenario", id()}},
        };
    }

    bool archive_artifacts(const ScenarioContext& context,
                           nlohmann::json* artifact_index,
                           std::string* err) const override {
        (void)context;
        (void)artifact_index;
        (void)err;
        return true;
    }

    VerificationResult verify(const ScenarioContext& context,
                              const nlohmann::json& artifact_index,
                              std::string* err) const override {
        (void)context;
        (void)artifact_index;
        (void)err;
        return {};
    }
};

class DuplicateStepIdScenario final : public ScenarioAdapter {
public:
    std::string id() const override { return "duplicate_step_id"; }
    std::string description() const override { return "A scenario with duplicate step ids."; }
    std::string executor_name() const override { return "harness_runner"; }
    std::string verifier_name() const override { return "duplicate_step_verifier"; }

    ScenarioPlanDefinition definition() const override {
        return {
            "Reject duplicate step ids before artifact layout is created.",
            {
                ScenarioStepDefinition{
                    "duplicate-step",
                    "First duplicate step",
                    "This should never execute.",
                    "printf 'first\\n'",
                },
                ScenarioStepDefinition{
                    "duplicate-step",
                    "Second duplicate step",
                    "This should never execute.",
                    "printf 'second\\n'",
                },
            },
            nlohmann::json{{"scenario", id()}},
        };
    }

    bool archive_artifacts(const ScenarioContext& context,
                           nlohmann::json* artifact_index,
                           std::string* err) const override {
        (void)context;
        (void)artifact_index;
        (void)err;
        return true;
    }

    VerificationResult verify(const ScenarioContext& context,
                              const nlohmann::json& artifact_index,
                              std::string* err) const override {
        (void)context;
        (void)artifact_index;
        (void)err;
        return {};
    }
};

class HarnessRunnerTest : public ::testing::Test {
protected:
    fs::path workspace_root;
    fs::path output_root;

    void SetUp() override {
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        workspace_root = fs::temp_directory_path() / ("nano_harness_runner_" + unique);
        output_root = workspace_root / "tmp" / "harness-runs";
        fs::create_directories(workspace_root);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(workspace_root, ec);
    }
};

TEST_F(HarnessRunnerTest, ArtifactContractCreatesStandardRunLayout) {
    FakeScenario scenario("printf 'hello from harness\\n'; printf 'warn\\n' >&2");

    const HarnessRunResult result =
        run_harness(scenario, HarnessRunOptions{workspace_root, output_root, std::string("artifact-contract")});

    EXPECT_TRUE(fs::is_regular_file(result.run_json_path));
    EXPECT_TRUE(fs::is_regular_file(result.plan_json_path));
    EXPECT_TRUE(fs::is_regular_file(result.trace_jsonl_path));
    EXPECT_TRUE(fs::is_directory(result.run_dir / "artifacts"));
    EXPECT_TRUE(fs::is_regular_file(result.run_dir / "artifacts" / "steps" / "fake-step" / "stdout.txt"));
    EXPECT_TRUE(fs::is_regular_file(result.run_dir / "artifacts" / "steps" / "fake-step" / "stderr.txt"));
    EXPECT_TRUE(fs::is_regular_file(result.run_dir / "artifacts" / "steps" / "fake-step" / "result.json"));
    EXPECT_TRUE(fs::is_regular_file(result.run_dir / "artifacts" / "scenario" / "index.json"));

    const nlohmann::json run_json = load_json_file(result.run_json_path);
    EXPECT_EQ(run_json.value("status", ""), "succeeded");
    EXPECT_EQ(run_json.value("executor", ""), "harness_runner");
    EXPECT_EQ(run_json.value("verifier", ""), "fake_verifier");
    EXPECT_EQ(run_json.value("verification_status", ""), "passed");
    EXPECT_TRUE(run_json.contains("verification_summary"));
    ASSERT_TRUE(run_json.contains("acceptance"));
    EXPECT_EQ(run_json.at("acceptance").value("status", ""), "success");

    const nlohmann::json plan_json = load_json_file(result.plan_json_path);
    ASSERT_TRUE(plan_json.contains("plan"));
    ASSERT_TRUE(plan_json.at("plan").contains("steps"));
    ASSERT_EQ(plan_json.at("plan").at("steps").size(), 1u);
    EXPECT_EQ(plan_json.at("plan").at("steps")[0].value("status", ""), "completed");
}

TEST_F(HarnessRunnerTest, TraceJsonlEmitsLifecycleEventsInOrder) {
    FakeScenario scenario("printf 'trace order\\n'");

    const HarnessRunResult result =
        run_harness(scenario, HarnessRunOptions{workspace_root, output_root, std::string("event-order")});
    const std::vector<TraceEvent> events = parse_trace_file(result.trace_jsonl_path);

    const std::size_t run_started = find_event_index(events, "run_started");
    const std::size_t plan_created = find_event_index(events, "plan_created");
    const std::size_t step_started = find_event_index(events, "step_started");
    const std::size_t tool_invoked = find_event_index(events, "tool_invoked");
    const std::size_t tool_finished = find_event_index(events, "tool_finished");
    const std::size_t verification_started = find_event_index(events, "verification_started");
    const std::size_t verification_passed = find_event_index(events, "verification_passed");
    const std::size_t acceptance_checked = find_event_index(events, "acceptance_checked");
    const std::size_t run_succeeded = find_event_index(events, "run_succeeded");

    ASSERT_LT(run_started, events.size());
    ASSERT_LT(plan_created, events.size());
    ASSERT_LT(step_started, events.size());
    ASSERT_LT(tool_invoked, events.size());
    ASSERT_LT(tool_finished, events.size());
    ASSERT_LT(verification_started, events.size());
    ASSERT_LT(verification_passed, events.size());
    ASSERT_LT(acceptance_checked, events.size());
    ASSERT_LT(run_succeeded, events.size());

    EXPECT_LT(run_started, plan_created);
    EXPECT_LT(plan_created, step_started);
    EXPECT_LT(step_started, tool_invoked);
    EXPECT_LT(tool_invoked, tool_finished);
    EXPECT_LT(tool_finished, verification_started);
    EXPECT_LT(verification_started, verification_passed);
    EXPECT_LT(verification_passed, acceptance_checked);
    EXPECT_LT(acceptance_checked, run_succeeded);
}

TEST_F(HarnessRunnerTest, TracePayloadsCarryReplayFields) {
    FakeScenario scenario("printf 'trace payloads\\n'");

    const HarnessRunResult result =
        run_harness(scenario, HarnessRunOptions{workspace_root, output_root, std::string("trace-fields")});
    const std::vector<TraceEvent> events = parse_trace_file(result.trace_jsonl_path);

    ASSERT_EQ(events.size(), 9u);
    for (const TraceEvent& event : events) {
        EXPECT_EQ(event.payload.value("run_id", ""), "trace-fields") << event.kind;
        EXPECT_EQ(event.payload.value("scenario", ""), "fake_scenario") << event.kind;
        EXPECT_EQ(event.payload.value("executor", ""), "harness_runner") << event.kind;
        EXPECT_EQ(event.payload.value("verifier", ""), "fake_verifier") << event.kind;
    }

    const TraceEvent* tool_invoked = find_event(events, "tool_invoked");
    ASSERT_NE(tool_invoked, nullptr);
    EXPECT_EQ(tool_invoked->payload.value("step_id", ""), "fake-step");
    EXPECT_EQ(tool_invoked->payload.value("tool_name", ""), "subprocess");
    EXPECT_EQ(tool_invoked->payload.value("working_directory", ""), workspace_root.generic_string());
    EXPECT_EQ(tool_invoked->payload.value("command_artifact", ""),
              "artifacts/steps/fake-step/command.json");

    const TraceEvent* verification_started = find_event(events, "verification_started");
    ASSERT_NE(verification_started, nullptr);
    EXPECT_EQ(verification_started->payload.value("artifact_index", ""),
              "artifacts/scenario/index.json");

    const TraceEvent* run_succeeded = find_event(events, "run_succeeded");
    ASSERT_NE(run_succeeded, nullptr);
    EXPECT_EQ(run_succeeded->payload.value("run_json_path", ""), "run.json");
    EXPECT_EQ(run_succeeded->payload.value("plan_path", ""), "plan.json");
    EXPECT_EQ(run_succeeded->payload.value("trace_path", ""), "trace.jsonl");
}

TEST_F(HarnessRunnerTest, DocAutomationScenarioArchivesEvidenceAndVerifiesFromArtifacts) {
    std::unique_ptr<ScenarioAdapter> scenario = create_scenario_adapter("doc_automation");
    ASSERT_NE(scenario, nullptr);

    ASSERT_NO_THROW(must_write_text_file(workspace_root / "README.md", "# README\n"));
    ASSERT_NO_THROW(must_write_text_file(workspace_root / "book" / "src" / "demo.md", "# Demo\n"));
    ASSERT_NO_THROW(
        must_write_text_file(workspace_root / "docs" / "generated" / "docgen_e2e_summary.md", "# Summary\n"));
    ASSERT_NO_THROW(must_write_text_file(workspace_root / "docs" / "generated" / "doc_scope_decision.json",
                                         nlohmann::json{
                                             {"approved_targets", nlohmann::json::array({"README.md", "book/src/demo.md"})},
                                             {"update_readme", true},
                                             {"update_book", true},
                                         }
                                             .dump(2)));
    ASSERT_NO_THROW(must_write_text_file(workspace_root / "docs" / "generated" / "verify_report.json",
                                         nlohmann::json{{"blocking_passed", true}}.dump(2)));
    ASSERT_NO_THROW(must_write_text_file(workspace_root / "docs" / "generated" / "e2e_loop_state.json",
                                         nlohmann::json{{"phase", "Passed"}}.dump(2)));
    ASSERT_NO_THROW(must_write_text_file(workspace_root / "docs" / "generated" / "e2e_run_evidence.json",
                                         nlohmann::json{{"phase", "Passed"}}.dump(2)));

    const fs::path run_dir = output_root / "doc-scenario-smoke";
    const fs::path artifacts_dir = run_dir / "artifacts";
    const fs::path scenario_artifacts_dir = artifacts_dir / "scenario";
    const fs::path step_artifacts_dir = artifacts_dir / "steps";
    fs::create_directories(scenario_artifacts_dir);
    fs::create_directories(step_artifacts_dir);

    const ScenarioContext context{
        workspace_root,
        run_dir,
        artifacts_dir,
        scenario_artifacts_dir,
        step_artifacts_dir,
        "doc-scenario-smoke",
        scenario->id(),
        {StepResult{
            "run-doc-automation",
            "success",
            "2026-03-27T00:00:00.000Z",
            "2026-03-27T00:00:01.000Z",
            "Step completed successfully.",
            0,
            nlohmann::json::object(),
            nlohmann::json::object(),
        }},
    };

    nlohmann::json artifact_index;
    std::string err;
    ASSERT_TRUE(scenario->archive_artifacts(context, &artifact_index, &err)) << err;

    EXPECT_TRUE(fs::is_regular_file(run_dir / "artifacts" / "scenario" / "generated" / "verify_report.json"));
    EXPECT_TRUE(fs::is_regular_file(run_dir / "artifacts" / "scenario" / "targets" / "README.md"));
    EXPECT_TRUE(fs::is_regular_file(run_dir / "artifacts" / "scenario" / "targets" / "book" / "src" / "demo.md"));
    ASSERT_TRUE(artifact_index.contains("copied_target_files"));
    bool saw_readme_copy = false;
    for (const auto& item : artifact_index.at("copied_target_files")) {
        if (item.is_string() && item.get<std::string>() == "artifacts/scenario/targets/README.md") {
            saw_readme_copy = true;
        }
    }
    EXPECT_TRUE(saw_readme_copy);

    const VerificationResult verification = scenario->verify(context, artifact_index, &err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(verification.status, "passed");
    EXPECT_EQ(verification.acceptance.status, "success");
}

TEST_F(HarnessRunnerTest, FailedRunEmitsTerminalFailureAndRunFailedTrace) {
    FakeScenario scenario("exit 7");

    const HarnessRunResult result =
        run_harness(scenario, HarnessRunOptions{workspace_root, output_root, std::string("failed-run")});
    ASSERT_EQ(result.run.status, "failed");
    ASSERT_EQ(result.run.step_results.size(), 1u);
    EXPECT_EQ(result.run.step_results[0].status, "terminal_failure");
    EXPECT_EQ(result.run.verification_status, "failed");
    EXPECT_EQ(result.run.acceptance.status, "terminal_failure");

    const std::vector<TraceEvent> events = parse_trace_file(result.trace_jsonl_path);
    EXPECT_LT(find_event_index(events, "verification_failed"), events.size());
    EXPECT_LT(find_event_index(events, "run_failed"), events.size());
}

TEST_F(HarnessRunnerTest, VerifierExceptionStillLeavesFailedRunEvidenceAndAcceptanceTransition) {
    ThrowingVerifyScenario scenario;

    const HarnessRunResult result =
        run_harness(scenario, HarnessRunOptions{workspace_root, output_root, std::string("verify-throws")});

    ASSERT_EQ(result.run.status, "failed");
    ASSERT_EQ(result.run.step_results.size(), 1u);
    EXPECT_EQ(result.run.step_results[0].status, "success");
    EXPECT_EQ(result.run.verification_status, "failed");
    EXPECT_EQ(result.run.acceptance.status, "terminal_failure");
    EXPECT_TRUE(fs::is_regular_file(result.run_json_path));
    EXPECT_TRUE(fs::is_regular_file(result.plan_json_path));
    EXPECT_TRUE(fs::is_regular_file(result.trace_jsonl_path));
    EXPECT_TRUE(fs::is_regular_file(result.run_dir / "artifacts" / "steps" / "verify-throws-step" / "stdout.txt"));
    EXPECT_TRUE(fs::is_regular_file(result.run_dir / "artifacts" / "steps" / "verify-throws-step" / "result.json"));
    EXPECT_TRUE(fs::is_regular_file(result.run_dir / "artifacts" / "scenario" / "index.json"));
    EXPECT_TRUE(fs::is_regular_file(result.run_dir / "artifacts" / "scenario" / "proof.txt"));

    const nlohmann::json run_json = load_json_file(result.run_json_path);
    EXPECT_EQ(run_json.value("status", ""), "failed");
    ASSERT_TRUE(run_json.contains("acceptance"));
    EXPECT_EQ(run_json.at("acceptance").value("status", ""), "terminal_failure");
    EXPECT_NE(run_json.value("verification_summary", "").find("Verifier crashed after step success."),
              std::string::npos);

    const std::vector<TraceEvent> events = parse_trace_file(result.trace_jsonl_path);
    EXPECT_LT(find_event_index(events, "verification_failed"), events.size());
    EXPECT_LT(find_event_index(events, "run_failed"), events.size());
}

TEST_F(HarnessRunnerTest, ReusedRunIdCleansPreviousArtifactsAndTrace) {
    const std::string run_id = "reused-run";

    const HarnessRunResult first =
        run_harness(FakeScenario("printf 'first run\\n'"),
                    HarnessRunOptions{workspace_root, output_root, run_id});
    must_write_text_file(first.run_dir / "artifacts" / "stale.txt", "stale");
    EXPECT_EQ(read_text_file(first.run_dir / "artifacts" / "steps" / "fake-step" / "stdout.txt"),
              "first run\n");

    const HarnessRunResult second =
        run_harness(FakeScenario("printf 'second run\\n'"),
                    HarnessRunOptions{workspace_root, output_root, run_id});

    EXPECT_FALSE(fs::exists(second.run_dir / "artifacts" / "stale.txt"));
    EXPECT_EQ(read_text_file(second.run_dir / "artifacts" / "steps" / "fake-step" / "stdout.txt"),
              "second run\n");

    const std::vector<TraceEvent> events = parse_trace_file(second.trace_jsonl_path);
    EXPECT_EQ(count_events_by_kind(events, "run_started"), 1u);
    EXPECT_EQ(count_events_by_kind(events, "run_succeeded"), 1u);
}

TEST_F(HarnessRunnerTest, RejectsUnsafeRunIdsAndStepIdsBeforeCreatingRunLayout) {
    EXPECT_THROW(run_harness(FakeScenario("printf 'ok\\n'"),
                             HarnessRunOptions{workspace_root, output_root, std::string("../escape-run")}),
                 std::invalid_argument);
    EXPECT_FALSE(fs::exists(output_root / "escape-run"));

    EXPECT_THROW(run_harness(UnsafeStepIdScenario{},
                             HarnessRunOptions{workspace_root, output_root, std::string("unsafe-step-id")}),
                 std::invalid_argument);
    EXPECT_FALSE(fs::exists(output_root / "unsafe-step-id"));

    EXPECT_THROW(run_harness(DuplicateStepIdScenario{},
                             HarnessRunOptions{workspace_root, output_root, std::string("duplicate-step-id")}),
                 std::invalid_argument);
    EXPECT_FALSE(fs::exists(output_root / "duplicate-step-id"));
}

}  // namespace
