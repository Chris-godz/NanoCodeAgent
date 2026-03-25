#include "mcp.hpp"

#include "logger.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace {

constexpr char kProtocolVersion[] = "2024-11-05";
constexpr std::size_t kMaxToolsListPages = 64;

enum class QuoteMode {
    None,
    Single,
    Double,
};

std::string sanitize_identifier(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0 || ch == '_' || ch == '-') {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            sanitized.push_back('_');
        }
    }
    while (!sanitized.empty() && sanitized.front() == '_') {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && sanitized.back() == '_') {
        sanitized.pop_back();
    }
    return sanitized.empty() ? "tool" : sanitized;
}

McpServerRecord make_server_record(const McpServerInfo& info) {
    McpServerRecord record;
    record.server_name = info.server_name;
    record.negotiated_protocol_version = info.negotiated_protocol_version;
    record.capabilities = info.capabilities;
    record.tool_cache = nlohmann::json::array();
    for (const McpToolInfo& tool : info.tools) {
        record.tool_cache.push_back({
            {"remote_name", tool.remote_name},
            {"local_name", tool.local_name},
            {"description", tool.description},
            {"input_schema", tool.input_schema}
        });
    }
    return record;
}

std::string tool_call_id_for_context(const ToolCall& call, const ToolExecutionContext* context) {
    if (context != nullptr && !context->tool_call_id.empty()) {
        return context->tool_call_id;
    }
    if (!call.id.empty()) {
        return call.id;
    }
    return "call_" + std::to_string(call.index);
}

std::string status_from_mcp_result(const nlohmann::json& result) {
    if (!result.is_object()) {
        return "failed";
    }
    return result.value("isError", false) ? "failed" : "ok";
}

nlohmann::json make_capped_bridge_result(const std::string& server_name,
                                         const std::string& remote_tool_name,
                                         const nlohmann::json& raw_result,
                                         size_t output_limit) {
    const bool ok = !raw_result.value("isError", false);
    nlohmann::json response{
        {"ok", ok},
        {"status", ok ? "ok" : "failed"},
        {"server_name", server_name},
        {"tool_name", remote_tool_name},
        {"result", raw_result}
    };

    if (output_limit == 0 || response.dump().size() <= output_limit) {
        return response;
    }

    const std::string raw_dump = raw_result.dump();
    std::string preview;
    size_t preview_limit = std::min(raw_dump.size(), output_limit / 2);
    while (preview_limit > 0) {
        preview = raw_dump.substr(0, preview_limit);
        response = {
            {"ok", ok},
            {"status", ok ? "ok" : "failed"},
            {"server_name", server_name},
            {"tool_name", remote_tool_name},
            {"truncated", true},
            {"result_preview", preview},
            {"error", "MCP tool result exceeded the output cap and was truncated."}
        };
        if (response.dump().size() <= output_limit) {
            return response;
        }
        preview_limit /= 2;
    }

    return {
        {"ok", ok},
        {"status", ok ? "ok" : "failed"},
        {"server_name", server_name},
        {"tool_name", remote_tool_name},
        {"truncated", true},
        {"error", "MCP tool result exceeded the output cap and was truncated."}
    };
}

std::optional<std::vector<std::string>> tokenize_command_args(const std::string& command_text, std::string* err) {
    std::vector<std::string> tokens;
    std::string current;
    QuoteMode quote_mode = QuoteMode::None;
    bool escaping = false;
    bool token_started = false;

    auto finish_token = [&]() {
        if (!token_started) {
            return;
        }
        tokens.push_back(current);
        current.clear();
        token_started = false;
    };

    for (char ch : command_text) {
        if (escaping) {
            current.push_back(ch);
            token_started = true;
            escaping = false;
            continue;
        }

        if (quote_mode == QuoteMode::Single) {
            if (ch == '\'') {
                quote_mode = QuoteMode::None;
            } else {
                current.push_back(ch);
            }
            token_started = true;
            continue;
        }

        if (quote_mode == QuoteMode::Double) {
            if (ch == '"') {
                quote_mode = QuoteMode::None;
            } else if (ch == '\\') {
                escaping = true;
            } else {
                current.push_back(ch);
            }
            token_started = true;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            finish_token();
            continue;
        }

        if (ch == '\'') {
            quote_mode = QuoteMode::Single;
            token_started = true;
            continue;
        }

        if (ch == '"') {
            quote_mode = QuoteMode::Double;
            token_started = true;
            continue;
        }

        if (ch == '\\') {
            escaping = true;
            token_started = true;
            continue;
        }

        current.push_back(ch);
        token_started = true;
    }

    if (escaping) {
        if (err) {
            *err = "MCP command has a trailing escape character. Use structured executable/args text; no shell is used.";
        }
        return std::nullopt;
    }
    if (quote_mode != QuoteMode::None) {
        if (err) {
            *err = "MCP command has an unterminated quoted argument. Use structured executable/args text; no shell is used.";
        }
        return std::nullopt;
    }

    finish_token();
    if (tokens.empty() || tokens.front().empty()) {
        if (err) {
            *err = "MCP server command must include a non-empty executable. Shell wrapping is not supported.";
        }
        return std::nullopt;
    }
    return tokens;
}

}  // namespace

std::optional<McpServerSpec> parse_mcp_server_spec(const std::string& spec, std::string* err) {
    const std::size_t delimiter = spec.find('=');
    if (delimiter == std::string::npos) {
        if (err) {
            *err = "Invalid MCP server spec '" + spec + "'. Expected <name>=<executable> [args...].";
        }
        return std::nullopt;
    }

    McpServerSpec parsed;
    parsed.name = spec.substr(0, delimiter);
    const std::string command_text = spec.substr(delimiter + 1);
    if (parsed.name.empty() || command_text.empty()) {
        if (err) {
            *err = "Invalid MCP server spec '" + spec + "'. Name and command must both be non-empty.";
        }
        return std::nullopt;
    }

    const std::string sanitized_name = sanitize_identifier(parsed.name);
    if (sanitized_name != parsed.name) {
        if (err) {
            *err = "Invalid MCP server name '" + parsed.name + "'. Use only letters, numbers, '_' or '-'.";
        }
        return std::nullopt;
    }

    auto command_args = tokenize_command_args(command_text, err);
    if (!command_args.has_value()) {
        return std::nullopt;
    }
    parsed.command_args = std::move(*command_args);

    return parsed;
}

std::string mcp_make_local_tool_name(const std::string& server_name, const std::string& remote_tool_name) {
    return "mcp." + sanitize_identifier(server_name) + "." + sanitize_identifier(remote_tool_name);
}

nlohmann::json mcp_normalize_input_schema(const nlohmann::json& input_schema) {
    if (!input_schema.is_object()) {
        return nlohmann::json{
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        };
    }

    nlohmann::json schema = input_schema;
    if (!schema.contains("type")) {
        schema["type"] = "object";
    }
    if (!schema.contains("properties")) {
        schema["properties"] = nlohmann::json::object();
    }
    return schema;
}

McpClient::McpClient(McpServerSpec spec, std::string working_directory, McpClientOptions options)
    : spec_(std::move(spec)),
      options_(options),
      transport_(spec_.name, spec_.command_args, std::move(working_directory), options_.write_timeout_ms) {}

bool McpClient::start(std::string* err) {
    return transport_.start(err);
}

bool McpClient::request(const std::string& method,
                        const nlohmann::json& params,
                        nlohmann::json* result,
                        std::string* err) {
    const int request_id = next_request_id_++;
    const nlohmann::json request_json{
        {"jsonrpc", "2.0"},
        {"id", request_id},
        {"method", method},
        {"params", params}
    };

    if (!transport_.send_message(request_json, err)) {
        return false;
    }

    while (true) {
        nlohmann::json response;
        if (!transport_.read_message(options_.request_timeout_ms,
                                     options_.max_message_bytes,
                                     &response,
                                     err)) {
            return false;
        }
        if (!response.is_object()) {
            if (err) {
                *err = "MCP server '" + spec_.name + "' returned a non-object JSON-RPC message.";
            }
            return false;
        }
        if (response.contains("method") && !response.contains("id")) {
            continue;
        }
        if (!response.contains("id") || !response.at("id").is_number_integer()) {
            if (err) {
                *err = "MCP server '" + spec_.name + "' returned a malformed JSON-RPC response id.";
            }
            return false;
        }
        if (response.at("id").get<int>() != request_id) {
            continue;
        }
        if (response.contains("error")) {
            if (err) {
                *err = "MCP request '" + method + "' failed: " + response.at("error").dump();
            }
            return false;
        }
        if (!response.contains("result")) {
            if (err) {
                *err = "MCP request '" + method + "' returned no result.";
            }
            return false;
        }
        *result = response.at("result");
        return true;
    }
}

bool McpClient::notify(const std::string& method, const nlohmann::json& params, std::string* err) {
    return transport_.send_message(nlohmann::json{
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    }, err);
}

bool McpClient::initialize(McpServerInfo* info, std::string* err) {
    if (!transport_.running() && !start(err)) {
        return false;
    }

    nlohmann::json result;
    if (!request("initialize",
                 nlohmann::json{
                     {"protocolVersion", kProtocolVersion},
                     {"capabilities", nlohmann::json::object()},
                     {"clientInfo", {
                         {"name", "NanoCodeAgent"},
                         {"version", "0.1.0"}
                     }}
                 },
                 &result,
                 err)) {
        return false;
    }
    if (!result.is_object()) {
        if (err) {
            *err = "MCP initialize result from server '" + spec_.name + "' must be an object.";
        }
        return false;
    }

    McpServerInfo server_info;
    server_info.server_name = spec_.name;
    if (!result.contains("protocolVersion") || !result.at("protocolVersion").is_string()) {
        if (err) {
            *err = "MCP initialize result from server '" + spec_.name + "' is missing protocolVersion.";
        }
        return false;
    }
    server_info.negotiated_protocol_version = result.at("protocolVersion").get<std::string>();
    if (result.contains("capabilities") && result.at("capabilities").is_object()) {
        server_info.capabilities = result.at("capabilities");
    }

    if (!notify("notifications/initialized", nlohmann::json::object(), err)) {
        return false;
    }

    initialized_ = true;
    if (info != nullptr) {
        *info = std::move(server_info);
    }
    return true;
}

bool McpClient::list_tools(std::vector<McpToolInfo>* tools, std::string* err) {
    if (!initialized_) {
        if (err) {
            *err = "MCP client for server '" + spec_.name + "' is not initialized.";
        }
        return false;
    }

    std::vector<McpToolInfo> listed_tools;
    std::unordered_set<std::string> seen_remote_names;
    std::unordered_set<std::string> seen_local_names;
    std::unordered_set<std::string> seen_cursors;
    std::optional<std::string> cursor;

    for (std::size_t page_index = 0; page_index < kMaxToolsListPages; ++page_index) {
        nlohmann::json params = nlohmann::json::object();
        if (cursor.has_value()) {
            params["cursor"] = *cursor;
        }

        nlohmann::json result;
        if (!request("tools/list", params, &result, err)) {
            return false;
        }
        if (!result.is_object() || !result.contains("tools") || !result.at("tools").is_array()) {
            if (err) {
                *err = "MCP tools/list result from server '" + spec_.name + "' is malformed.";
            }
            return false;
        }

        listed_tools.reserve(listed_tools.size() + result.at("tools").size());
        for (const auto& item : result.at("tools")) {
            if (!item.is_object() || !item.contains("name") || !item.at("name").is_string()) {
                if (err) {
                    *err = "MCP tools/list from server '" + spec_.name + "' returned a malformed tool entry.";
                }
                return false;
            }

            McpToolInfo tool;
            tool.remote_name = item.at("name").get<std::string>();
            tool.local_name = mcp_make_local_tool_name(spec_.name, tool.remote_name);
            if (!seen_remote_names.insert(tool.remote_name).second) {
                if (err) {
                    *err = "MCP tools/list from server '" + spec_.name + "' returned duplicate tool name '" +
                           tool.remote_name + "'.";
                }
                return false;
            }
            if (!seen_local_names.insert(tool.local_name).second) {
                if (err) {
                    *err = "MCP tools/list from server '" + spec_.name +
                           "' produced duplicate local tool alias '" + tool.local_name + "'.";
                }
                return false;
            }
            if (item.contains("description") && item.at("description").is_string()) {
                tool.description = item.at("description").get<std::string>();
            }
            if (item.contains("inputSchema")) {
                tool.input_schema = mcp_normalize_input_schema(item.at("inputSchema"));
            } else {
                tool.input_schema = mcp_normalize_input_schema(nlohmann::json::object());
            }
            listed_tools.push_back(std::move(tool));
        }

        if (!result.contains("nextCursor") || result.at("nextCursor").is_null()) {
            cursor.reset();
            break;
        }
        if (!result.at("nextCursor").is_string()) {
            if (err) {
                *err = "MCP tools/list from server '" + spec_.name + "' returned a non-string nextCursor.";
            }
            return false;
        }

        const std::string next_cursor = result.at("nextCursor").get<std::string>();
        if (next_cursor.empty()) {
            cursor.reset();
            break;
        }
        if (!seen_cursors.insert(next_cursor).second) {
            if (err) {
                *err = "MCP tools/list pagination from server '" + spec_.name +
                       "' repeated a cursor and could not make progress.";
            }
            return false;
        } else {
            cursor = next_cursor;
        }
    }

    if (cursor.has_value()) {
        if (err) {
            *err = "MCP tools/list from server '" + spec_.name + "' exceeded the pagination page cap.";
        }
        return false;
    }

    if (tools != nullptr) {
        *tools = std::move(listed_tools);
    }
    return true;
}

bool McpClient::call_tool(const std::string& remote_tool_name,
                          const nlohmann::json& arguments,
                          nlohmann::json* result,
                          std::string* err) {
    if (!initialized_) {
        if (err) {
            *err = "MCP client for server '" + spec_.name + "' is not initialized.";
        }
        return false;
    }

    nlohmann::json request_result;
    if (!request("tools/call",
                 nlohmann::json{
                     {"name", remote_tool_name},
                     {"arguments", arguments.is_object() ? arguments : nlohmann::json::object()}
                 },
                 &request_result,
                 err)) {
        return false;
    }

    if (!request_result.is_object()) {
        if (err) {
            *err = "MCP tools/call result from server '" + spec_.name + "' must be an object.";
        }
        return false;
    }

    if (result != nullptr) {
        *result = std::move(request_result);
    }
    return true;
}

void McpClient::close() {
    transport_.close();
    initialized_ = false;
}

McpSession::McpSession(std::string working_directory, McpClientOptions options)
    : working_directory_(std::move(working_directory)), options_(options) {}

McpSession::~McpSession() {
    close();
}

bool McpSession::start(const std::vector<std::string>& server_specs,
                       SessionState* session_state,
                       std::string* err) {
    close();
    if (session_state != nullptr) {
        set_session_mcp_servers(*session_state, {});
    }

    std::vector<McpServerRecord> server_records;
    for (const std::string& spec_string : server_specs) {
        auto parsed_spec = parse_mcp_server_spec(spec_string, err);
        if (!parsed_spec.has_value()) {
            close();
            return false;
        }
        if (index_by_server_name_.find(parsed_spec->name) != index_by_server_name_.end()) {
            if (err) {
                *err = "Duplicate MCP server name '" + parsed_spec->name + "'.";
            }
            close();
            return false;
        }

        auto client = std::make_unique<McpClient>(*parsed_spec, working_directory_, options_);
        if (!client->start(err)) {
            close();
            return false;
        }

        McpServerInfo info;
        if (!client->initialize(&info, err)) {
            close();
            return false;
        }

        if (!client->list_tools(&info.tools, err)) {
            close();
            return false;
        }

        index_by_server_name_[info.server_name] = servers_.size();
        server_records.push_back(make_server_record(info));
        servers_.push_back(info);
        clients_.push_back(std::move(client));
        LOG_INFO("Connected MCP server '{}' with {} tools.", info.server_name, info.tools.size());
    }

    if (session_state != nullptr) {
        set_session_mcp_servers(*session_state, server_records);
    }
    return true;
}

bool McpSession::call_tool(const std::string& server_name,
                           const std::string& remote_tool_name,
                           const ToolCall& call,
                           size_t output_limit,
                           const ToolExecutionContext* context,
                           nlohmann::json* result,
                           std::string* err) {
    const auto it = index_by_server_name_.find(server_name);
    if (it == index_by_server_name_.end()) {
        if (err) {
            *err = "MCP server '" + server_name + "' is not connected.";
        }
        return false;
    }

    nlohmann::json raw_result;
    if (!clients_[it->second]->call_tool(remote_tool_name, call.arguments, &raw_result, err)) {
        if (context != nullptr && context->session_state != nullptr) {
            append_mcp_tool_call_observation(*context->session_state,
                                             context->turn_index,
                                             tool_call_id_for_context(call, context),
                                             server_name,
                                             remote_tool_name,
                                             "failed",
                                             nlohmann::json{
                                                 {"ok", false},
                                                 {"status", "failed"},
                                                 {"error", err != nullptr ? *err : std::string("MCP tools/call failed.")}
                                             });
        }
        return false;
    }

    const nlohmann::json bridge_result = make_capped_bridge_result(server_name, remote_tool_name, raw_result, output_limit);
    if (context != nullptr && context->session_state != nullptr) {
        append_mcp_tool_call_observation(*context->session_state,
                                         context->turn_index,
                                         tool_call_id_for_context(call, context),
                                         server_name,
                                         remote_tool_name,
                                         status_from_mcp_result(raw_result),
                                         bridge_result);
    }

    if (result != nullptr) {
        *result = bridge_result;
    }
    return true;
}

void McpSession::close() {
    for (auto& client : clients_) {
        client->close();
    }
    clients_.clear();
    servers_.clear();
    index_by_server_name_.clear();
}
