#include "state.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

constexpr int kSessionStateSchemaVersion = 1;

std::atomic<std::uint64_t> g_session_counter{0};

std::string fallback_tool_call_id(const ToolCall& call) {
    if (!call.id.empty()) {
        return call.id;
    }
    return "call_" + std::to_string(call.index);
}

bool read_string_field(const nlohmann::json& json_value,
                       const char* key,
                       std::string* out,
                       std::string* err,
                       bool required = false) {
    if (!json_value.contains(key)) {
        if (required && err) {
            *err = std::string("Missing required string field '") + key + "'.";
        }
        return !required;
    }
    if (!json_value.at(key).is_string()) {
        if (err) {
            *err = std::string("Field '") + key + "' must be a string.";
        }
        return false;
    }
    if (out) {
        *out = json_value.at(key).get<std::string>();
    }
    return true;
}

bool read_int_field(const nlohmann::json& json_value,
                    const char* key,
                    int* out,
                    std::string* err,
                    bool required = false) {
    if (!json_value.contains(key)) {
        if (required && err) {
            *err = std::string("Missing required integer field '") + key + "'.";
        }
        return !required;
    }
    const auto& field = json_value.at(key);
    if (!field.is_number_integer()) {
        if (err) {
            *err = std::string("Field '") + key + "' must be an integer.";
        }
        return false;
    }
    const int value = field.get<int>();
    if (value < 0) {
        if (err) {
            *err = std::string("Field '") + key + "' must be non-negative.";
        }
        return false;
    }
    if (out) {
        *out = value;
    }
    return true;
}

bool parse_tool_call_record(const nlohmann::json& json_value, ToolCallRecord* out, std::string* err) {
    if (!json_value.is_object()) {
        if (err) {
            *err = "ToolCallRecord entries must be JSON objects.";
        }
        return false;
    }

    ToolCallRecord record;
    if (!read_int_field(json_value, "turn_index", &record.turn_index, err, true) ||
        !read_string_field(json_value, "tool_call_id", &record.tool_call_id, err, true) ||
        !read_string_field(json_value, "tool_name", &record.tool_name, err, true) ||
        !read_string_field(json_value, "status", &record.status, err, true) ||
        !read_string_field(json_value, "started_at", &record.started_at, err, true)) {
        return false;
    }

    if (json_value.contains("finished_at")) {
        if (!json_value.at("finished_at").is_string()) {
            if (err) {
                *err = "Field 'finished_at' must be a string.";
            }
            return false;
        }
        record.finished_at = json_value.at("finished_at").get<std::string>();
    }

    if (json_value.contains("arguments")) {
        record.arguments = json_value.at("arguments");
    }

    if (out) {
        *out = std::move(record);
    }
    return true;
}

bool parse_observation_record(const nlohmann::json& json_value, ObservationRecord* out, std::string* err) {
    if (!json_value.is_object()) {
        if (err) {
            *err = "ObservationRecord entries must be JSON objects.";
        }
        return false;
    }

    ObservationRecord record;
    if (!read_int_field(json_value, "turn_index", &record.turn_index, err, true) ||
        !read_string_field(json_value, "tool_call_id", &record.tool_call_id, err, true) ||
        !read_string_field(json_value, "tool_name", &record.tool_name, err, true) ||
        !read_string_field(json_value, "content", &record.content, err, true) ||
        !read_string_field(json_value, "created_at", &record.created_at, err, true)) {
        return false;
    }

    if (out) {
        *out = std::move(record);
    }
    return true;
}

bool parse_mcp_server_record(const nlohmann::json& json_value, McpServerRecord* out, std::string* err) {
    if (!json_value.is_object()) {
        if (err) {
            *err = "McpServerRecord entries must be JSON objects.";
        }
        return false;
    }

    McpServerRecord record;
    if (!read_string_field(json_value, "server_name", &record.server_name, err, true) ||
        !read_string_field(json_value,
                           "negotiated_protocol_version",
                           &record.negotiated_protocol_version,
                           err,
                           true)) {
        return false;
    }
    if (json_value.contains("command") &&
        !read_string_field(json_value, "command", nullptr, err, false)) {
        return false;
    }

    if (json_value.contains("capabilities")) {
        if (!json_value.at("capabilities").is_object()) {
            if (err) {
                *err = "Field 'capabilities' must be an object.";
            }
            return false;
        }
        record.capabilities = json_value.at("capabilities");
    }

    if (json_value.contains("tool_cache")) {
        if (!json_value.at("tool_cache").is_array()) {
            if (err) {
                *err = "Field 'tool_cache' must be an array.";
            }
            return false;
        }
        record.tool_cache = json_value.at("tool_cache");
    }

    if (out) {
        *out = std::move(record);
    }
    return true;
}

bool parse_mcp_tool_call_observation_record(const nlohmann::json& json_value,
                                            McpToolCallObservationRecord* out,
                                            std::string* err) {
    if (!json_value.is_object()) {
        if (err) {
            *err = "McpToolCallObservationRecord entries must be JSON objects.";
        }
        return false;
    }

    McpToolCallObservationRecord record;
    if (!read_int_field(json_value, "turn_index", &record.turn_index, err, true) ||
        !read_string_field(json_value, "tool_call_id", &record.tool_call_id, err, true) ||
        !read_string_field(json_value, "server_name", &record.server_name, err, true) ||
        !read_string_field(json_value, "tool_name", &record.tool_name, err, true) ||
        !read_string_field(json_value, "status", &record.status, err, true) ||
        !read_string_field(json_value, "created_at", &record.created_at, err, true)) {
        return false;
    }

    if (json_value.contains("result")) {
        if (!json_value.at("result").is_object()) {
            if (err) {
                *err = "Field 'result' must be an object.";
            }
            return false;
        }
        record.result = json_value.at("result");
    }

    if (out) {
        *out = std::move(record);
    }
    return true;
}

bool validate_messages_array(const nlohmann::json& messages, std::string* err) {
    if (!messages.is_array()) {
        if (err) {
            *err = "Field 'messages' must be an array.";
        }
        return false;
    }

    for (std::size_t i = 0; i < messages.size(); ++i) {
        const auto& message = messages[i];
        if (!message.is_object()) {
            if (err) {
                *err = "Messages entries must be JSON objects.";
            }
            return false;
        }
        if (!message.contains("role") || !message.at("role").is_string()) {
            if (err) {
                *err = "Messages entries must include a string 'role'.";
            }
            return false;
        }
    }

    return true;
}

std::string make_session_id() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const auto counter = g_session_counter.fetch_add(1, std::memory_order_relaxed);
    return "session-" + std::to_string(millis) + "-" + std::to_string(counter);
}

}  // namespace

std::string state_now_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.'
        << std::setw(3) << std::setfill('0') << millis << 'Z';
    return oss.str();
}

SessionState make_session_state() {
    SessionState session;
    session.session_id = make_session_id();
    session.created_at = state_now_timestamp();
    session.updated_at = session.created_at;
    session.messages = nlohmann::json::array();
    session.active_rules_snapshot = nlohmann::json::object();
    return session;
}

void touch_session(SessionState& session) {
    session.updated_at = state_now_timestamp();
}

void prepare_session_state(SessionState& session,
                           const std::vector<std::string>& active_skills,
                           const nlohmann::json& active_rules_snapshot) {
    if (session.session_id.empty()) {
        session.session_id = make_session_id();
    }
    if (session.created_at.empty()) {
        session.created_at = state_now_timestamp();
    }
    if (!session.messages.is_array()) {
        session.messages = nlohmann::json::array();
    }
    session.active_skills = active_skills;
    session.active_rules_snapshot = active_rules_snapshot.is_object()
                                        ? active_rules_snapshot
                                        : nlohmann::json::object();
    touch_session(session);
}

void seed_session_messages_if_empty(SessionState& session,
                                    const std::string& system_prompt,
                                    const std::string& user_prompt) {
    if (!session.messages.is_array()) {
        session.messages = nlohmann::json::array();
    }
    if (!session.messages.empty()) {
        return;
    }

    if (!system_prompt.empty()) {
        session.messages.push_back({{"role", "system"}, {"content", system_prompt}});
    }
    session.messages.push_back({{"role", "user"}, {"content", user_prompt}});
    touch_session(session);
}

void set_session_scratchpad(SessionState& session, const std::string& scratchpad) {
    session.scratchpad = scratchpad;
    touch_session(session);
}

std::size_t append_tool_call_record(SessionState& session, int turn_index, const ToolCall& call) {
    ToolCallRecord record;
    record.turn_index = turn_index;
    record.tool_call_id = fallback_tool_call_id(call);
    record.tool_name = call.name;
    record.arguments = call.arguments;
    record.status = "started";
    record.started_at = state_now_timestamp();
    session.tool_calls.push_back(std::move(record));
    touch_session(session);
    return session.tool_calls.size() - 1;
}

void finish_tool_call_record(SessionState& session, std::size_t record_index, const std::string& status) {
    if (record_index >= session.tool_calls.size()) {
        return;
    }
    session.tool_calls[record_index].status = status;
    session.tool_calls[record_index].finished_at = state_now_timestamp();
    touch_session(session);
}

void append_observation_record(SessionState& session,
                               int turn_index,
                               const std::string& tool_call_id,
                               const std::string& tool_name,
                               const std::string& content) {
    ObservationRecord record;
    record.turn_index = turn_index;
    record.tool_call_id = tool_call_id;
    record.tool_name = tool_name;
    record.content = content;
    record.created_at = state_now_timestamp();
    session.observations.push_back(std::move(record));
    ++session.counters.observations;
    touch_session(session);
}

void set_session_mcp_servers(SessionState& session, const std::vector<McpServerRecord>& mcp_servers) {
    session.mcp_servers = mcp_servers;
    touch_session(session);
}

void append_mcp_tool_call_observation(SessionState& session,
                                      int turn_index,
                                      const std::string& tool_call_id,
                                      const std::string& server_name,
                                      const std::string& tool_name,
                                      const std::string& status,
                                      const nlohmann::json& result) {
    McpToolCallObservationRecord record;
    record.turn_index = turn_index;
    record.tool_call_id = tool_call_id;
    record.server_name = server_name;
    record.tool_name = tool_name;
    record.status = status;
    record.result = result.is_object() ? result : nlohmann::json::object();
    record.created_at = state_now_timestamp();
    session.mcp_tool_call_observations.push_back(std::move(record));
    touch_session(session);
}

std::string tool_call_status_from_output(const std::string& output) {
    try {
        const nlohmann::json parsed = nlohmann::json::parse(output);
        if (!parsed.is_object()) {
            return "invalid_output";
        }
        const std::string status = parsed.value("status", "");
        if (!status.empty()) {
            return status;
        }
        if (parsed.value("timed_out", false)) {
            return "timed_out";
        }
        return parsed.value("ok", true) ? "ok" : "failed";
    } catch (...) {
        return "invalid_output";
    }
}

nlohmann::json make_active_rules_snapshot(const AgentConfig& config) {
    return nlohmann::json{
        {"mode", config.mode},
        {"allow_mutating_tools", config.allow_mutating_tools},
        {"allow_execution_tools", config.allow_execution_tools},
        {"max_turns", config.max_turns},
        {"max_tool_calls_per_turn", config.max_tool_calls_per_turn},
        {"max_total_tool_calls", config.max_total_tool_calls},
        {"max_tool_output_bytes", config.max_tool_output_bytes},
        {"max_context_bytes", config.max_context_bytes},
        {"max_skill_prompt_bytes", config.max_skill_prompt_bytes}
    };
}

bool session_state_from_json(const nlohmann::json& json_value, SessionState* out, std::string* err) {
    if (!out) {
        if (err) {
            *err = "Session parsing requires a non-null output object.";
        }
        return false;
    }
    if (!json_value.is_object()) {
        if (err) {
            *err = "Session state must be a JSON object.";
        }
        return false;
    }

    if (json_value.contains("schema_version")) {
        int schema_version = 0;
        if (!read_int_field(json_value, "schema_version", &schema_version, err, true)) {
            return false;
        }
        if (schema_version != kSessionStateSchemaVersion) {
            if (err) {
                *err = "Unsupported session schema_version: " + std::to_string(schema_version);
            }
            return false;
        }
    }

    SessionState session = make_session_state();
    if (json_value.contains("session_id") &&
        !read_string_field(json_value, "session_id", &session.session_id, err, true)) {
        return false;
    }
    if (json_value.contains("created_at") &&
        !read_string_field(json_value, "created_at", &session.created_at, err, true)) {
        return false;
    }
    if (json_value.contains("updated_at") &&
        !read_string_field(json_value, "updated_at", &session.updated_at, err, true)) {
        return false;
    }
    if (json_value.contains("turn_index") &&
        !read_int_field(json_value, "turn_index", &session.turn_index, err, true)) {
        return false;
    }

    if (json_value.contains("messages")) {
        if (!validate_messages_array(json_value.at("messages"), err)) {
            return false;
        }
        session.messages = json_value.at("messages");
    }

    if (json_value.contains("tool_calls")) {
        const auto& tool_calls = json_value.at("tool_calls");
        if (!tool_calls.is_array()) {
            if (err) {
                *err = "Field 'tool_calls' must be an array.";
            }
            return false;
        }
        session.tool_calls.clear();
        session.tool_calls.reserve(tool_calls.size());
        for (const auto& item : tool_calls) {
            ToolCallRecord record;
            if (!parse_tool_call_record(item, &record, err)) {
                return false;
            }
            session.tool_calls.push_back(std::move(record));
        }
    }

    if (json_value.contains("observations")) {
        const auto& observations = json_value.at("observations");
        if (!observations.is_array()) {
            if (err) {
                *err = "Field 'observations' must be an array.";
            }
            return false;
        }
        session.observations.clear();
        session.observations.reserve(observations.size());
        for (const auto& item : observations) {
            ObservationRecord record;
            if (!parse_observation_record(item, &record, err)) {
                return false;
            }
            session.observations.push_back(std::move(record));
        }
    }

    if (json_value.contains("mcp_servers")) {
        const auto& mcp_servers = json_value.at("mcp_servers");
        if (!mcp_servers.is_array()) {
            if (err) {
                *err = "Field 'mcp_servers' must be an array.";
            }
            return false;
        }
        session.mcp_servers.clear();
        session.mcp_servers.reserve(mcp_servers.size());
        for (const auto& item : mcp_servers) {
            McpServerRecord record;
            if (!parse_mcp_server_record(item, &record, err)) {
                return false;
            }
            session.mcp_servers.push_back(std::move(record));
        }
    }

    if (json_value.contains("mcp_tool_call_observations")) {
        const auto& observations = json_value.at("mcp_tool_call_observations");
        if (!observations.is_array()) {
            if (err) {
                *err = "Field 'mcp_tool_call_observations' must be an array.";
            }
            return false;
        }
        session.mcp_tool_call_observations.clear();
        session.mcp_tool_call_observations.reserve(observations.size());
        for (const auto& item : observations) {
            McpToolCallObservationRecord record;
            if (!parse_mcp_tool_call_observation_record(item, &record, err)) {
                return false;
            }
            session.mcp_tool_call_observations.push_back(std::move(record));
        }
    }

    if (json_value.contains("counters")) {
        const auto& counters = json_value.at("counters");
        if (!counters.is_object()) {
            if (err) {
                *err = "Field 'counters' must be an object.";
            }
            return false;
        }
        if (counters.contains("llm_turns") &&
            !read_int_field(counters, "llm_turns", &session.counters.llm_turns, err, true)) {
            return false;
        }
        if (counters.contains("tool_calls_requested") &&
            !read_int_field(counters,
                            "tool_calls_requested",
                            &session.counters.tool_calls_requested,
                            err,
                            true)) {
            return false;
        }
        if (counters.contains("observations") &&
            !read_int_field(counters, "observations", &session.counters.observations, err, true)) {
            return false;
        }
    }

    if (json_value.contains("scratchpad") &&
        !read_string_field(json_value, "scratchpad", &session.scratchpad, err, true)) {
        return false;
    }

    if (json_value.contains("active_skills")) {
        const auto& active_skills = json_value.at("active_skills");
        if (!active_skills.is_array()) {
            if (err) {
                *err = "Field 'active_skills' must be an array of strings.";
            }
            return false;
        }
        session.active_skills.clear();
        session.active_skills.reserve(active_skills.size());
        for (const auto& item : active_skills) {
            if (!item.is_string()) {
                if (err) {
                    *err = "Field 'active_skills' must be an array of strings.";
                }
                return false;
            }
            session.active_skills.push_back(item.get<std::string>());
        }
    }

    if (json_value.contains("active_rules_snapshot")) {
        if (!json_value.at("active_rules_snapshot").is_object()) {
            if (err) {
                *err = "Field 'active_rules_snapshot' must be an object.";
            }
            return false;
        }
        session.active_rules_snapshot = json_value.at("active_rules_snapshot");
    }

    *out = std::move(session);
    return true;
}

nlohmann::json session_state_to_json(const SessionState& session) {
    nlohmann::json tool_calls = nlohmann::json::array();
    for (const ToolCallRecord& record : session.tool_calls) {
        tool_calls.push_back({
            {"turn_index", record.turn_index},
            {"tool_call_id", record.tool_call_id},
            {"tool_name", record.tool_name},
            {"arguments", record.arguments},
            {"status", record.status},
            {"started_at", record.started_at},
            {"finished_at", record.finished_at}
        });
    }

    nlohmann::json observations = nlohmann::json::array();
    for (const ObservationRecord& record : session.observations) {
        observations.push_back({
            {"turn_index", record.turn_index},
            {"tool_call_id", record.tool_call_id},
            {"tool_name", record.tool_name},
            {"content", record.content},
            {"created_at", record.created_at}
        });
    }

    nlohmann::json mcp_servers = nlohmann::json::array();
    for (const McpServerRecord& record : session.mcp_servers) {
        mcp_servers.push_back({
            {"server_name", record.server_name},
            {"negotiated_protocol_version", record.negotiated_protocol_version},
            {"capabilities", record.capabilities},
            {"tool_cache", record.tool_cache}
        });
    }

    nlohmann::json mcp_tool_call_observations = nlohmann::json::array();
    for (const McpToolCallObservationRecord& record : session.mcp_tool_call_observations) {
        mcp_tool_call_observations.push_back({
            {"turn_index", record.turn_index},
            {"tool_call_id", record.tool_call_id},
            {"server_name", record.server_name},
            {"tool_name", record.tool_name},
            {"status", record.status},
            {"result", record.result},
            {"created_at", record.created_at}
        });
    }

    return nlohmann::json{
        {"schema_version", kSessionStateSchemaVersion},
        {"session_id", session.session_id},
        {"created_at", session.created_at},
        {"updated_at", session.updated_at},
        {"turn_index", session.turn_index},
        {"messages", session.messages},
        {"tool_calls", tool_calls},
        {"observations", observations},
        {"mcp_servers", mcp_servers},
        {"mcp_tool_call_observations", mcp_tool_call_observations},
        {"counters", {
            {"llm_turns", session.counters.llm_turns},
            {"tool_calls_requested", session.counters.tool_calls_requested},
            {"observations", session.counters.observations}
        }},
        {"scratchpad", session.scratchpad},
        {"active_skills", session.active_skills},
        {"active_rules_snapshot", session.active_rules_snapshot}
    };
}
