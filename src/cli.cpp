#include "cli.hpp"
#include <iostream>
#include <getopt.h>
#include <cstdlib>

void print_help() {
    std::cout << "Usage: agent [OPTIONS] -e <prompt>\n\n"
              << "A C++23 based coding agent.\n\n"
              << "Options:\n"
              << "  -e, --execute <prompt>   The task or prompt for the agent to execute (Required)\n"
              << "  -w, --workspace <dir>    Working directory for the agent (Default: .)\n"
              << "  --config <path>          Path to configuration file (Env: NCA_CONFIG)\n"
              << "  --model <name>           LLM model to use (Default: gpt-4o, Env: NCA_MODEL)\n"
              << "  --api-key <key>          API key for the model provider (Env: NCA_API_KEY)\n"
              << "  --base-url <url>         Base URL for the API (Env: NCA_BASE_URL)\n"
              << "  --mode <real|mock>       Run mode: real network or mock (Default: real, Env: NCA_MODE)\n"
              << "  --mock-fixture <path>    Path to mock fixture file when in mock mode (Env: NCA_MOCK_FIXTURE)\n"
              << "  --system-prompt-file <path> Custom system prompt file (Env: NCA_SYSTEM_PROMPT_FILE)\n"
              << "  --session-file <path>    Persist session state to a JSON file (Env: NCA_SESSION_FILE)\n"
              << "  --dry-run                Print what would be sent without making network requests (Env: NCA_DRY_RUN)\n"
              << "  --allow-mutating-tools   Allow approval-required mutating tools (Env: NCA_ALLOW_MUTATING_TOOLS)\n"
              << "  --allow-execution-tools  Allow approval-required execution tools (Env: NCA_ALLOW_EXECUTION_TOOLS)\n"
              << "  --skill <name>           Enable a runtime skill from .agents/skills (repeatable, Env: NCA_SKILLS)\n"
              << "  --max-turns <n>          Maximum conversation turns (Default: 20)\n"
              << "  --max-tool-calls-per-turn <n> Max tools per turn (Default: 8)\n"
              << "  --max-total-tool-calls <n> Max total tool calls (Default: 50)\n"
              << "  --max-tool-output-bytes <n> Max single tool output bytes (Default: 16384)\n"
              << "  --max-context-bytes <n>  Max accumulated context before sliding window (Default: 204800)\n"
              << "  --debug                  Enable verbose debug logging (Env: NCA_DEBUG)\n"
              << "  -h, --help               Show this help message and exit\n"
              << "  -v, --version            Show version information and exit\n\n"
              << "Example:\n"
              << "  agent -w /tmp/project -e \"Create a Python script\" --debug\n";
}

void print_version() {
    std::cout << "nano-agent version 0.1.0\n";
}

CliResult cli_parse(int argc, char* argv[], AgentConfig& config) {
    const char* const short_opts = "he:w:v";
    const option long_opts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, 'v'},
        {"execute", required_argument, nullptr, 'e'},
        {"workspace", required_argument, nullptr, 'w'},
        {"config", required_argument, nullptr, 1004},
        {"model", required_argument, nullptr, 1000},
        {"api-key", required_argument, nullptr, 1001},
        {"base-url", required_argument, nullptr, 1002},
        {"debug", no_argument, nullptr, 1003},
        {"max-turns", required_argument, nullptr, 2000},
        {"max-tool-calls-per-turn", required_argument, nullptr, 2001},
        {"max-total-tool-calls", required_argument, nullptr, 2002},
        {"max-tool-output-bytes", required_argument, nullptr, 2003},
        {"max-context-bytes", required_argument, nullptr, 2004},
        {"mode", required_argument, nullptr, 3000},
        {"mock-fixture", required_argument, nullptr, 3001},
        {"system-prompt-file", required_argument, nullptr, 3002},
        {"dry-run", no_argument, nullptr, 3003},
        {"allow-mutating-tools", no_argument, nullptr, 3004},
        {"allow-execution-tools", no_argument, nullptr, 3005},
        {"skill", required_argument, nullptr, 3006},
        {"session-file", required_argument, nullptr, 3007},
        {nullptr, no_argument, nullptr, 0}
    };

    // Ensure getopt_long state is completely reset to allow multiple parsing in tests
    optind = 1;

    int opt;
    bool saw_skill_flag = false;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                print_help();
                return CliResult::ExitSuccess;
            case 'v':
                print_version();
                return CliResult::ExitSuccess;
            case 'e':
                config.prompt = optarg;
                break;
            case 'w':
                config.workspace = optarg;
                break;
            case 1004:
                config.config_file_path = optarg;
                break;
            case 1000:
                config.model = optarg;
                break;
            case 1001:
                config.api_key = optarg;
                break;
            case 1002:
                config.base_url = optarg;
                break;
            case 1003:
                config.debug_mode = true;
                break;
            case 2000:
                config.max_turns = std::stoi(optarg);
                break;
            case 2001:
                config.max_tool_calls_per_turn = std::stoi(optarg);
                break;
            case 2002:
                config.max_total_tool_calls = std::stoi(optarg);
                break;
            case 2003:
                config.max_tool_output_bytes = std::stoull(optarg);
                break;
            case 2004:
                config.max_context_bytes = std::stoull(optarg);
                break;
            case 3000:
                config.mode = optarg;
                break;
            case 3001:
                config.mock_fixture = optarg;
                break;
            case 3002:
                config.system_prompt_file = optarg;
                break;
            case 3007:
                config.session_file = optarg;
                break;
            case 3003:
                config.dry_run = true;
                break;
            case 3004:
                config.allow_mutating_tools = true;
                break;
            case 3005:
                config.allow_execution_tools = true;
                break;
            case 3006:
                if (!saw_skill_flag) {
                    config.enabled_skills.clear();
                    saw_skill_flag = true;
                }
                config.enabled_skills.push_back(optarg);
                break;
            case '?':
            default:
                std::cerr << "Try 'agent --help' for more information.\n";
                return CliResult::ExitFailure;
        }
    }

    if (config.prompt.empty()) {
        std::cerr << "Error: Missing required argument '-e' or '--execute'.\n";
        std::cerr << "Try 'agent --help' for more information.\n";
        return CliResult::ExitFailure;
    }

    return CliResult::Success;
}
