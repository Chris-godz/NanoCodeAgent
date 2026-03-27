#include <gtest/gtest.h>

#include "agent_loop.hpp"

namespace {

void expect_common_repair_constraints(const std::string& prompt) {
    EXPECT_NE(prompt.find("Planner contract repair required."), std::string::npos);
    EXPECT_NE(prompt.find("Return ONLY one JSON object."), std::string::npos);
    EXPECT_NE(prompt.find("No markdown."), std::string::npos);
    EXPECT_NE(prompt.find("No explanation."), std::string::npos);
    EXPECT_NE(prompt.find("plan.steps must be a non-empty array"), std::string::npos);
    EXPECT_NE(prompt.find("id, title, and detail"), std::string::npos);
}

} // namespace

TEST(PlannerRepairPromptTest, Version1UsesStrongPlainTextConstraints) {
    AgentConfig config;
    config.planner_repair_prompt_version = "v1";

    const std::string prompt = make_plan_repair_prompt(
        config,
        {"Planner response field 'plan.steps' must be a non-empty array."});

    expect_common_repair_constraints(prompt);
    EXPECT_NE(prompt.find("Validation errors:"), std::string::npos);
    EXPECT_NE(prompt.find("plan.steps"), std::string::npos);
    EXPECT_EQ(prompt.find("Example valid shape:"), std::string::npos);
    EXPECT_EQ(prompt.find("Minimal legal template:"), std::string::npos);
}

TEST(PlannerRepairPromptTest, Version2IncludesSchemaExample) {
    AgentConfig config;
    config.planner_repair_prompt_version = "v2";

    const std::string prompt = make_plan_repair_prompt(config, {});

    expect_common_repair_constraints(prompt);
    EXPECT_NE(prompt.find("Example valid shape:"), std::string::npos);
    EXPECT_NE(prompt.find("{\"plan\":{\"summary\":\"short summary\""), std::string::npos);
    EXPECT_EQ(prompt.find("Minimal legal template:"), std::string::npos);
}

TEST(PlannerRepairPromptTest, Version3EchoesErrorsAndMinimalTemplate) {
    AgentConfig config;
    config.planner_repair_prompt_version = "v3";

    const std::string prompt = make_plan_repair_prompt(
        config,
        {
            "Planner response field 'plan.steps' must be a non-empty array.",
            "Planner response field 'plan.summary' must be a string when present."
        });

    expect_common_repair_constraints(prompt);
    EXPECT_NE(prompt.find("Repair the previous invalid planner response using the errors below."), std::string::npos);
    EXPECT_NE(prompt.find("Planner response field 'plan.steps' must be a non-empty array."), std::string::npos);
    EXPECT_NE(prompt.find("Planner response field 'plan.summary' must be a string when present."), std::string::npos);
    EXPECT_NE(prompt.find("Minimal legal template:"), std::string::npos);
    EXPECT_NE(prompt.find("{\"plan\":{\"summary\":\"<optional summary>\""), std::string::npos);
}

TEST(PlannerRepairPromptTest, UnknownVersionFallsBackToVersion3) {
    AgentConfig config;
    config.planner_repair_prompt_version = "not-a-real-version";

    const std::string prompt = make_plan_repair_prompt(config, {});

    EXPECT_NE(prompt.find("Minimal legal template:"), std::string::npos);
    EXPECT_EQ(prompt.find("Example valid shape:"), std::string::npos);
}

TEST(PlannerRepairPromptTest, AutoVersionUsesVersion2ForDeepSeekProfile) {
    AgentConfig config;
    config.planner_repair_prompt_version = "auto";
    config.model = "deepseek-chat";
    config.base_url = "https://api.deepseek.com";

    const std::string prompt = make_plan_repair_prompt(config, {});

    EXPECT_NE(prompt.find("Example valid shape:"), std::string::npos);
    EXPECT_EQ(prompt.find("Minimal legal template:"), std::string::npos);
}
