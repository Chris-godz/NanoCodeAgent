#pragma once
#include "config.hpp"
#include "tool_call.hpp"
#include <string>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>

class SseParser;
class ToolCallAssembler;

struct PlannerRepairModeSelection {
    std::string requested_mode;
    std::string effective_mode;
    std::string reason;
};

// Parse API JSON response (legacy non-streaming)
bool llm_parse_response(const std::string& json_resp, std::string* out_text, std::string* err);

// Exposed for Mock Tests: Parse chunk and call the content callback.
bool llm_stream_process_chunk(const std::string& chunk,
                              SseParser& parser,
                              const std::function<bool(const std::string&)>& on_content_delta,
                              ToolCallAssembler* tool_asm,
                              std::string* err,
                              nlohmann::json* usage = nullptr);

// Exposed for request-shaping and artifact-envelope unit tests.
bool llm_is_planner_repair_turn(const nlohmann::json& messages);
bool llm_provider_profile_is_deepseek(const AgentConfig& cfg);
PlannerRepairModeSelection llm_resolve_planner_repair_mode(const AgentConfig& cfg);
nlohmann::json llm_build_chat_completion_request(const AgentConfig& cfg,
                                                 const nlohmann::json& messages,
                                                 const nlohmann::json& tools);
nlohmann::json llm_materialize_assistant_message(const AgentConfig& cfg,
                                                 const nlohmann::json& messages,
                                                 const std::string& full_content,
                                                 const std::vector<ToolCall>& tool_calls,
                                                 const nlohmann::json& usage = nlohmann::json::object());

// Day 10 SSE Streaming API for Agent
// Returns a materialised JSON response message representing the assistant's turn (with content/tool_calls)
// Throws std::runtime_error on network errors or parsing failure.
nlohmann::json llm_chat_completion_stream(
    const AgentConfig& cfg, 
    const nlohmann::json& messages,
    const nlohmann::json& tools,
    std::function<bool(const std::string&)> on_content_delta
);
