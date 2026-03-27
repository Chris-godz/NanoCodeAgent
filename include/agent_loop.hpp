#pragma once

#include "config.hpp"
#include "state.hpp"
#include "trace.hpp"
#include "tool_registry.hpp"
#include <string>
#include <nlohmann/json.hpp>
#include <functional>

// Abstract function type that runs the LLM generation.
// Inputs: config, context messages, tool registry.
// Output: updated response (assistant message) and list of tool calls to execute. 
// Uses a string for content and vector of tool calls.
struct AgentTurnResult {
    std::string content;
    nlohmann::json tool_calls; // Parsed array of tool descriptions ready to respond to
};

// Abstracting LLM chat completion
using LLMStreamFunc = std::function<nlohmann::json(const AgentConfig&, const nlohmann::json&, const nlohmann::json&)>;

// Runs the agent until no tools are requested, it hits limits, or a fatal tool failure stops the run.
void agent_run(const AgentConfig& config, 
               const std::string& system_prompt, 
               const std::string& user_prompt, 
               const nlohmann::json& tools_registry,
               LLMStreamFunc llm_func,
               SessionState* session_state = nullptr,
               const ToolRegistry* tool_registry = nullptr,
               TraceSink* trace_sink = nullptr);

// Exposed for planner repair prompt unit tests.
std::string make_plan_repair_prompt(const AgentConfig& config,
                                    const std::vector<std::string>& errors);
