#include "agent_tools.hpp"
#include <cmath>
#include <limits>

namespace {

bool parse_timeout_ms_arg(const nlohmann::json& value, int& timeout_ms) {
    long long parsed = 0;

    if (value.is_number_integer()) {
        parsed = value.get<long long>();
    } else if (value.is_number_unsigned()) {
        auto parsed_u = value.get<unsigned long long>();
        if (parsed_u > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
            return false;
        }
        parsed = static_cast<long long>(parsed_u);
    } else if (value.is_number_float()) {
        double as_double = value.get<double>();
        if (!std::isfinite(as_double) || std::floor(as_double) != as_double) {
            return false;
        }
        if (as_double < static_cast<double>(std::numeric_limits<long long>::min()) ||
            as_double > static_cast<double>(std::numeric_limits<long long>::max())) {
            return false;
        }
        parsed = static_cast<long long>(as_double);
    } else if (value.is_string()) {
        const auto s = value.get<std::string>();
        std::size_t idx = 0;
        try {
            parsed = std::stoll(s, &idx, 10);
        } catch (...) {
            return false;
        }
        if (idx != s.size()) {
            return false;
        }
    } else {
        return false;
    }

    if (parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        return false;
    }

    timeout_ms = static_cast<int>(parsed);
    return true;
}

}

                if (!parse_timeout_ms_arg(t, timeout)) {
                    return format_tool_error("Invalid 'timeout_ms' argument for bash_execute_safe. Expected integer in range [1, INT_MAX].");
#include "write_file.hpp"

#include <stdexcept>
#include <utility>

namespace {

constexpr size_t kMaxReadBytes = 4 * 1024 * 1024;
constexpr size_t kMaxWriteBytes = 4 * 1024 * 1024;
constexpr size_t kMaxBashOutputBytes = 1024 * 1024;
constexpr size_t kMaxRepoOutputBytes = 64 * 1024;

std::string require_string_arg(const ToolCall& cmd, const char* key, const char* tool_name) {
    if (!cmd.arguments.contains(key)) {
        throw std::runtime_error("Missing '" + std::string(key) + "' argument for " + tool_name + ".");
    }
    return cmd.arguments.at(key).get<std::string>();
}

std::string optional_string_arg(const ToolCall& cmd, const char* key, const std::string& default_value = "") {
    if (!cmd.arguments.contains(key)) {
        return default_value;
    }
    return cmd.arguments.at(key).get<std::string>();
}

size_t optional_size_arg(const ToolCall& cmd, const char* key, size_t default_value = 0) {
    if (!cmd.arguments.contains(key)) {
        return default_value;
    }

    const auto& value = cmd.arguments.at(key);
    if (value.is_number_unsigned()) {
        return value.get<size_t>();
    }
    if (value.is_number_integer()) {
        const auto parsed = value.get<long long>();
        if (parsed < 0) {
            throw std::runtime_error("Argument '" + std::string(key) + "' must be non-negative.");
        }
        return static_cast<size_t>(parsed);
    }
    if (value.is_string()) {
        const auto parsed = std::stoll(value.get<std::string>());
        if (parsed < 0) {
            throw std::runtime_error("Argument '" + std::string(key) + "' must be non-negative.");
        }
        return static_cast<size_t>(parsed);
    }

    throw std::runtime_error("Argument '" + std::string(key) + "' must be an integer.");
}

int optional_int_arg(const ToolCall& cmd, const char* key, int default_value) {
    return static_cast<int>(optional_size_arg(cmd, key, static_cast<size_t>(default_value)));
}

std::vector<std::string> optional_string_array_arg(const ToolCall& cmd, const char* key) {
    if (!cmd.arguments.contains(key)) {
        return {};
    }

    std::vector<std::string> result;
    for (const auto& item : cmd.arguments.at(key)) {
        result.push_back(item.get<std::string>());
    }
    return result;
}

nlohmann::json make_parameters_schema(nlohmann::json properties, nlohmann::json required = nlohmann::json::array()) {
    return {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)}
    };
}

void register_or_throw(ToolRegistry* registry, ToolDescriptor descriptor) {
    std::string err;
    if (!registry->register_tool(std::move(descriptor), &err)) {
        throw std::runtime_error(err);
    }
}

ToolRegistry build_default_tool_registry() {
    ToolRegistry registry;

    register_or_throw(&registry, ToolDescriptor{
        .name = "read_file_safe",
        .description = "Reads the complete contents of a workspace file.",
        .category = ToolCategory::ReadOnly,
        .requires_approval = false,
        .json_schema = make_parameters_schema({
            {"path", {
                {"type", "string"},
                {"description", "The relative path to the file to read."}
            }}
        }, {"path"}),
        .max_output_bytes = kMaxReadBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            const std::string path = require_string_arg(cmd, "path", "read_file_safe");
            const auto res = read_file_safe(config.workspace_abs, path, output_limit);
            return nlohmann::json{
                {"ok", res.ok},
                {"content", res.content},
                {"truncated", res.truncated},
                {"error", res.err}
            };
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "write_file_safe",
        .description = "Writes string content to a workspace file, overwriting existing content.",
        .category = ToolCategory::Mutating,
        .requires_approval = true,
        .json_schema = make_parameters_schema({
            {"path", {
                {"type", "string"},
                {"description", "The relative path to the file to write."}
            }},
            {"content", {
                {"type", "string"},
                {"description", "The text content to be written."}
            }}
        }, {"path", "content"}),
        .max_output_bytes = 8192,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t) {
            const std::string path = require_string_arg(cmd, "path", "write_file_safe");
            const std::string content = require_string_arg(cmd, "content", "write_file_safe");
            const auto res = write_file_safe(config.workspace_abs, path, content, kMaxWriteBytes);
            return nlohmann::json{
                {"ok", res.ok},
                {"error", res.err}
            };
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "bash_execute_safe",
        .description = "Executes a bounded bash command inside the workspace.",
        .category = ToolCategory::Execution,
        .requires_approval = true,
        .json_schema = make_parameters_schema({
            {"command", {
                {"type", "string"},
                {"description", "The bash command to run."}
            }},
            {"timeout_ms", {
                {"type", "integer"},
                {"description", "Timeout in milliseconds for the command. Default is 5000."}
            }}
        }, {"command"}),
        .max_output_bytes = kMaxBashOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            const std::string command = require_string_arg(cmd, "command", "bash_execute_safe");
            const int timeout_ms = optional_int_arg(cmd, "timeout_ms", 5000);
            const auto res = bash_execute_safe(config.workspace_abs, command, timeout_ms, output_limit, output_limit);
            return nlohmann::json{
                {"ok", res.ok},
                {"exit_code", res.exit_code},
                {"stdout", res.out_tail},
                {"stderr", res.err_tail},
                {"truncated", res.truncated},
                {"timed_out", res.timed_out},
                {"error", res.err}
            };
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "list_files_bounded",
        .description = "Lists workspace files with optional directory and extension filters.",
        .category = ToolCategory::ReadOnly,
        .requires_approval = false,
        .json_schema = make_parameters_schema({
            {"directory", {
                {"type", "string"},
                {"description", "Optional relative directory to scan within the workspace."}
            }},
            {"extensions", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "Optional file extensions to include, such as ['.cpp', '.hpp']."}
            }},
            {"max_results", {
                {"type", "integer"},
                {"description", "Optional maximum number of files to return."}
            }}
        }),
        .max_output_bytes = kMaxRepoOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            return list_files_bounded(
                config.workspace_abs,
                optional_string_arg(cmd, "directory"),
                optional_string_array_arg(cmd, "extensions"),
                optional_size_arg(cmd, "max_results", 0),
                output_limit);
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "rg_search",
        .description = "Searches the workspace with ripgrep and returns structured matches.",
        .category = ToolCategory::ReadOnly,
        .requires_approval = false,
        .json_schema = make_parameters_schema({
            {"query", {
                {"type", "string"},
                {"description", "The symbol or text to search for."}
            }},
            {"directory", {
                {"type", "string"},
                {"description", "Optional relative directory to search from."}
            }},
            {"max_results", {
                {"type", "integer"},
                {"description", "Optional maximum number of matches to return."}
            }},
            {"max_snippet_bytes", {
                {"type", "integer"},
                {"description", "Optional maximum bytes to keep per match snippet."}
            }}
        }, {"query"}),
        .max_output_bytes = kMaxRepoOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            return rg_search(
                config.workspace_abs,
                require_string_arg(cmd, "query", "rg_search"),
                optional_string_arg(cmd, "directory"),
                optional_size_arg(cmd, "max_results", 0),
                optional_size_arg(cmd, "max_snippet_bytes", 0),
                output_limit);
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "git_status",
        .description = "Returns the current git working tree status for the workspace.",
        .category = ToolCategory::ReadOnly,
        .requires_approval = false,
        .json_schema = make_parameters_schema(nlohmann::json::object()),
        .max_output_bytes = kMaxRepoOutputBytes,
        .execute = [](const ToolCall&, const AgentConfig& config, size_t output_limit) {
            return git_status(config.workspace_abs, 0, output_limit);
        }
    });

    return registry;
}

} // namespace

std::string format_tool_error(const std::string& error_msg) {
    nlohmann::json err = {
        {"ok", false},
        {"status", "failed"},
        {"error", error_msg}
    };
    return err.dump();
}

const ToolRegistry& get_default_tool_registry() {
    static const ToolRegistry registry = build_default_tool_registry();
    return registry;
}

std::string execute_tool(const ToolCall& cmd, const AgentConfig& config) {
    return get_default_tool_registry().execute(cmd, config);
}

nlohmann::json get_agent_tools_schema() {
    return get_default_tool_registry().to_openai_schema();
}
