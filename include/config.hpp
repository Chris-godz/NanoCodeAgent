#pragma once

#include <cstddef>
#include <string>
#include <optional>
#include <vector>

struct AgentConfig {
    std::string prompt;
    std::string workspace;
    std::string workspace_abs; // Normalized absolute path to workspace
    std::string model;
    std::optional<std::string> api_key;
    std::string base_url;
    bool debug_mode;
    std::string config_file_path; // Path to loaded config file, if any
    std::vector<std::string> enabled_skills;
    size_t max_skill_prompt_bytes = 32768;
    
    // Day 10 features
    std::string mode = "real"; // "real" or "mock"
    std::optional<std::string> mock_fixture;
    std::optional<std::string> system_prompt_file;
    std::optional<std::string> session_file;
    std::vector<std::string> mcp_servers;
    bool dry_run = false;

    // Agent limits / Brake system
    int max_turns = 20;
    int max_tool_calls_per_turn = 8;
    int max_total_tool_calls = 50;
    size_t max_tool_output_bytes = 16384; // 16KB
    size_t max_context_bytes = 204800;    // 200KB
    bool allow_mutating_tools = false;
    bool allow_execution_tools = false;
};

// Initialize configuration considering Defaults < Config File < ENV
AgentConfig config_init(int argc, char* argv[]);
