#include <gtest/gtest.h>

#include "state.hpp"

#include <stdexcept>

namespace {

TEST(StateModelTest, DefaultConstructionProvidesMinimalSkeletonDefaults) {
    PlanStep step;
    EXPECT_TRUE(step.id.empty());
    EXPECT_TRUE(step.title.empty());
    EXPECT_EQ(step.status, "pending");
    EXPECT_TRUE(step.detail.empty());
    EXPECT_TRUE(step.metadata.is_object());
    EXPECT_TRUE(step.metadata.empty());

    Plan plan;
    EXPECT_EQ(plan.generation, 0);
    EXPECT_TRUE(plan.summary.empty());
    EXPECT_TRUE(plan.steps.empty());
    EXPECT_TRUE(plan.metadata.is_object());
    EXPECT_TRUE(plan.metadata.empty());
    EXPECT_EQ(plan.current_step_index, -1);
    EXPECT_TRUE(plan.outcome.empty());

    TraceEvent event;
    EXPECT_TRUE(event.kind.empty());
    EXPECT_TRUE(event.message.empty());
    EXPECT_TRUE(event.created_at.empty());
    EXPECT_TRUE(event.payload.is_object());
    EXPECT_TRUE(event.payload.empty());

    SessionState session;
    EXPECT_TRUE(session.session_id.empty());
    EXPECT_TRUE(session.created_at.empty());
    EXPECT_TRUE(session.updated_at.empty());
    EXPECT_EQ(session.turn_index, 0);
    EXPECT_TRUE(session.messages.is_array());
    EXPECT_TRUE(session.messages.empty());
    EXPECT_TRUE(session.active_rules_snapshot.is_object());
    EXPECT_TRUE(session.active_rules_snapshot.empty());
    EXPECT_EQ(session.plan.generation, 0);
    EXPECT_TRUE(session.plan.summary.empty());
    EXPECT_TRUE(session.plan.steps.empty());
    EXPECT_TRUE(session.plan_raw_response.is_object());
    EXPECT_TRUE(session.plan_raw_response.empty());
    EXPECT_TRUE(session.plan_validated_artifact.is_object());
    EXPECT_TRUE(session.plan_validated_artifact.empty());
    EXPECT_TRUE(session.plan_validation_errors.empty());
    EXPECT_FALSE(session.plan_normalization_applied);
    EXPECT_EQ(session.plan_state, "IDLE");
    EXPECT_FALSE(session.needs_plan);
    EXPECT_TRUE(session.trace.empty());
}

TEST(StateModelTest, JsonRoundTripPreservesSkeletonTypes) {
    PlanStep step;
    step.id = "step-1";
    step.title = "capture planner output";
    step.status = "in_progress";
    step.detail = "keep the schema small";
    step.metadata = nlohmann::json{{"owner", "planner"}, {"optional", true}};

    const nlohmann::json step_json = step;
    const PlanStep parsed_step = step_json.get<PlanStep>();
    EXPECT_EQ(parsed_step.id, step.id);
    EXPECT_EQ(parsed_step.title, step.title);
    EXPECT_EQ(parsed_step.status, step.status);
    EXPECT_EQ(parsed_step.detail, step.detail);
    EXPECT_EQ(parsed_step.metadata, step.metadata);

    Plan plan;
    plan.generation = 3;
    plan.summary = "bootstrap planning state";
    plan.steps.push_back(step);
    plan.metadata = nlohmann::json{{"mode", "planning"}};

    const nlohmann::json plan_json = plan;
    const Plan parsed_plan = plan_json.get<Plan>();
    EXPECT_EQ(parsed_plan.generation, plan.generation);
    EXPECT_EQ(parsed_plan.summary, plan.summary);
    ASSERT_EQ(parsed_plan.steps.size(), 1u);
    EXPECT_EQ(parsed_plan.steps[0].title, step.title);
    EXPECT_EQ(parsed_plan.metadata, plan.metadata);
    EXPECT_EQ(parsed_plan.current_step_index, -1);
    EXPECT_TRUE(parsed_plan.outcome.empty());

    TraceEvent event;
    event.kind = "note";
    event.message = "planner reserved for future work";
    event.created_at = "2026-03-25T12:00:00.000Z";
    event.payload = nlohmann::json{{"phase", "state-skeleton"}};

    const nlohmann::json event_json = event;
    const TraceEvent parsed_event = event_json.get<TraceEvent>();
    EXPECT_EQ(parsed_event.kind, event.kind);
    EXPECT_EQ(parsed_event.message, event.message);
    EXPECT_EQ(parsed_event.created_at, event.created_at);
    EXPECT_EQ(parsed_event.payload, event.payload);

    SessionState session = make_session_state();
    session.messages.push_back(nlohmann::json{{"role", "user"}, {"content", "bootstrap state"}});
    session.scratchpad = "state skeleton";
    session.plan = plan;
    session.plan_raw_response = nlohmann::json{{"role", "assistant"}, {"content", "{\"plan\":{}}"}};
    session.plan_validated_artifact = nlohmann::json{{"plan", {{"summary", plan.summary}, {"steps", plan.steps}}}};
    session.plan_validation_errors = {"first attempt invalid"};
    session.plan_normalization_applied = true;
    session.plan_state = "PLAN_READY";
    session.trace.push_back(event);

    const nlohmann::json session_json = session_state_to_json(session);

    SessionState parsed_session;
    std::string err;
    ASSERT_TRUE(session_state_from_json(session_json, &parsed_session, &err)) << err;
    EXPECT_EQ(parsed_session.session_id, session.session_id);
    EXPECT_EQ(parsed_session.messages, session.messages);
    EXPECT_EQ(parsed_session.scratchpad, session.scratchpad);
    EXPECT_FALSE(parsed_session.needs_plan);
    EXPECT_EQ(parsed_session.plan.generation, plan.generation);
    EXPECT_EQ(parsed_session.plan.summary, plan.summary);
    ASSERT_EQ(parsed_session.plan.steps.size(), 1u);
    EXPECT_EQ(parsed_session.plan.steps[0].status, "in_progress");
    EXPECT_EQ(parsed_session.plan_raw_response, session.plan_raw_response);
    EXPECT_EQ(parsed_session.plan_validated_artifact, session.plan_validated_artifact);
    ASSERT_EQ(parsed_session.plan_validation_errors.size(), 1u);
    EXPECT_EQ(parsed_session.plan_validation_errors[0], "first attempt invalid");
    EXPECT_TRUE(parsed_session.plan_normalization_applied);
    EXPECT_EQ(parsed_session.plan_state, "PLAN_READY");
    ASSERT_EQ(parsed_session.trace.size(), 1u);
    EXPECT_EQ(parsed_session.trace[0].kind, "note");
    EXPECT_EQ(parsed_session.trace[0].payload, event.payload);
}

TEST(StateModelTest, PlanRepairAttemptsJsonRoundTripAndDefaulting) {
    SessionState session = make_session_state();

    nlohmann::json encoded = session_state_to_json(session);
    EXPECT_EQ(encoded.value("plan_repair_attempts", -1), 0);

    encoded["plan_repair_attempts"] = 1;
    SessionState parsed;
    std::string err;
    ASSERT_TRUE(session_state_from_json(encoded, &parsed, &err)) << err;

    const nlohmann::json reparsed = session_state_to_json(parsed);
    EXPECT_EQ(reparsed.value("plan_repair_attempts", -1), 1);

    encoded.erase("plan_repair_attempts");
    SessionState defaulted;
    ASSERT_TRUE(session_state_from_json(encoded, &defaulted, &err)) << err;

    const nlohmann::json reencoded_defaulted = session_state_to_json(defaulted);
    EXPECT_EQ(reencoded_defaulted.value("plan_repair_attempts", -1), 0);
}

TEST(StateModelTest, PlanStepStatusValidationAcceptsOnlySupportedValues) {
    EXPECT_TRUE(is_valid_plan_step_status("pending"));
    EXPECT_TRUE(is_valid_plan_step_status("in_progress"));
    EXPECT_TRUE(is_valid_plan_step_status("completed"));
    EXPECT_TRUE(is_valid_plan_step_status("failed"));
    EXPECT_FALSE(is_valid_plan_step_status("blocked"));
    EXPECT_FALSE(is_valid_plan_step_status(""));

    const nlohmann::json valid_json = nlohmann::json{{"status", "completed"}};
    const PlanStep valid = valid_json.get<PlanStep>();
    EXPECT_EQ(valid.status, "completed");

    const nlohmann::json invalid_json = nlohmann::json{{"status", "blocked"}};
    EXPECT_THROW((void)invalid_json.get<PlanStep>(), std::invalid_argument);
}

TEST(StateModelTest, EmptyPlanAndTraceRoundTripRemainEmpty) {
    SessionState session = make_session_state();

    const nlohmann::json json_value = session_state_to_json(session);
    ASSERT_TRUE(json_value.contains("plan"));
    ASSERT_TRUE(json_value.contains("trace"));
    EXPECT_TRUE(json_value.at("plan").is_object());
    EXPECT_TRUE(json_value.at("plan").at("steps").is_array());
    EXPECT_TRUE(json_value.at("plan").at("steps").empty());
    EXPECT_TRUE(json_value.at("trace").is_array());
    EXPECT_TRUE(json_value.at("trace").empty());

    SessionState parsed;
    std::string err;
    ASSERT_TRUE(session_state_from_json(json_value, &parsed, &err)) << err;
    EXPECT_EQ(parsed.plan.generation, 0);
    EXPECT_TRUE(parsed.plan.summary.empty());
    EXPECT_TRUE(parsed.plan.steps.empty());
    EXPECT_TRUE(parsed.trace.empty());
}

TEST(StateModelTest, MissingPlanGenerationDefaultsToZero) {
    SessionState session = make_session_state();
    session.plan.summary = "legacy plan";

    nlohmann::json json_value = session_state_to_json(session);
    json_value["plan"].erase("generation");

    SessionState parsed;
    std::string err;
    ASSERT_TRUE(session_state_from_json(json_value, &parsed, &err)) << err;
    EXPECT_EQ(parsed.plan.generation, 0);
    EXPECT_EQ(parsed.plan.summary, "legacy plan");
}

TEST(StateModelTest, SessionStateToJsonRejectsInvalidPlanStepStatus) {
    SessionState session = make_session_state();
    session.plan.steps.push_back(PlanStep{
        .id = "step-1",
        .title = "invalid",
        .status = "blocked",
    });

    EXPECT_THROW((void)session_state_to_json(session), std::invalid_argument);
}

TEST(StateModelTest, SessionStateToJsonRejectsInvalidPlanMetadata) {
    SessionState session = make_session_state();
    session.plan.metadata = nlohmann::json::array({"bad"});

    EXPECT_THROW((void)session_state_to_json(session), std::invalid_argument);
}

TEST(StateModelTest, SessionStateToJsonRejectsInvalidTracePayload) {
    SessionState session = make_session_state();
    session.trace.push_back(TraceEvent{
        .kind = "note",
        .message = "bad payload",
        .created_at = "2026-03-25T12:00:00.000Z",
        .payload = nlohmann::json::array({"bad"})
    });

    EXPECT_THROW((void)session_state_to_json(session), std::invalid_argument);
}

TEST(StateModelTest, SessionStateToJsonRejectsNegativePlanGeneration) {
    SessionState session = make_session_state();
    session.plan.generation = -1;

    EXPECT_THROW((void)session_state_to_json(session), std::invalid_argument);
}

TEST(StateModelTest, LegacyActivePlanBackfillsPlanReadyContractState) {
    SessionState session = make_session_state();
    session.needs_plan = true;
    session.plan.summary = "legacy active plan";
    session.plan.steps.push_back(PlanStep{
        .id = "step-1",
        .title = "resume execution",
        .status = "in_progress",
        .detail = "still running"
    });
    session.plan.current_step_index = 0;
    session.plan.outcome = "in_progress";

    nlohmann::json json_value = session_state_to_json(session);
    json_value.erase("plan_raw_response");
    json_value.erase("plan_validated_artifact");
    json_value.erase("plan_validation_errors");
    json_value.erase("plan_normalization_applied");
    json_value.erase("plan_state");

    SessionState parsed;
    std::string err;
    ASSERT_TRUE(session_state_from_json(json_value, &parsed, &err)) << err;
    EXPECT_EQ(parsed.plan_state, "PLAN_READY");
    EXPECT_TRUE(parsed.plan_raw_response.empty());
    EXPECT_TRUE(parsed.plan_validation_errors.empty());
    EXPECT_FALSE(parsed.plan_normalization_applied);
    ASSERT_TRUE(parsed.plan_validated_artifact.contains("plan"));
    EXPECT_EQ(parsed.plan_validated_artifact["plan"].value("summary", ""), "legacy active plan");
    ASSERT_TRUE(parsed.plan_validated_artifact["plan"].contains("steps"));
    ASSERT_EQ(parsed.plan_validated_artifact["plan"]["steps"].size(), 1u);
}

TEST(StateModelTest, ResetSessionPlanClearsPlannerContractFields) {
    SessionState session = make_session_state();
    session.plan.summary = "stale";
    session.plan.steps.push_back(PlanStep{
        .id = "step-1",
        .title = "stale step",
        .status = "in_progress"
    });
    session.plan.current_step_index = 0;
    session.plan.outcome = "in_progress";
    session.plan_raw_response = nlohmann::json{{"role", "assistant"}};
    session.plan_validated_artifact = nlohmann::json{{"plan", {{"steps", nlohmann::json::array()}}}};
    session.plan_validation_errors = {"bad contract"};
    session.plan_normalization_applied = true;
    session.plan_state = "PLAN_READY";

    const int previous_generation = session.plan.generation;
    reset_session_plan(session);

    EXPECT_EQ(session.plan.generation, previous_generation + 1);
    EXPECT_TRUE(session.plan.summary.empty());
    EXPECT_TRUE(session.plan.steps.empty());
    EXPECT_EQ(session.plan.current_step_index, -1);
    EXPECT_TRUE(session.plan.outcome.empty());
    EXPECT_TRUE(session.plan_raw_response.empty());
    EXPECT_TRUE(session.plan_validated_artifact.empty());
    EXPECT_TRUE(session.plan_validation_errors.empty());
    EXPECT_FALSE(session.plan_normalization_applied);
    EXPECT_EQ(session.plan_state, "IDLE");
}

}  // namespace
