#include "mcp.hpp"

#include <utility>

bool McpToolBridge::register_tools(ToolRegistry* registry, std::string* err) const {
    if (registry == nullptr) {
        if (err) {
            *err = "McpToolBridge requires a non-null ToolRegistry.";
        }
        return false;
    }
    if (session_ == nullptr) {
        if (err) {
            *err = "McpToolBridge requires a non-null McpSession.";
        }
        return false;
    }

    for (const McpServerInfo& server : session_->servers()) {
        for (const McpToolInfo& tool : server.tools) {
            ToolDescriptor descriptor;
            descriptor.name = tool.local_name;
            descriptor.description = tool.description.empty()
                                         ? "MCP stdio tool '" + tool.remote_name + "' from server '" + server.server_name + "'."
                                         : tool.description;
            descriptor.category = ToolCategory::Execution;
            descriptor.mutates_repository_state = true;
            descriptor.can_execute_repo_controlled_code = true;
            descriptor.requires_approval = true;
            descriptor.json_schema = tool.input_schema;
            descriptor.execute = [session = session_,
                                  server_name = server.server_name,
                                  remote_tool_name = tool.remote_name](const ToolCall& call,
                                                                       const AgentConfig&,
                                                                       size_t output_limit,
                                                                       const ToolExecutionContext* context) {
                nlohmann::json result;
                std::string err;
                if (!session->call_tool(server_name,
                                        remote_tool_name,
                                        call,
                                        output_limit,
                                        context,
                                        &result,
                                        &err)) {
                    return nlohmann::json{
                        {"ok", false},
                        {"status", "failed"},
                        {"server_name", server_name},
                        {"tool_name", remote_tool_name},
                        {"error", err}
                    };
                }
                return result;
            };

            if (!registry->register_tool(std::move(descriptor), err)) {
                return false;
            }
        }
    }

    return true;
}
