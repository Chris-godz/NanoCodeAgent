#pragma once

#include "config.hpp"
#include "tool_call.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
    SessionCounters counters;
    std::string scratchpad;
    std::vector<std::string> active_skills;
    nlohmann::json active_rules_snapshot = nlohmann::json::object();
};

SessionState make_session_state();
std::string state_now_timestamp();
void touch_session(SessionState& session);
void prepare_session_state(SessionState& session,
                           const std::vector<std::string>& active_skills,
                           const nlohmann::json& active_rules_snapshot);
void seed_session_messages_if_empty(SessionState& session,
                                    const std::string& system_prompt,
                                    const std::string& user_prompt);
void set_session_scratchpad(SessionState& session, const std::string& scratchpad);
std::size_t append_tool_call_record(SessionState& session, int turn_index, const ToolCall& call);
void finish_tool_call_record(SessionState& session, std::size_t record_index, const std::string& status);
void append_observation_record(SessionState& session,
                               int turn_index,
                               const std::string& tool_call_id,
                               const std::string& tool_name,
                               const std::string& content);
std::string tool_call_status_from_output(const std::string& output);
nlohmann::json make_active_rules_snapshot(const AgentConfig& config);
bool session_state_from_json(const nlohmann::json& json_value, SessionState* out, std::string* err);
nlohmann::json session_state_to_json(const SessionState& session);
