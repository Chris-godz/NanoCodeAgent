#pragma once

#include <cstddef>
#include <string>

struct SubprocessResult {
    bool ok = false;
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
    bool timed_out = false;
    bool truncated = false;
    std::size_t stdout_bytes = 0;
    std::size_t stderr_bytes = 0;
    std::string error;
};

SubprocessResult run_subprocess_capture(const std::string& working_dir,
                                        const std::string& command,
                                        int timeout_ms = 3600000,
                                        std::size_t max_stdout_bytes = 8 * 1024 * 1024,
                                        std::size_t max_stderr_bytes = 8 * 1024 * 1024);
