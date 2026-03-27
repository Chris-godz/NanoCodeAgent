#include <gtest/gtest.h>
#include "llm.hpp"
#include "sse_parser.hpp"

namespace {

nlohmann::json planner_repair_messages() {
    return nlohmann::json::array({
        {
            {"role", "system"},
            {"content", "System prompt"}
        },
        {
            {"role", "assistant"},
            {"content", "{\"plan\":{\"steps\":{}}}"}
        },
        {
            {"role", "system"},
            {"content", "Planner contract repair required.\nReturn ONLY one JSON object.\nNo markdown.\n"}
        }
    });
}

nlohmann::json regular_messages() {
    return nlohmann::json::array({
        {
            {"role", "system"},
            {"content", "System prompt"}
        },
        {
            {"role", "user"},
            {"content", "Do some work"}
        }
    });
}

nlohmann::json sample_tools() {
    return nlohmann::json::array({
        {
            {"type", "function"},
            {"function", {
                {"name", "write_file"},
                {"parameters", {
                    {"type", "object"}
                }}
            }}
        }
    });
}

} // namespace

TEST(LlmStreamTest, AccumulateContentCallbacks) {
    SseParser parser;
    std::string err;
    std::string accumulated;
    
    std::function<bool(const std::string&)> callback = [&accumulated](const std::string& content) -> bool {
        accumulated += content;
        return true;
    };

    // First Payload Chunk
    std::string chunk1 = "data: {\"choices\":[{\"delta\":{\"content\":\"he\"}}]}\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk1, parser, callback, nullptr, &err));
    EXPECT_EQ(accumulated, "he");

    // Second Payload Chunk
    std::string chunk2 = "data: {\"choices\":[{\"delta\":{\"content\":\"llo\"}}]}\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk2, parser, callback, nullptr, &err));
    EXPECT_EQ(accumulated, "hello");

    // Finished flag Chunk
    std::string chunk3 = "data: [DONE]\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk3, parser, callback, nullptr, &err));
}

TEST(LlmStreamTest, ParseErrorPayload) {
    SseParser parser;
    std::string err;
    std::function<bool(const std::string&)> callback = [](const std::string& content) -> bool { return true; };

    // API Error Payload
    std::string chunk = "data: {\"error\":{\"message\":\"API error limit\"}}\n\n";
    EXPECT_FALSE(llm_stream_process_chunk(chunk, parser, callback, nullptr, &err));
    EXPECT_EQ(err, "API Error: API error limit");
}

TEST(LlmStreamTest, BadJsonFailsFast) {
    SseParser parser;
    std::string err;
    std::function<bool(const std::string&)> callback = [](const std::string& content) -> bool { return true; };

    // Bad Formatted JSON
    std::string chunk = "data: {invalid json}\n\n";
    // Based on Day 10 fail-fast requirement, bad JSON chunks should fail fast instead of ignoring.
    EXPECT_FALSE(llm_stream_process_chunk(chunk, parser, callback, nullptr, &err));
}

TEST(LlmStreamTest, IgnoreControlChunks) {
    SseParser parser;
    std::string err;
    std::string accumulated;
    std::function<bool(const std::string&)> callback = [&accumulated](const std::string& content) -> bool {
        accumulated += content;
        return true;
    };

    // Control chunk: Only tells role without content
    std::string chunk = "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\"}}]}\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk, parser, callback, nullptr, &err));
    EXPECT_EQ(accumulated, ""); // Ignored successfully
}

TEST(LlmStreamTest, AbortFromUserCallback) {
    SseParser parser;
    std::string err;
    std::function<bool(const std::string&)> callback = [](const std::string& content) -> bool {
        return false; // Signal abort immediately
    };

    std::string chunk = "data: {\"choices\":[{\"delta\":{\"content\":\"hi\"}}]}\n\n";
    EXPECT_FALSE(llm_stream_process_chunk(chunk, parser, callback, nullptr, &err));
}

TEST(LlmStreamTest, CapturesUsagePayloadFromStreamChunk) {
    SseParser parser;
    std::string err;
    nlohmann::json usage;
    std::function<bool(const std::string&)> callback = [](const std::string&) -> bool { return true; };

    std::string chunk =
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":11,\"completion_tokens\":7,\"total_tokens\":18}}\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk, parser, callback, nullptr, &err, &usage));
    EXPECT_TRUE(usage.is_object());
    EXPECT_EQ(usage.value("prompt_tokens", -1), 11);
    EXPECT_EQ(usage.value("completion_tokens", -1), 7);
    EXPECT_EQ(usage.value("total_tokens", -1), 18);
}

TEST(LlmStreamTest, DetectsPlannerRepairTurnFromLastSystemMessage) {
    EXPECT_TRUE(llm_is_planner_repair_turn(planner_repair_messages()));
    EXPECT_FALSE(llm_is_planner_repair_turn(regular_messages()));
}

TEST(LlmStreamTest, AutoRepairModeUsesStructuredForDeepSeekProfile) {
    AgentConfig config;
    config.model = "deepseek-chat";
    config.base_url = "https://api.deepseek.com";
    config.planner_repair_mode = "auto";

    const PlannerRepairModeSelection selection = llm_resolve_planner_repair_mode(config);
    EXPECT_EQ(selection.requested_mode, "auto");
    EXPECT_EQ(selection.effective_mode, "structured");
    EXPECT_EQ(selection.reason, "provider_profile_default_deepseek_structured");

    const nlohmann::json request =
        llm_build_chat_completion_request(config, planner_repair_messages(), sample_tools());
    ASSERT_TRUE(request.contains("response_format"));
    EXPECT_EQ(request.at("response_format").value("type", ""), "json_object");
}

TEST(LlmStreamTest, AutoRepairModeFallsBackToBaselineForNonDeepSeekProfile) {
    AgentConfig config;
    config.model = "claude-3-opus";
    config.base_url = "https://api.anthropic.com";
    config.planner_repair_mode = "auto";

    const PlannerRepairModeSelection selection = llm_resolve_planner_repair_mode(config);
    EXPECT_EQ(selection.requested_mode, "auto");
    EXPECT_EQ(selection.effective_mode, "baseline");
    EXPECT_EQ(selection.reason, "provider_profile_default_baseline_non_deepseek");

    const nlohmann::json request =
        llm_build_chat_completion_request(config, planner_repair_messages(), sample_tools());
    EXPECT_FALSE(request.contains("response_format"));
    EXPECT_FALSE(request.contains("tools"));
    EXPECT_FALSE(request.contains("tool_choice"));
}

TEST(LlmStreamTest, ExplicitStructuredFallsBackWhenProviderCapabilityUnknown) {
    AgentConfig config;
    config.model = "claude-3-opus";
    config.base_url = "https://api.anthropic.com";
    config.planner_repair_mode = "structured";

    const PlannerRepairModeSelection selection = llm_resolve_planner_repair_mode(config);
    EXPECT_EQ(selection.requested_mode, "structured");
    EXPECT_EQ(selection.effective_mode, "baseline");
    EXPECT_EQ(selection.reason, "explicit_structured_fallback_baseline_provider_capability_unknown");

    const nlohmann::json request =
        llm_build_chat_completion_request(config, planner_repair_messages(), sample_tools());
    EXPECT_FALSE(request.contains("response_format"));
    EXPECT_FALSE(request.contains("tools"));
    EXPECT_FALSE(request.contains("tool_choice"));
}

TEST(LlmStreamTest, BaselineRepairRequestDropsRegularTools) {
    AgentConfig config;
    config.model = "gpt-4o";
    config.planner_repair_mode = "baseline";

    const nlohmann::json request =
        llm_build_chat_completion_request(config, planner_repair_messages(), sample_tools());

    EXPECT_FALSE(request.contains("tools"));
    EXPECT_FALSE(request.contains("response_format"));
    EXPECT_FALSE(request.contains("tool_choice"));
}

TEST(LlmStreamTest, StructuredRepairRequestAddsJsonObjectResponseFormat) {
    AgentConfig config;
    config.model = "gpt-4o";
    config.planner_repair_mode = "structured";

    const nlohmann::json request =
        llm_build_chat_completion_request(config, planner_repair_messages(), sample_tools());

    EXPECT_FALSE(request.contains("tools"));
    ASSERT_TRUE(request.contains("response_format"));
    EXPECT_EQ(request.at("response_format").value("type", ""), "json_object");
}

TEST(LlmStreamTest, ArtifactEnvelopeRepairRequestUsesSyntheticTool) {
    AgentConfig config;
    config.model = "gpt-4o";
    config.planner_repair_mode = "artifact_envelope";

    const nlohmann::json request =
        llm_build_chat_completion_request(config, planner_repair_messages(), sample_tools());

    ASSERT_TRUE(request.contains("tools"));
    ASSERT_TRUE(request.at("tools").is_array());
    ASSERT_EQ(request.at("tools").size(), 1u);
    EXPECT_EQ(request.at("tools")[0].at("function").value("name", ""), "planner_repair_artifact");
    ASSERT_TRUE(request.contains("tool_choice"));
    EXPECT_EQ(request.at("tool_choice").at("function").value("name", ""), "planner_repair_artifact");
    EXPECT_FALSE(request.contains("response_format"));
}

TEST(LlmStreamTest, NonRepairRequestKeepsRegularTools) {
    AgentConfig config;
    config.model = "gpt-4o";
    config.planner_repair_mode = "structured";

    const nlohmann::json request =
        llm_build_chat_completion_request(config, regular_messages(), sample_tools());

    ASSERT_TRUE(request.contains("tools"));
    ASSERT_EQ(request.at("tools").size(), 1u);
    EXPECT_FALSE(request.contains("response_format"));
    EXPECT_FALSE(request.contains("tool_choice"));
}

TEST(LlmStreamTest, ArtifactEnvelopeMaterializesPlannerJsonAsAssistantContent) {
    AgentConfig config;
    config.model = "gpt-4o";
    config.planner_repair_mode = "artifact_envelope";

    ToolCall tool_call;
    tool_call.id = "call_1";
    tool_call.name = "planner_repair_artifact";
    tool_call.arguments = nlohmann::json{
        {"plan", {
            {"summary", "repaired plan"},
            {"steps", nlohmann::json::array({
                {
                    {"id", "step-1"},
                    {"title", "inspect"},
                    {"detail", "inspect the broken planner output"}
                }
            })}
        }}
    };

    const nlohmann::json response = llm_materialize_assistant_message(
        config,
        planner_repair_messages(),
        "",
        {tool_call},
        nlohmann::json{{"total_tokens", 12}});

    ASSERT_TRUE(response.contains("content"));
    EXPECT_FALSE(response.contains("tool_calls"));
    EXPECT_EQ(nlohmann::json::parse(response.at("content").get<std::string>()), tool_call.arguments);
    ASSERT_TRUE(response.contains("usage"));
    EXPECT_EQ(response.at("usage").value("total_tokens", -1), 12);
}
