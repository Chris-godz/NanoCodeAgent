#pragma once

#include <string>
#include <optional>

struct AgentConfig {
    std::string prompt;
    std::string workspace;
    std::string workspace_abs; // Normalized absolute path to workspace
    std::string model;
    std::optional<std::string> api_key;
    std::string base_url;
    bool debug_mode;
    std::string config_file_path; // Path to loaded config file, if any
};

// Initialize configuration considering Defaults < Config File < ENV
AgentConfig config_init(int argc, char* argv[]);
