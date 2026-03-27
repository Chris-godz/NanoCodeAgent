#pragma once

#include "config.hpp"
#include "tool_call.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

struct PlanStep {
    std::string id;
    std::string title;
    std::string status = "pending";
    std::string detail;
    nlohmann::json metadata = nlohmann::json::object();
};

struct Plan {
    int generation = 0;
    std::string summary;
    std::vector<PlanStep> steps;
    nlohmann::json metadata = nlohmann::json::object();
    int current_step_index = -1;
    std::string outcome;
};

struct TraceEvent {
    std::string kind;
    std::string message;
    std::string created_at;
    nlohmann::json payload = nlohmann::json::object();
};

struct ToolCallRecord {
    int turn_index = 0;
    std::string tool_call_id;
    std::string tool_name;
    nlohmann::json arguments = nlohmann::json::object();
    std::string status = "started";
    std::string started_at;
    std::string finished_at;
};

struct ObservationRecord {
    int turn_index = 0;
    std::string tool_call_id;
    std::string tool_name;
    std::string content;
    std::string created_at;
};

struct McpServerRecord {
    std::string server_name;
    std::string negotiated_protocol_version;
    nlohmann::json capabilities = nlohmann::json::object();
    nlohmann::json tool_cache = nlohmann::json::array();
};

struct McpToolCallObservationRecord {
    int turn_index = 0;
    std::string tool_call_id;
    std::string server_name;
    std::string tool_name;
    std::string status;
    nlohmann::json result = nlohmann::json::object();
    std::string created_at;
};

struct SessionCounters {
    int llm_turns = 0;
    int tool_calls_requested = 0;
    int observations = 0;
};

struct SessionState {
    std::string session_id;
    std::string created_at;
    std::string updated_at;
    int turn_index = 0;
    nlohmann::json messages = nlohmann::json::array();
    std::vector<ToolCallRecord> tool_calls;
    std::vector<ObservationRecord> observations;
    std::vector<McpServerRecord> mcp_servers;
    std::vector<McpToolCallObservationRecord> mcp_tool_call_observations;
    SessionCounters counters;
    std::string scratchpad;
    bool needs_plan = false;
    std::vector<std::string> active_skills;
    nlohmann::json active_rules_snapshot = nlohmann::json::object();
    Plan plan;
    nlohmann::json plan_raw_response = nlohmann::json::object();
    nlohmann::json plan_validated_artifact = nlohmann::json::object();
    std::vector<std::string> plan_validation_errors;
    bool plan_normalization_applied = false;
    int plan_repair_attempts = 0;
    std::string plan_state = "IDLE";
    std::vector<TraceEvent> trace;
};

bool is_valid_plan_step_status(std::string_view status);
void to_json(nlohmann::json& json_value, const PlanStep& step);
void from_json(const nlohmann::json& json_value, PlanStep& step);
void to_json(nlohmann::json& json_value, const Plan& plan);
void from_json(const nlohmann::json& json_value, Plan& plan);
void to_json(nlohmann::json& json_value, const TraceEvent& event);
void from_json(const nlohmann::json& json_value, TraceEvent& event);

SessionState make_session_state();
std::string state_now_timestamp();
void touch_session(SessionState& session);
void prepare_session_state(SessionState& session,
                           const std::vector<std::string>& active_skills,
                           const nlohmann::json& active_rules_snapshot);
void reset_session_plan(SessionState& session);
void seed_session_messages_if_empty(SessionState& session,
                                    const std::string& system_prompt,
                                    const std::string& user_prompt);
void set_session_scratchpad(SessionState& session, const std::string& scratchpad);
std::size_t append_tool_call_record(SessionState& session, int turn_index, const ToolCall& call);
std::size_t append_skipped_tool_call_record(SessionState& session, int turn_index, const ToolCall& call);
void finish_tool_call_record(SessionState& session, std::size_t record_index, const std::string& status);
void append_observation_record(SessionState& session,
                               int turn_index,
                               const std::string& tool_call_id,
                               const std::string& tool_name,
                               const std::string& content);
void set_session_mcp_servers(SessionState& session, const std::vector<McpServerRecord>& mcp_servers);
void append_mcp_tool_call_observation(SessionState& session,
                                      int turn_index,
                                      const std::string& tool_call_id,
                                      const std::string& server_name,
                                      const std::string& tool_name,
                                      const std::string& status,
                                      const nlohmann::json& result);
std::string tool_call_status_from_output(const std::string& output);
nlohmann::json make_active_rules_snapshot(const AgentConfig& config);
bool session_state_from_json(const nlohmann::json& json_value, SessionState* out, std::string* err);
nlohmann::json session_state_to_json(const SessionState& session);
