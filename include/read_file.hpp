#pragma once

#include <string>

struct ReadResult {
    bool ok;
    size_t bytes_read;
    std::string abs_path;
    std::string content;
    bool truncated;
    bool is_binary;
    std::string err;
};

/**
 * @brief Safely read a file preventing Symlink/TOCTOU escapes, detecting binary and enforcing size limits.
 * 
 * @param workspace_abs The absolute path to the workspace root.
 * @param rel_path The relative path representing the target file.
 * @param max_read_bytes Upper bound for payload extraction (defaults to 4MB).
 * @return ReadResult Struct indicating operation success, metrics, content payload, boolean flags and errors.
 */
ReadResult read_file_safe(const std::string& workspace_abs,
                          const std::string& rel_path,
                          size_t max_read_bytes = 4 * 1024 * 1024);
