#pragma once

#include "config.hpp"
#include "tool_call.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

struct SessionState;

struct ToolExecutionContext {
    SessionState* session_state = nullptr;
    int turn_index = 0;
    std::string tool_call_id;
};

enum class ToolCategory {
    ReadOnly,
    Mutating,
    Execution
};

std::string tool_category_to_string(ToolCategory category);

struct ToolDescriptor {
    using Executor = std::function<nlohmann::json(const ToolCall&, const AgentConfig&, size_t, const ToolExecutionContext*)>;

    std::string name;
    std::string description;
    ToolCategory category = ToolCategory::ReadOnly;
    bool mutates_repository_state = false;
    bool can_execute_repo_controlled_code = false;
    bool requires_approval = false;
    std::vector<std::string> skill_aliases;
    nlohmann::json json_schema = nlohmann::json::object();
    size_t max_output_bytes = 0;
    Executor execute;
};

class ToolRegistry {
public:
    bool register_tool(ToolDescriptor descriptor, std::string* err = nullptr);

    const ToolDescriptor* find(const std::string& name) const;

    std::string execute(const ToolCall& call, const AgentConfig& config) const;
    std::string execute(const ToolCall& call,
                        const AgentConfig& config,
                        const ToolExecutionContext* context) const;

    nlohmann::json to_openai_schema() const;

    nlohmann::json to_openai_schema(const AgentConfig& config) const;

    size_t size() const { return descriptors_.size(); }

private:
    std::vector<ToolDescriptor> descriptors_;
    std::unordered_map<std::string, size_t> index_by_name_;
};
