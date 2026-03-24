#include "agent_tools.hpp"
#include "apply_patch.hpp"
#include "bash_tool.hpp"
#include "build_test_tools.hpp"
#include "read_file.hpp"
#include "repo_tools.hpp"
#include "write_file.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaxReadBytes = 4 * 1024 * 1024;
constexpr size_t kMaxWriteBytes = 4 * 1024 * 1024;
constexpr size_t kMaxBashOutputBytes = 1024 * 1024;
constexpr size_t kDefaultBuildToolOutputBytes = 64 * 1024;
constexpr size_t kMaxBuildToolOutputBytes = 1024 * 1024;
constexpr int kDefaultBuildTimeoutMs = 120000;
constexpr int kDefaultTestTimeoutMs = 120000;
constexpr size_t kMaxRepoOutputBytes = 64 * 1024;
constexpr size_t kMaxGitContextLines = 1000;

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

size_t optional_git_context_lines_arg(const ToolCall& cmd, const char* key, size_t default_value) {
    return std::min(optional_size_arg(cmd, key, default_value), kMaxGitContextLines);
}

int parse_timeout_ms_arg(const ToolCall& cmd, const char* key, int default_value) {
    if (!cmd.arguments.contains(key)) {
        return default_value;
    }

    const auto& value = cmd.arguments.at(key);
    long long parsed = 0;

    if (value.is_number_unsigned()) {
        const auto raw = value.get<unsigned long long>();
        if (raw > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("Argument '" + std::string(key) + "' must be between 1 and " +
                                     std::to_string(std::numeric_limits<int>::max()) + ".");
        }
        parsed = static_cast<long long>(raw);
    } else if (value.is_number_integer()) {
        parsed = value.get<long long>();
    } else if (value.is_string()) {
        parsed = std::stoll(value.get<std::string>());
    } else {
        throw std::runtime_error("Argument '" + std::string(key) + "' must be an integer.");
    }

    if (parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        throw std::runtime_error("Argument '" + std::string(key) + "' must be between 1 and " +
                                 std::to_string(std::numeric_limits<int>::max()) + ".");
    }

    return static_cast<int>(parsed);
}

bool optional_bool_arg(const ToolCall& cmd, const char* key, bool default_value = false) {
    if (!cmd.arguments.contains(key)) {
        return default_value;
    }

    const auto& value = cmd.arguments.at(key);
    if (!value.is_boolean()) {
        throw std::runtime_error("Argument '" + std::string(key) + "' must be a boolean.");
    }
    return value.get<bool>();
}

std::string require_non_empty_string_arg(const ToolCall& cmd, const char* key, const char* tool_name) {
    const std::string value = require_string_arg(cmd, key, tool_name);
    if (value.empty()) {
        throw std::runtime_error("Argument '" + std::string(key) + "' for " + tool_name + " must not be empty.");
    }
    return value;
}

size_t parse_bounded_output_bytes_arg(const ToolCall& cmd, const char* key, size_t default_value) {
    const size_t parsed = optional_size_arg(cmd, key, default_value);
    if (parsed == 0 || parsed > kMaxBuildToolOutputBytes) {
        throw std::runtime_error("Argument '" + std::string(key) + "' must be between 1 and " +
                                 std::to_string(kMaxBuildToolOutputBytes) + ".");
    }
    return parsed;
}

size_t clamp_effective_output_limit(size_t requested_limit, size_t registry_output_limit) {
    if (registry_output_limit == 0) {
        return requested_limit;
    }
    return std::min(requested_limit, registry_output_limit);
}

void reject_unknown_arguments(const ToolCall& cmd,
                              const std::vector<std::string>& allowed_keys,
                              const char* tool_name) {
    for (const auto& [key, _] : cmd.arguments.items()) {
        if (std::find(allowed_keys.begin(), allowed_keys.end(), key) == allowed_keys.end()) {
            throw std::runtime_error("Unknown argument '" + key + "' for " + tool_name + ".");
        }
    }
}

std::string parse_build_mode_arg(const ToolCall& cmd) {
    const std::string build_mode = optional_string_arg(cmd, "build_mode", "debug");
    if (build_mode != "debug" && build_mode != "release") {
        throw std::runtime_error("Argument 'build_mode' must be one of: debug, release.");
    }
    return build_mode;
}

nlohmann::json build_script_result_json(const BuildScriptResult& result) {
    return nlohmann::json{
        {"ok", result.ok},
        {"status", result.status},
        {"exit_code", result.exit_code},
        {"stdout", result.stdout_text},
        {"stderr", result.stderr_text},
        {"truncated", result.truncated},
        {"timed_out", result.timed_out},
        {"summary", result.summary}
    };
}

nlohmann::json apply_patch_result_to_json(const ApplyPatchResult& result) {
    nlohmann::json response{
        {"ok", result.ok},
        {"match_count", result.match_count},
        {"error", result.err},
        {"reject_code", patch_reject_code_to_string(result.reject_code)}
    };
    if (result.patch_index >= 0) {
        response["patch_index"] = result.patch_index;
    }
    return response;
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

std::vector<std::string> optional_string_array_arg_strict(const ToolCall& cmd, const char* key) {
    if (!cmd.arguments.contains(key)) {
        return {};
    }

    const auto& value = cmd.arguments.at(key);
    if (!value.is_array()) {
        throw std::runtime_error("Argument '" + std::string(key) + "' must be an array of strings.");
    }

    std::vector<std::string> result;
    result.reserve(value.size());
    for (const auto& item : value) {
        if (!item.is_string()) {
            throw std::runtime_error("Argument '" + std::string(key) + "' must be an array of strings.");
        }
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
        .mutates_repository_state = false,
        .can_execute_repo_controlled_code = false,
        .requires_approval = false,
        .skill_aliases = {"Read"},
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
        .mutates_repository_state = true,
        .can_execute_repo_controlled_code = false,
        .requires_approval = true,
        .skill_aliases = {"Write"},
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
        .mutates_repository_state = true,
        .can_execute_repo_controlled_code = true,
        .requires_approval = true,
        .skill_aliases = {"Bash"},
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
            const int timeout_ms = parse_timeout_ms_arg(cmd, "timeout_ms", 5000);
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
        .name = "build_project_safe",
        .description = "Runs ./build.sh in debug or release mode with bounded retained output.",
        .category = ToolCategory::Execution,
        .mutates_repository_state = true,
        .can_execute_repo_controlled_code = true,
        .requires_approval = true,
        .skill_aliases = {"Bash"},
        .json_schema = make_parameters_schema({
            {"build_mode", {
                {"type", "string"},
                {"enum", nlohmann::json::array({"debug", "release"})},
                {"description", "Optional build mode. Defaults to debug."}
            }},
            {"clean_first", {
                {"type", "boolean"},
                {"description", "If true, run ./build.sh clean before the build."}
            }},
            {"timeout_ms", {
                {"type", "integer"},
                {"description", "Timeout in milliseconds for the whole build sequence. Default is 120000."}
            }},
            {"max_output_bytes", {
                {"type", "integer"},
                {"description", "Maximum retained bytes per output stream. Default is 65536."}
            }}
        }),
        .max_output_bytes = kMaxBashOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            if (cmd.arguments.contains("target")) {
                throw std::runtime_error("Argument 'target' is not supported by build_project_safe in v1.");
            }
            reject_unknown_arguments(cmd, {"build_mode", "clean_first", "timeout_ms", "max_output_bytes"},
                                     "build_project_safe");
            const std::string build_mode = parse_build_mode_arg(cmd);
            const bool clean_first = optional_bool_arg(cmd, "clean_first", false);
            const int timeout_ms = parse_timeout_ms_arg(cmd, "timeout_ms", kDefaultBuildTimeoutMs);
            const size_t requested_output_bytes = parse_bounded_output_bytes_arg(
                cmd, "max_output_bytes", kDefaultBuildToolOutputBytes);
            const size_t max_output_bytes = clamp_effective_output_limit(requested_output_bytes, output_limit);

            std::vector<std::string> subcommands;
            if (clean_first) {
                subcommands.push_back("clean");
            }
            subcommands.push_back(build_mode);

            return build_script_result_json(
                run_build_script_sequence(config.workspace_abs, subcommands, timeout_ms, max_output_bytes));
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "test_project_safe",
        .description = "Runs ./build.sh test with bounded retained output and a small ctest summary.",
        .category = ToolCategory::Execution,
        .mutates_repository_state = true,
        .can_execute_repo_controlled_code = true,
        .requires_approval = true,
        .skill_aliases = {"Bash"},
        .json_schema = make_parameters_schema({
            {"timeout_ms", {
                {"type", "integer"},
                {"description", "Timeout in milliseconds for the test run. Default is 120000."}
            }},
            {"max_output_bytes", {
                {"type", "integer"},
                {"description", "Maximum retained bytes per output stream. Default is 65536."}
            }}
        }),
        .max_output_bytes = kMaxBashOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            if (cmd.arguments.contains("filter")) {
                throw std::runtime_error("Argument 'filter' is not supported by test_project_safe in v1.");
            }
            if (cmd.arguments.contains("ensure_debug_build")) {
                throw std::runtime_error(
                    "Argument 'ensure_debug_build' is not supported because ./build.sh test already ensures a debug build.");
            }
            reject_unknown_arguments(cmd, {"timeout_ms", "max_output_bytes"}, "test_project_safe");

            const int timeout_ms = parse_timeout_ms_arg(cmd, "timeout_ms", kDefaultTestTimeoutMs);
            const size_t requested_output_bytes = parse_bounded_output_bytes_arg(
                cmd, "max_output_bytes", kDefaultBuildToolOutputBytes);
            const size_t max_output_bytes = clamp_effective_output_limit(requested_output_bytes, output_limit);
            auto result = run_build_script_sequence(config.workspace_abs, {"test"}, timeout_ms, max_output_bytes);
            nlohmann::json json_result = build_script_result_json(result);

            const auto parsed = parse_ctest_summary(result.stdout_text, result.stderr_text);
            if (parsed.passed_count.has_value()) {
                json_result["passed_count"] = *parsed.passed_count;
            }
            if (parsed.failed_count.has_value()) {
                json_result["failed_count"] = *parsed.failed_count;
            }
            if (!parsed.failed_tests.empty()) {
                json_result["failed_tests"] = parsed.failed_tests;
            }
            return json_result;
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "list_files_bounded",
        .description = "Lists workspace files with optional directory and extension filters.",
        .category = ToolCategory::ReadOnly,
        .mutates_repository_state = false,
        .can_execute_repo_controlled_code = false,
        .requires_approval = false,
        .skill_aliases = {"Read"},
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
        .mutates_repository_state = false,
        .can_execute_repo_controlled_code = false,
        .requires_approval = false,
        .skill_aliases = {"Read"},
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
        .mutates_repository_state = false,
        .can_execute_repo_controlled_code = false,
        .requires_approval = false,
        .skill_aliases = {"Read"},
        .json_schema = make_parameters_schema(nlohmann::json::object()),
        .max_output_bytes = kMaxRepoOutputBytes,
        .execute = [](const ToolCall&, const AgentConfig& config, size_t output_limit) {
            return git_status(config.workspace_abs, 0, output_limit);
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "git_add",
        .description = "Stages explicitly listed git pathspecs into the index.",
        .category = ToolCategory::Mutating,
        .mutates_repository_state = true,
        .can_execute_repo_controlled_code = true,
        .requires_approval = true,
        .json_schema = make_parameters_schema({
            {"pathspecs", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "Explicit git pathspecs to stage. Must contain at least one entry."}
            }}
        }, {"pathspecs"}),
        .max_output_bytes = kMaxRepoOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            const std::vector<std::string> pathspecs = optional_string_array_arg_strict(cmd, "pathspecs");
            return git_add(config.workspace_abs, pathspecs, output_limit);
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "git_commit",
        .description = "Creates a git commit from the currently staged index changes.",
        .category = ToolCategory::Mutating,
        .mutates_repository_state = true,
        .can_execute_repo_controlled_code = true,
        .requires_approval = true,
        .json_schema = make_parameters_schema({
            {"message", {
                {"type", "string"},
                {"description", "Commit message for the staged index changes."}
            }}
        }, {"message"}),
        .max_output_bytes = kMaxRepoOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            const std::string message = require_string_arg(cmd, "message", "git_commit");
            return git_commit(config.workspace_abs, message, output_limit);
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "apply_patch",
        .description = "Applies an exact-text replacement patch to a workspace file. "
                       "Supports single mode (old_text + new_text) and batch mode (patches array). "
                       "old_text must appear exactly once in the file. "
                       "An explicit empty new_text is valid and deletes the matched text.",
        .category = ToolCategory::Mutating,
        .mutates_repository_state = true,
        .can_execute_repo_controlled_code = false,
        .requires_approval = true,
        .skill_aliases = {"Edit"},
        .json_schema = nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "Relative path to the file to patch."}
                }},
                {"old_text", {
                    {"type", "string"},
                    {"description", "(Single mode) The exact text to search for. Must occur exactly once."}
                }},
                {"new_text", {
                    {"type", "string"},
                    {"description", "(Single mode) The replacement text. Pass an explicit empty string to delete."}
                }},
                {"patches", {
                    {"type", "array"},
                    {"description", "(Batch mode) Array of {old_text, new_text} patch entries applied in order."},
                    {"items", {
                        {"type", "object"},
                        {"properties", {
                            {"old_text", {{"type", "string"}}},
                            {"new_text", {{"type", "string"}}}
                        }},
                        {"required", nlohmann::json::array({"old_text", "new_text"})}
                    }}
                }}
            }},
            {"oneOf", nlohmann::json::array({
                nlohmann::json{
                    {"properties", {
                        {"path", nlohmann::json::object()},
                        {"old_text", nlohmann::json::object()},
                        {"new_text", nlohmann::json::object()}
                    }},
                    {"required", nlohmann::json::array({"path", "old_text", "new_text"})},
                    {"additionalProperties", false}
                },
                nlohmann::json{
                    {"properties", {
                        {"path", nlohmann::json::object()},
                        {"patches", nlohmann::json::object()}
                    }},
                    {"required", nlohmann::json::array({"path", "patches"})},
                    {"additionalProperties", false}
                }
            })}
        },
        .max_output_bytes = 8192,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t) -> nlohmann::json {
            if (!cmd.arguments.contains("path")) {
                return nlohmann::json{{"ok", false}, {"error", "Missing 'path' argument for apply_patch."}};
            }
            if (!cmd.arguments.at("path").is_string()) {
                return nlohmann::json{{"ok", false}, {"error", "Invalid type for 'path': expected string."}};
            }
            const std::string path = cmd.arguments.at("path").get<std::string>();

            // Reject unknown top-level keys to match the schema's
            // additionalProperties: false contract in each oneOf branch.
            static const std::array<std::string, 4> kAllowedKeys{"path", "old_text", "new_text", "patches"};
            for (const auto& [key, _] : cmd.arguments.items()) {
                if (std::find(kAllowedKeys.begin(), kAllowedKeys.end(), key) == kAllowedKeys.end()) {
                    return nlohmann::json{
                        {"ok", false},
                        {"error", "Unknown argument '" + key + "' for apply_patch. "
                                  "Allowed top-level fields: path, old_text, new_text, patches."}
                    };
                }
            }

            const bool has_patches  = cmd.arguments.contains("patches");
            const bool has_old_text = cmd.arguments.contains("old_text");
            const bool has_new_text = cmd.arguments.contains("new_text");

            // Reject mixed mode: patches array combined with single-mode fields
            if (has_patches && (has_old_text || has_new_text)) {
                return nlohmann::json{
                    {"ok", false},
                    {"error", "Ambiguous input: 'patches' cannot be combined with 'old_text' or "
                              "'new_text'. Use single mode (old_text + new_text) or batch mode "
                              "(patches array), not both."}
                };
            }

            if (has_patches) {
                // Batch mode
                const auto& patches_val = cmd.arguments.at("patches");
                if (!patches_val.is_array()) {
                    return nlohmann::json{
                        {"ok", false},
                        {"error", "'patches' must be an array of {old_text, new_text} objects, "
                                  "not a " + std::string(patches_val.type_name()) + "."}
                    };
                }
                if (patches_val.empty()) {
                    return nlohmann::json{
                        {"ok", false},
                        {"error", "'patches' array must not be empty."}
                    };
                }

                std::vector<PatchEntry> patches;
                patches.reserve(patches_val.size());
                for (size_t i = 0; i < patches_val.size(); ++i) {
                    const auto& entry = patches_val[i];
                    if (!entry.is_object()) {
                        return nlohmann::json{
                            {"ok", false},
                            {"error", "patches[" + std::to_string(i) + "]: expected object, got "
                                      + std::string(entry.type_name()) + "."}
                        };
                    }
                    if (!entry.contains("old_text")) {
                        return nlohmann::json{
                            {"ok", false},
                            {"error", "patches[" + std::to_string(i) + "]: missing required field 'old_text'."}
                        };
                    }
                    if (!entry["old_text"].is_string()) {
                        return nlohmann::json{
                            {"ok", false},
                            {"error", "patches[" + std::to_string(i) + "]: field 'old_text' must be a string."}
                        };
                    }
                    if (!entry.contains("new_text")) {
                        return nlohmann::json{
                            {"ok", false},
                            {"error", "patches[" + std::to_string(i) + "]: missing required field 'new_text'. "
                                      "To delete text pass an explicit empty string."}
                        };
                    }
                    if (!entry["new_text"].is_string()) {
                        return nlohmann::json{
                            {"ok", false},
                            {"error", "patches[" + std::to_string(i) + "]: field 'new_text' must be a string. "
                                      "To delete text pass an explicit empty string."}
                        };
                    }
                    patches.push_back({
                        entry["old_text"].get<std::string>(),
                        entry["new_text"].get<std::string>()
                    });
                }

                const auto result = apply_patch_batch(config.workspace_abs, path, patches);
                return apply_patch_result_to_json(result);
            } else {
                // Single mode: both old_text and new_text must be explicitly present
                if (!has_old_text) {
                    return nlohmann::json{
                        {"ok", false},
                        {"error", "Missing 'old_text' argument for apply_patch."}
                    };
                }
                if (!has_new_text) {
                    return nlohmann::json{
                        {"ok", false},
                        {"error", "Missing 'new_text' argument for apply_patch. "
                                  "To delete text pass an explicit empty string."}
                    };
                }
                if (!cmd.arguments.at("old_text").is_string()) {
                    return nlohmann::json{
                        {"ok", false},
                        {"error", "Argument 'old_text' must be a string."}
                    };
                }
                if (!cmd.arguments.at("new_text").is_string()) {
                    return nlohmann::json{
                        {"ok", false},
                        {"error", "Argument 'new_text' must be a string."}
                    };
                }
                const std::string old_text = cmd.arguments.at("old_text").get<std::string>();
                const std::string new_text = cmd.arguments.at("new_text").get<std::string>();

                const auto result = apply_patch_single(config.workspace_abs, path, old_text, new_text);
                return apply_patch_result_to_json(result);
            }
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "git_diff",
        .description = "Returns the unified diff for the workspace. "
                       "By default shows unstaged changes; set cached=true for staged changes. "
                       "Optionally filter by pathspecs and control hunk context with context_lines (default 3).",
        .category = ToolCategory::ReadOnly,
        .mutates_repository_state = false,
        .can_execute_repo_controlled_code = false,
        .requires_approval = false,
        .skill_aliases = {"Read"},
        .json_schema = make_parameters_schema({
            {"cached", {
                {"type", "boolean"},
                {"description", "If true, show staged (cached) changes instead of unstaged ones. Default false."}
            }},
            {"pathspecs", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "Optional list of paths or patterns to restrict the diff."}
            }},
            {"context_lines", {
                {"type", "integer"},
                {"description", "Number of context lines around each hunk. Default 3."}
            }}
        }),
        .max_output_bytes = kMaxRepoOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            bool cached = false;
            if (cmd.arguments.contains("cached")) {
                const auto& v = cmd.arguments.at("cached");
                if (!v.is_boolean()) {
                    throw std::runtime_error("Argument 'cached' must be a boolean.");
                }
                cached = v.get<bool>();
            }
            const size_t context_lines = optional_git_context_lines_arg(cmd, "context_lines", 3);
            const std::vector<std::string> pathspecs = optional_string_array_arg_strict(cmd, "pathspecs");
            return git_diff(config.workspace_abs, cached, pathspecs, context_lines, output_limit);
        }
    });

    register_or_throw(&registry, ToolDescriptor{
        .name = "git_show",
        .description = "Shows information about a git object (commit, tag, etc.). "
                       "rev is required. Optionally disable patch or stat output, filter by pathspecs, "
                       "and control diff context with context_lines (default 3).",
        .category = ToolCategory::ReadOnly,
        .mutates_repository_state = false,
        .can_execute_repo_controlled_code = false,
        .requires_approval = false,
        .skill_aliases = {"Read"},
        .json_schema = make_parameters_schema({
            {"rev", {
                {"type", "string"},
                {"description", "The git revision to show (e.g. HEAD, a commit SHA, a tag)."}
            }},
            {"patch", {
                {"type", "boolean"},
                {"description", "Include the unified diff patch. Default true."}
            }},
            {"stat", {
                {"type", "boolean"},
                {"description", "Include the diffstat summary. Default true."}
            }},
            {"pathspecs", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "Optional list of paths to restrict the shown diff."}
            }},
            {"context_lines", {
                {"type", "integer"},
                {"description", "Number of context lines around each hunk. Default 3."}
            }}
        }, {"rev"}),
        .max_output_bytes = kMaxRepoOutputBytes,
        .execute = [](const ToolCall& cmd, const AgentConfig& config, size_t output_limit) {
            const std::string rev = require_string_arg(cmd, "rev", "git_show");
            bool patch = true;
            if (cmd.arguments.contains("patch")) {
                const auto& v = cmd.arguments.at("patch");
                if (!v.is_boolean()) {
                    throw std::runtime_error("Argument 'patch' must be a boolean.");
                }
                patch = v.get<bool>();
            }
            bool stat = true;
            if (cmd.arguments.contains("stat")) {
                const auto& v = cmd.arguments.at("stat");
                if (!v.is_boolean()) {
                    throw std::runtime_error("Argument 'stat' must be a boolean.");
                }
                stat = v.get<bool>();
            }
            const size_t context_lines = optional_git_context_lines_arg(cmd, "context_lines", 3);
            const std::vector<std::string> pathspecs = optional_string_array_arg_strict(cmd, "pathspecs");
            return git_show(config.workspace_abs, rev, patch, stat, pathspecs,
                            context_lines, output_limit);
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
