#include "config.hpp"
#include "cli.hpp"
#include "logger.hpp"
#include "workspace.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. Initialize configuration with defaults, config file, and env vars
    // Priority: Defaults < Config File < ENV
    AgentConfig config = config_init(argc, argv);

    // 2. Parse command line arguments (highest priority)
    CliResult cli_res = cli_parse(argc, argv, config);
    if (cli_res == CliResult::ExitSuccess) {
        return 0; // Help or version requested
    } else if (cli_res == CliResult::ExitFailure) {
        return 1; // Parsing failed
    }

    // 3. Initialize workspace
    std::string ws_err;
    if (!workspace_init(&config, &ws_err)) {
        std::cerr << "Agent Error: Failed to initialize workspace: " << ws_err << "\n";
        return 1;
    }

    // 4. Initialize logger
    logger_init(config.debug_mode);

    // 5. Log configuration details (Debug level)
    LOG_DEBUG("Configuration loaded:");
    if (!config.config_file_path.empty()) {
        LOG_DEBUG("  Config path: {}", config.config_file_path);
    } else {
        LOG_DEBUG("  Config path: (None)");
    }
    LOG_DEBUG("  Workspace (rel): {}", config.workspace);
    LOG_DEBUG("  Workspace (abs): {}", config.workspace_abs);
    LOG_DEBUG("  Model: {}", config.model);
    LOG_DEBUG("  Base URL: {}", config.base_url);
    LOG_DEBUG("  API Key: {}", config.api_key.has_value() ? "***" : "Not set");
    LOG_DEBUG("  Prompt: {}", config.prompt);

    // 6. Stub for execution
    LOG_INFO("准备调用模型处理任务: {}", config.prompt);

    return 0;
}
