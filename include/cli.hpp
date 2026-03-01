#pragma once

#include "config.hpp"

enum class CliResult {
    Success,
    ExitSuccess, // For --help and --version
    ExitFailure  // For parsing errors
};

// Parse command line arguments and update the configuration
CliResult cli_parse(int argc, char* argv[], AgentConfig& config);
