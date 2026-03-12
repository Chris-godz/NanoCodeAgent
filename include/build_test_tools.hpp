#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct BuildScriptResult {
    bool ok = false;
    std::string status;
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
    bool truncated = false;
    bool timed_out = false;
    std::string summary;
};

struct ParsedTestSummary {
    std::optional<int> passed_count;
    std::optional<int> failed_count;
    std::vector<std::string> failed_tests;
};

BuildScriptResult run_build_script_sequence(const std::string& workspace_abs,
                                            const std::vector<std::string>& subcommands,
                                            int timeout_ms,
                                            size_t max_output_bytes);

ParsedTestSummary parse_ctest_summary(const std::string& stdout_text,
                                      const std::string& stderr_text);
