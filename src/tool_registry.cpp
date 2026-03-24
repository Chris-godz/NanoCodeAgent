#include "tool_registry.hpp"

#include <algorithm>

namespace {

nlohmann::json make_registry_error(const std::string& message) {
    return nlohmann::json{
        {"ok", false},
        {"status", "failed"},
        {"error", message}
    };
}

nlohmann::json missing_approvals_for(const ToolDescriptor& descriptor, const AgentConfig& config) {
    nlohmann::json missing = nlohmann::json::array();
    if (descriptor.mutates_repository_state && !config.allow_mutating_tools) {
        missing.push_back("mutating");
    }
    if (descriptor.can_execute_repo_controlled_code && !config.allow_execution_tools) {
        missing.push_back("execution");
    }
    return missing;
}

nlohmann::json make_approval_blocked_result(const ToolDescriptor& descriptor, const AgentConfig& config) {
    const bool requires_mutating = descriptor.mutates_repository_state;
    const bool requires_execution = descriptor.can_execute_repo_controlled_code;
    const nlohmann::json missing = missing_approvals_for(descriptor, config);

    std::string error = "Tool execution requires approval under the current policy.";
    if (requires_mutating && requires_execution &&
        missing == nlohmann::json::array({"mutating", "execution"})) {
        error =
            "Tool execution is blocked by default because this tool both mutates repository state and can "
            "execute repository-controlled code. Enable allow_mutating_tools, set NCA_ALLOW_MUTATING_TOOLS=1, "
            "or pass --allow-mutating-tools, and enable allow_execution_tools, set "
            "NCA_ALLOW_EXECUTION_TOOLS=1, or pass --allow-execution-tools.";
    } else if (requires_mutating && !config.allow_mutating_tools) {
        error =
            "Mutating tool execution is blocked by default. Enable allow_mutating_tools, set "
            "NCA_ALLOW_MUTATING_TOOLS=1, or pass --allow-mutating-tools.";
    } else if (requires_execution && !config.allow_execution_tools) {
        error =
            "Execution tool use is blocked by default. Enable allow_execution_tools, set "
            "NCA_ALLOW_EXECUTION_TOOLS=1, or pass --allow-execution-tools.";
    }

    return nlohmann::json{
        {"ok", false},
        {"status", "blocked"},
        {"tool", descriptor.name},
        {"category", tool_category_to_string(descriptor.category)},
        {"requires_approval", descriptor.requires_approval},
        {"requires_mutating_approval", requires_mutating},
        {"requires_execution_approval", requires_execution},
        {"missing_approvals", missing},
        {"error", error}
    };
}

size_t resolve_effective_output_limit(size_t descriptor_limit, size_t config_limit) {
    if (descriptor_limit == 0) {
        return config_limit;
    }
    if (config_limit == 0) {
        return descriptor_limit;
    }
    return std::min(descriptor_limit, config_limit);
}

bool tool_visible_in_schema(const ToolDescriptor& descriptor, const AgentConfig& config) {
    if (descriptor.mutates_repository_state && !config.allow_mutating_tools) {
        return false;
    }
    if (descriptor.can_execute_repo_controlled_code && !config.allow_execution_tools) {
        return false;
    }
    return true;
}

bool tool_execution_allowed(const ToolDescriptor& descriptor, const AgentConfig& config) {
    if (!descriptor.requires_approval) {
        return true;
    }

    return missing_approvals_for(descriptor, config).empty();
}

} // namespace

std::string tool_category_to_string(ToolCategory category) {
    switch (category) {
        case ToolCategory::ReadOnly:
            return "read_only";
        case ToolCategory::Mutating:
            return "mutating";
        case ToolCategory::Execution:
            return "execution";
    }
    return "unknown";
}

bool ToolRegistry::register_tool(ToolDescriptor descriptor, std::string* err) {
    if (descriptor.name.empty()) {
        if (err) *err = "Tool name must not be empty";
        return false;
    }
    if (!descriptor.execute) {
        if (err) *err = "Tool '" + descriptor.name + "' is missing an execute callback";
        return false;
    }
    if (index_by_name_.find(descriptor.name) != index_by_name_.end()) {
        if (err) *err = "Tool '" + descriptor.name + "' is already registered";
        return false;
    }

    if (!descriptor.mutates_repository_state && !descriptor.can_execute_repo_controlled_code) {
        descriptor.requires_approval = false;
    }

    index_by_name_[descriptor.name] = descriptors_.size();
    descriptors_.push_back(std::move(descriptor));
    return true;
}

const ToolDescriptor* ToolRegistry::find(const std::string& name) const {
    auto it = index_by_name_.find(name);
    if (it == index_by_name_.end()) {
        return nullptr;
    }
    return &descriptors_[it->second];
}

std::string ToolRegistry::execute(const ToolCall& call, const AgentConfig& config) const {
    const ToolDescriptor* descriptor = find(call.name);
    if (!descriptor) {
        return make_registry_error("Tool '" + call.name + "' is not registered.").dump();
    }

    const size_t output_limit = resolve_effective_output_limit(descriptor->max_output_bytes, config.max_tool_output_bytes);

    if (!tool_execution_allowed(*descriptor, config)) {
        return make_approval_blocked_result(*descriptor, config).dump();
    }

    try {
        nlohmann::json result = descriptor->execute(call, config, output_limit);
        if (!result.is_object()) {
            return make_registry_error("Tool '" + call.name + "' returned a non-object result.").dump();
        }
        return result.dump();
    } catch (const std::exception& e) {
        return make_registry_error(std::string("Exception during tool execution: ") + e.what()).dump();
    }
}

nlohmann::json ToolRegistry::to_openai_schema() const {
    nlohmann::json schema = nlohmann::json::array();

    for (const auto& descriptor : descriptors_) {
        schema.push_back({
            {"type", "function"},
            {"function", {
                {"name", descriptor.name},
                {"description", descriptor.description},
                {"parameters", descriptor.json_schema}
            }}
        });
    }

    return schema;
}

nlohmann::json ToolRegistry::to_openai_schema(const AgentConfig& config) const {
    nlohmann::json schema = nlohmann::json::array();

    for (const auto& descriptor : descriptors_) {
        if (!tool_visible_in_schema(descriptor, config)) {
            continue;
        }

        schema.push_back({
            {"type", "function"},
            {"function", {
                {"name", descriptor.name},
                {"description", descriptor.description},
                {"parameters", descriptor.json_schema}
            }}
        });
    }

    return schema;
}
