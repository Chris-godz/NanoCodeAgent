#include "config.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <vector>

static void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
}

static std::string to_lower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c){ return std::tolower(c); });
    return res;
}

static bool parse_bool_like(const std::string& value) {
    const std::string lowered = to_lower(value);
    return lowered == "true" || lowered == "1";
}

static std::vector<std::string> split_csv_list(const std::string& value) {
    std::vector<std::string> items;
    std::size_t start = 0;

    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        std::string item = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        trim(item);
        if (!item.empty()) {
            items.push_back(item);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    return items;
}

static std::optional<std::string> normalize_mcp_server_spec(std::string value) {
    trim(value);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
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
        else if (key == "debug") config.debug_mode = parse_bool_like(val);
        else if (key == "max_turns") config.max_turns = std::stoi(val);
        else if (key == "max_tool_calls_per_turn") config.max_tool_calls_per_turn = std::stoi(val);
        else if (key == "max_total_tool_calls") config.max_total_tool_calls = std::stoi(val);
        else if (key == "max_tool_output_bytes") config.max_tool_output_bytes = std::stoull(val);
        else if (key == "max_context_bytes") config.max_context_bytes = std::stoull(val);
        else if (key == "mode") config.mode = val;
        else if (key == "mock_fixture") config.mock_fixture = val;
        else if (key == "system_prompt_file") config.system_prompt_file = val;
        else if (key == "session_file") config.session_file = val;
        else if (key == "detail") config.detail_mode = parse_bool_like(val);
        else if (key == "trace_jsonl") config.trace_jsonl = val;
        else if (key == "mcp_server") {
            if (auto normalized = normalize_mcp_server_spec(val)) {
                config.mcp_servers.push_back(*normalized);
            }
        }
        else if (key == "dry_run") config.dry_run = parse_bool_like(val);
        else if (key == "allow_mutating_tools") config.allow_mutating_tools = parse_bool_like(val);
        else if (key == "allow_execution_tools") config.allow_execution_tools = parse_bool_like(val);
        else if (key == "skills") config.enabled_skills = split_csv_list(val);
    }
}

static void config_apply_env(AgentConfig& config) {
    if (const char* v = std::getenv("NCA_MODEL")) config.model = v;
    if (const char* v = std::getenv("NCA_API_KEY")) config.api_key = v;
    if (const char* v = std::getenv("NCA_BASE_URL")) config.base_url = v;
    if (const char* v = std::getenv("NCA_WORKSPACE")) config.workspace = v;
    if (const char* v = std::getenv("NCA_DEBUG")) config.debug_mode = parse_bool_like(v);
    if (const char* v = std::getenv("NCA_MAX_TURNS")) config.max_turns = std::stoi(v);
    if (const char* v = std::getenv("NCA_MAX_TOOL_CALLS_PER_TURN")) config.max_tool_calls_per_turn = std::stoi(v);
    if (const char* v = std::getenv("NCA_MAX_TOTAL_TOOL_CALLS")) config.max_total_tool_calls = std::stoi(v);
    if (const char* v = std::getenv("NCA_MAX_TOOL_OUTPUT_BYTES")) config.max_tool_output_bytes = std::stoull(v);
    if (const char* v = std::getenv("NCA_MAX_CONTEXT_BYTES")) config.max_context_bytes = std::stoull(v);
    if (const char* v = std::getenv("NCA_MODE")) config.mode = v;
    if (const char* v = std::getenv("NCA_MOCK_FIXTURE")) config.mock_fixture = v;
    if (const char* v = std::getenv("NCA_SYSTEM_PROMPT_FILE")) config.system_prompt_file = v;
    if (const char* v = std::getenv("NCA_SESSION_FILE")) config.session_file = v;
    if (const char* v = std::getenv("NCA_DETAIL")) config.detail_mode = parse_bool_like(v);
    if (const char* v = std::getenv("NCA_TRACE_JSONL")) config.trace_jsonl = v;
    if (const char* v = std::getenv("NCA_MCP_SERVER")) {
        if (auto normalized = normalize_mcp_server_spec(v)) {
            config.mcp_servers = {*normalized};
        } else {
            config.mcp_servers.clear();
        }
    }
    if (const char* v = std::getenv("NCA_DRY_RUN")) config.dry_run = parse_bool_like(v);
    if (const char* v = std::getenv("NCA_ALLOW_MUTATING_TOOLS")) config.allow_mutating_tools = parse_bool_like(v);
    if (const char* v = std::getenv("NCA_ALLOW_EXECUTION_TOOLS")) config.allow_execution_tools = parse_bool_like(v);
    if (const char* v = std::getenv("NCA_SKILLS")) config.enabled_skills = split_csv_list(v);
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
