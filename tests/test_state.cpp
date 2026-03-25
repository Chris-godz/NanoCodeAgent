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
    session.trace.push_back(event);

    const nlohmann::json session_json = session_state_to_json(session);

    SessionState parsed_session;
    std::string err;
    ASSERT_TRUE(session_state_from_json(session_json, &parsed_session, &err)) << err;
    EXPECT_EQ(parsed_session.session_id, session.session_id);
    EXPECT_EQ(parsed_session.messages, session.messages);
    EXPECT_EQ(parsed_session.scratchpad, session.scratchpad);
    EXPECT_EQ(parsed_session.plan.generation, plan.generation);
    EXPECT_EQ(parsed_session.plan.summary, plan.summary);
    ASSERT_EQ(parsed_session.plan.steps.size(), 1u);
    EXPECT_EQ(parsed_session.plan.steps[0].status, "in_progress");
    ASSERT_EQ(parsed_session.trace.size(), 1u);
    EXPECT_EQ(parsed_session.trace[0].kind, "note");
    EXPECT_EQ(parsed_session.trace[0].payload, event.payload);
}

TEST(StateModelTest, PlanStepStatusValidationAcceptsOnlySupportedValues) {
    EXPECT_TRUE(is_valid_plan_step_status("pending"));
    EXPECT_TRUE(is_valid_plan_step_status("in_progress"));
    EXPECT_TRUE(is_valid_plan_step_status("completed"));
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

}  // namespace
