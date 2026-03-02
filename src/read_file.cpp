#include "read_file.hpp"
#include "workspace.hpp"
#include "config.hpp"

#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <system_error>
#include <cstring>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

// Checks whether content has embedded NUL characters representing implicit binary files.
static bool check_is_binary(const std::string& buffer) {
    if (buffer.find('\0') != std::string::npos) {
        return true;
    }
    return false;
}

ReadResult read_file_safe(const std::string& workspace_abs,
                          const std::string& rel_path,
                          size_t max_read_bytes) {
    // 1) Validate via workspace_resolve to prevent basic boundary escapes like ".."
    AgentConfig dummy_cfg;
    dummy_cfg.workspace_abs = workspace_abs;
    
    std::string safe_abs_path;
    std::string err_msg;
    if (!workspace_resolve(dummy_cfg, rel_path, &safe_abs_path, &err_msg)) {
        return {false, 0, "", "", false, false, "Path resolution failed: " + err_msg};
    }

    // 2) TOCTOU safe traversal preventing any symlink components
    int dir_fd = open(workspace_abs.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0) {
        return {false, 0, "", "", false, false, std::string("Failed to open workspace root: ") + strerror(errno)};
    }

    fs::path rel_p(rel_path);
    rel_p = rel_p.lexically_normal(); 

    // Extract directory components and the final filename correctly
    std::vector<std::string> components;
    for (const auto& p : rel_p) {
        if (p.string() != "." && p.string() != "/") { 
            components.push_back(p.string());
        }
    }

    if (components.empty()) {
        close(dir_fd);
        return {false, 0, "", "", false, false, "Invalid empty path target"};
    }

    std::string filename = components.back();
    components.pop_back();

    // Iterate through directory components safely confirming no symlinks exist
    for (const auto& comp : components) {
        int next_fd = openat(dir_fd, comp.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next_fd < 0) {
            std::string e = strerror(errno);
            close(dir_fd);
            return {false, 0, "", "", false, false, "Failed to securely traverse directory '" + comp + "': " + e};
        }
        close(dir_fd);
        dir_fd = next_fd;
    }

    // Ensure final file is securely opened without following symlinks. 
    // Opening it with O_NONBLOCK to prevent hanging on FIFOs/Sockets during the open() syscall itself.
    int fd = openat(dir_fd, filename.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        std::string e = strerror(errno);
        close(dir_fd);
        return {false, 0, "", "", false, false, "Failed to securely open target file '" + filename + "': " + e};
    }

    // Verify statistical state: avoid reading FIFOs/Sockets causing hanging.
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        close(dir_fd);
        return {false, 0, "", "", false, false, "Failed to stat file descriptor: " + std::string(strerror(errno))};
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);
        close(dir_fd);
        return {false, 0, safe_abs_path, "", false, false, "Target is not a statistically regular file type (blocked FIFO/Device file)"};
    }

    // Start cyclic buffered reading honoring truncations up to max_read_bytes
    bool truncated = false;
    std::string accumulated;
    
    // Allocate space cautiously avoiding massive upfront memory allocation
    size_t chunk_size = 8192; 
    std::vector<char> buffer(chunk_size);

    while (accumulated.size() < max_read_bytes) {
        size_t space_left = max_read_bytes - accumulated.size();
        size_t read_target = std::min(chunk_size, space_left);
        
        ssize_t bytes_read = read(fd, buffer.data(), read_target);
        
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue; 
            }
            std::string e = strerror(errno);
            close(fd);
            close(dir_fd);
            return {false, accumulated.size(), "", "", false, false, "Filesystem IO error during read: " + e};
        }
        
        if (bytes_read == 0) { // EOF
            break; 
        }

        accumulated.append(buffer.data(), static_cast<size_t>(bytes_read));
        
        if (accumulated.size() == max_read_bytes) {
            // Did we actually reach the EOF or only hit the limit cap?
            // To prove truncation try reading exactly 1 more byte.
            char test_byte;
            ssize_t test_read = read(fd, &test_byte, 1);
            if (test_read > 0) {
                truncated = true;
            }
            break;
        }
    }

    close(fd);
    close(dir_fd);

    // Apply binary policy: Policy 1 - Reject immediately with context.
    if (check_is_binary(accumulated)) {
        return {false, accumulated.size(), safe_abs_path, "", truncated, true, 
               "Detected binary content (NUL bytes). Aborting raw read to prevent corruption. "
               "Please utilize bash tools such as `hexdump` or `xxd` to analyze this file."};
    }

    return {true, accumulated.size(), safe_abs_path, accumulated, truncated, false, ""};
}
