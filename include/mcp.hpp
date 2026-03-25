#pragma once

#include "mcp_transport.hpp"
#include "state.hpp"
#include "tool_registry.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

struct McpClientOptions {
    int request_timeout_ms = 3000;
    int write_timeout_ms = 3000;
    size_t max_message_bytes = 256 * 1024;
};

struct McpServerSpec {
    std::string name;
    std::vector<std::string> command_args;
};

struct McpToolInfo {
    std::string remote_name;
    std::string local_name;
    std::string description;
    nlohmann::json input_schema = nlohmann::json::object();
};

struct McpServerInfo {
    std::string server_name;
    std::string negotiated_protocol_version;
    nlohmann::json capabilities = nlohmann::json::object();
    std::vector<McpToolInfo> tools;
};

std::optional<McpServerSpec> parse_mcp_server_spec(const std::string& spec, std::string* err);
std::string mcp_make_local_tool_name(const std::string& server_name, const std::string& remote_tool_name);
nlohmann::json mcp_normalize_input_schema(const nlohmann::json& input_schema);

class McpClient {
public:
    McpClient(McpServerSpec spec, std::string working_directory, McpClientOptions options = {});

    bool start(std::string* err);
    bool initialize(McpServerInfo* info, std::string* err);
    bool list_tools(std::vector<McpToolInfo>* tools, std::string* err);
    bool call_tool(const std::string& remote_tool_name,
                   const nlohmann::json& arguments,
                   nlohmann::json* result,
                   std::string* err);
    void close();

    const McpServerSpec& spec() const { return spec_; }

private:
    bool request(const std::string& method,
                 const nlohmann::json& params,
                 nlohmann::json* result,
                 std::string* err);
    bool notify(const std::string& method, const nlohmann::json& params, std::string* err);

    McpServerSpec spec_;
    McpClientOptions options_;
    McpTransportStdio transport_;
    int next_request_id_ = 1;
    bool initialized_ = false;
};

class McpSession {
public:
    explicit McpSession(std::string working_directory, McpClientOptions options = {});
    ~McpSession();

    bool start(const std::vector<std::string>& server_specs, SessionState* session_state, std::string* err);
    bool call_tool(const std::string& server_name,
                   const std::string& remote_tool_name,
                   const ToolCall& call,
                   size_t output_limit,
                   const ToolExecutionContext* context,
                   nlohmann::json* result,
                   std::string* err);
    void close();

    const std::vector<McpServerInfo>& servers() const { return servers_; }

private:
    std::string working_directory_;
    McpClientOptions options_;
    std::vector<std::unique_ptr<McpClient>> clients_;
    std::vector<McpServerInfo> servers_;
    std::unordered_map<std::string, size_t> index_by_server_name_;
};

class McpToolBridge {
public:
    explicit McpToolBridge(McpSession* session) : session_(session) {}

    bool register_tools(ToolRegistry* registry, std::string* err) const;

private:
    McpSession* session_ = nullptr;
};
