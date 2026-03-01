#include "config.hpp"
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <iostream>

static void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
}

static std::string to_lower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c){ return std::tolower(c); });
    return res;
}

static void config_load_from_file(AgentConfig& config, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open config file: " << filepath << "\n";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        auto delim_pos = line.find('=');
        if (delim_pos == std::string::npos) continue;

        std::string key = line.substr(0, delim_pos);
        std::string val = line.substr(delim_pos + 1);
        trim(key);
        trim(val);
        
        key = to_lower(key);
        
        if (key == "model") config.model = val;
        else if (key == "api_key") config.api_key = val;
        else if (key == "base_url") config.base_url = val;
        else if (key == "workspace") config.workspace = val;
        else if (key == "debug") config.debug_mode = (val == "true" || val == "1");
    }
}

static void config_apply_env(AgentConfig& config) {
    if (const char* v = std::getenv("NCA_MODEL")) config.model = v;
    if (const char* v = std::getenv("NCA_API_KEY")) config.api_key = v;
    if (const char* v = std::getenv("NCA_BASE_URL")) config.base_url = v;
    if (const char* v = std::getenv("NCA_WORKSPACE")) config.workspace = v;
    if (const char* v = std::getenv("NCA_DEBUG")) config.debug_mode = (std::string(v) == "true" || std::string(v) == "1");
}

AgentConfig config_init(int argc, char* argv[]) {
    AgentConfig config;
    
    // 1. Default values
    config.workspace = ".";
    config.model = "gpt-4o";
    config.base_url = "https://api.openai.com/v1";
    config.debug_mode = false;

    // 2. Discover config file path (from CLI args first, then ENV)
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[i + 1];
            break;
        } else if (arg.starts_with("--config=")) {
            config_path = arg.substr(9);
            break;
        }
    }
    if (config_path.empty()) {
        if (const char* env_conf = std::getenv("NCA_CONFIG")) {
            config_path = env_conf;
        }
    }

    // 3. Load from config file if specified
    if (!config_path.empty()) {
        config.config_file_path = config_path;
        config_load_from_file(config, config_path);
    }

    // 4. Override with ENV variables
    config_apply_env(config);

    return config;
}
