#include "repo_tools.hpp"

#include "workspace.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <functional>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr size_t kDefaultListMaxResults = 100;
constexpr size_t kMaxListMaxResults = 500;
constexpr size_t kDefaultRgMaxResults = 50;
constexpr size_t kMaxRgMaxResults = 200;
constexpr size_t kDefaultGitMaxEntries = 200;
constexpr size_t kMaxGitMaxEntries = 500;
constexpr size_t kDefaultSnippetBytes = 160;
constexpr size_t kMaxSnippetBytes = 400;
constexpr size_t kMaxCommandStderrBytes = 8192;

std::string g_rg_binary_override;

struct ChildProcess {
    pid_t pid = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;
};

struct GitTextCommandResult {
    bool launched = false;
    std::string stdout_text;
    std::string stderr_text;
    int exit_code = -1;
    bool truncated = false;
    bool killed_early = false;
    std::string err;
};

nlohmann::json make_git_add_error_result(std::string stdout_text,
                                         std::string stderr_text,
                                         int exit_code,
                                         bool truncated,
                                         std::string error) {
    return {
        {"ok", false},
        {"stdout", std::move(stdout_text)},
        {"stderr", std::move(stderr_text)},
        {"exit_code", exit_code},
        {"truncated", truncated},
        {"error", std::move(error)}
    };
}

nlohmann::json make_git_commit_error_result(std::string stdout_text,
                                            std::string stderr_text,
                                            int exit_code,
                                            std::string commit_sha,
                                            bool truncated,
                                            std::string error) {
    return {
        {"ok", false},
        {"stdout", std::move(stdout_text)},
        {"stderr", std::move(stderr_text)},
        {"exit_code", exit_code},
        {"commit_sha", std::move(commit_sha)},
        {"truncated", truncated},
        {"error", std::move(error)}
    };
}

void close_fd(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

void kill_child_group(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    if (killpg(pid, SIGKILL) != 0) {
        kill(pid, SIGKILL);
    }
}

std::string normalize_directory_display(const std::string& directory) {
    return directory.empty() ? "." : directory;
}

size_t clamp_limit(size_t value, size_t default_value, size_t hard_max) {
    size_t resolved = value == 0 ? default_value : value;
    return std::min(resolved, hard_max);
}

std::string trim_line_endings(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

std::string limit_text(std::string text, size_t max_bytes) {
    text = trim_line_endings(std::move(text));
    if (max_bytes == 0 || text.size() <= max_bytes) {
        return text;
    }
    if (max_bytes <= 3) {
        return text.substr(0, max_bytes);
    }
    return text.substr(0, max_bytes - 3) + "...";
}

bool append_bounded_output_line(std::string* output,
                                const std::string& line,
                                size_t output_limit,
                                bool* truncated) {
    if (!output) {
        return false;
    }

    if (output_limit == 0) {
        output->append(line);
        output->push_back('\n');
        return true;
    }

    if (output->size() >= output_limit) {
        if (truncated) {
            *truncated = true;
        }
        return false;
    }

    const size_t remaining = output_limit - output->size();
    if (line.size() <= remaining) {
        output->append(line);
        if (line.size() < remaining) {
            output->push_back('\n');
        }
        return true;
    }

    output->append(line, 0, remaining);
    if (truncated) {
        *truncated = true;
    }
    return false;
}

bool stdout_buffer_exceeds_limit(const std::string& stdout_buffer,
                                 size_t output_limit) {
    return output_limit > 0 && stdout_buffer.size() > output_limit;
}

bool flush_excess_stdout_buffer(std::string* stdout_buffer,
                                const std::function<bool(const std::string&)>& on_line,
                                bool* killed_early,
                                pid_t child_pid,
                                size_t output_limit) {
    if (!stdout_buffer || output_limit == 0 || !stdout_buffer_exceeds_limit(*stdout_buffer, output_limit)) {
        return true;
    }

    const size_t first_newline = stdout_buffer->find('\n');
    if (first_newline != std::string::npos && first_newline <= output_limit) {
        return true;
    }

    if (on_line && !on_line(*stdout_buffer)) {
        if (killed_early) {
            *killed_early = true;
        }
        kill_child_group(child_pid);
        return false;
    }

    stdout_buffer->clear();
    return true;
}

std::string ascii_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string normalize_git_repo_error(std::string failure) {
    failure = trim_line_endings(std::move(failure));
    const std::string folded = ascii_lower(failure);
    if (folded.find("not a git repository") != std::string::npos) {
        return "Current workspace is not a git repository.";
    }
    return failure;
}

std::string normalize_git_commit_error(const std::string& stdout_text,
                                       const std::string& stderr_text) {
    std::string combined;
    if (!stdout_text.empty()) {
        combined += stdout_text;
    }
    if (!stderr_text.empty()) {
        if (!combined.empty()) {
            combined.push_back('\n');
        }
        combined += stderr_text;
    }
    combined = trim_line_endings(std::move(combined));
    const std::string folded = ascii_lower(combined);
    if (folded.find("nothing to commit") != std::string::npos ||
        folded.find("no changes added to commit") != std::string::npos) {
        return "No staged changes to commit.";
    }
    return normalize_git_repo_error(combined);
}

void append_bounded_stderr_segment(std::string* stderr_text,
                                   const std::string& segment,
                                   bool* truncated) {
    if (!stderr_text || segment.empty()) {
        return;
    }

    if (!stderr_text->empty() && stderr_text->size() < kMaxCommandStderrBytes) {
        stderr_text->push_back('\n');
    }

    const size_t remaining = stderr_text->size() >= kMaxCommandStderrBytes
                                 ? 0
                                 : kMaxCommandStderrBytes - stderr_text->size();
    const size_t keep = std::min(segment.size(), remaining);
    stderr_text->append(segment, 0, keep);
    if (keep < segment.size() && truncated) {
        *truncated = true;
    }
}

size_t clamp_git_context_lines(size_t context_lines) {
    return std::min(context_lines, kMaxGitContextLines);
}

std::string find_executable_in_path(const std::string& name) {
    if (name.empty()) {
        return "";
    }

    if (name.find('/') != std::string::npos) {
        return access(name.c_str(), X_OK) == 0 ? name : "";
    }

    // Use the process PATH environment variable so binaries installed via
    // Homebrew, Nix, or custom toolchains are found, not just the three
    // hardcoded directories.
    const char* path_env = std::getenv("PATH");
    if (path_env) {
        const std::string path_str(path_env);
        std::string::size_type start = 0;
        while (start <= path_str.size()) {
            const std::string::size_type end = path_str.find(':', start);
            const std::string dir = (end == std::string::npos)
                ? path_str.substr(start)
                : path_str.substr(start, end - start);
            if (!dir.empty()) {
                const fs::path candidate = fs::path(dir) / name;
                if (access(candidate.c_str(), X_OK) == 0) {
                    return candidate.string();
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return "";
    }

    // Fallback when PATH is not set in the environment.
    const std::vector<std::string> search_roots = {
        "/usr/bin",
        "/usr/local/bin",
        "/bin"
    };
    for (const auto& root : search_roots) {
        const fs::path candidate = fs::path(root) / name;
        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }

    return "";
}

bool secure_resolve_directory(const std::string& workspace_abs,
                              const std::string& directory,
                              std::string* resolved_abs,
                              std::string* resolved_rel,
                              std::string* err) {
    if (workspace_abs.empty()) {
        if (err) *err = "Workspace is not initialized";
        return false;
    }

    fs::path rel_path = directory.empty() ? fs::path(".") : fs::path(directory).lexically_normal();
    std::string normalized_rel = rel_path.generic_string();
    if (normalized_rel.empty() || normalized_rel == ".") {
        if (resolved_abs) *resolved_abs = workspace_abs;
        if (resolved_rel) *resolved_rel = "";
        return true;
    }

    AgentConfig cfg;
    cfg.workspace_abs = workspace_abs;

    std::string safe_abs;
    std::string resolve_err;
    if (!workspace_resolve(cfg, normalized_rel, &safe_abs, &resolve_err)) {
        if (err) *err = "Path resolution failed: " + resolve_err;
        return false;
    }

    int dir_fd = open(workspace_abs.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0) {
        if (err) *err = std::string("Failed to open workspace root: ") + strerror(errno);
        return false;
    }

    std::vector<std::string> components;
    for (const auto& part : rel_path) {
        const std::string component = part.string();
        if (component.empty() || component == "." || component == "/") {
            continue;
        }
        components.push_back(component);
    }

    for (const auto& component : components) {
        int next_fd = openat(dir_fd, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next_fd < 0) {
            const std::string message = "Failed to securely open directory '" + component + "': " + strerror(errno);
            close_fd(dir_fd);
            if (err) *err = message;
            return false;
        }
        close_fd(dir_fd);
        dir_fd = next_fd;
    }

    close_fd(dir_fd);

    if (resolved_abs) *resolved_abs = safe_abs;
    if (resolved_rel) *resolved_rel = normalized_rel == "." ? "" : normalized_rel;
    return true;
}

std::vector<std::string> normalize_extensions(const std::vector<std::string>& extensions) {
    std::vector<std::string> normalized;
    normalized.reserve(extensions.size());

    for (const auto& extension : extensions) {
        if (extension.empty()) {
            continue;
        }
        if (extension.front() == '.') {
            normalized.push_back(extension);
        } else {
            normalized.push_back("." + extension);
        }
    }

    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

bool extension_matches(const std::vector<std::string>& extensions, const fs::path& path) {
    if (extensions.empty()) {
        return true;
    }
    const std::string ext = path.extension().string();
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

void mark_result_truncated(nlohmann::json* result, bool* truncated) {
    if (result) {
        (*result)["truncated"] = true;
        if (result->contains("returned") && result->contains("files") && (*result)["files"].is_array()) {
            (*result)["returned"] = (*result)["files"].size();
        }
        if (result->contains("returned") && result->contains("matches") && (*result)["matches"].is_array()) {
            (*result)["returned"] = (*result)["matches"].size();
        }
        if (result->contains("returned") && result->contains("entries") && (*result)["entries"].is_array()) {
            (*result)["returned"] = (*result)["entries"].size();
        }
    }
    if (truncated) {
        *truncated = true;
    }
}

bool append_bounded_array_item(nlohmann::json* result,
                               const char* array_key,
                               nlohmann::json item,
                               size_t output_limit,
                               bool* truncated) {
    if (!result || !array_key) {
        return false;
    }

    auto& arr = (*result)[array_key];
    arr.push_back(std::move(item));
    (*result)["returned"] = arr.size();

    if (output_limit == 0 || result->dump().size() <= output_limit) {
        return true;
    }

    arr.erase(arr.end() - 1);
    (*result)["returned"] = arr.size();
    mark_result_truncated(result, truncated);
    return false;
}

void finalize_bounded_result(nlohmann::json* result, const char* array_key, size_t output_limit, bool truncated) {
    if (!result || !array_key) {
        return;
    }
    if (result->contains(array_key) && (*result)[array_key].is_array()) {
        (*result)["returned"] = (*result)[array_key].size();
    }
    if (truncated) {
        (*result)["truncated"] = true;
    }
    if (output_limit > 0 && result->dump().size() > output_limit) {
        (*result)["truncated"] = true;
    }
}

bool walk_directory_bounded(const fs::path& abs_dir,
                            const std::string& rel_dir,
                            const std::vector<std::string>& extensions,
                            size_t max_results,
                            nlohmann::json* result,
                            size_t output_limit,
                            bool* truncated,
                            std::string* err) {
    std::error_code ec;
    fs::directory_iterator iter(abs_dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        if (err) *err = "Failed to iterate directory '" + abs_dir.string() + "': " + ec.message();
        return false;
    }

    std::vector<fs::directory_entry> entries;
    for (const auto& entry : iter) {
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& lhs, const fs::directory_entry& rhs) {
        return lhs.path().filename().generic_string() < rhs.path().filename().generic_string();
    });

    for (const auto& entry : entries) {
        std::error_code status_ec;
        const fs::file_status status = entry.symlink_status(status_ec);
        if (status_ec) {
            if (err) *err = "Failed to stat path '" + entry.path().string() + "': " + status_ec.message();
            return false;
        }

        if (fs::is_symlink(status)) {
            continue;
        }

        const std::string name = entry.path().filename().generic_string();
        const std::string rel_path = rel_dir.empty() ? name : rel_dir + "/" + name;

        if (fs::is_directory(status)) {
            if (name == ".git") {
                continue;
            }
            if (!walk_directory_bounded(entry.path(), rel_path, extensions, max_results, result, output_limit, truncated, err)) {
                return false;
            }
            if (*truncated) {
                return true;
            }
            continue;
        }

        if (!fs::is_regular_file(status) || !extension_matches(extensions, entry.path())) {
            continue;
        }

        std::error_code size_ec;
        const auto size = entry.file_size(size_ec);
        if (size_ec) {
            if (err) *err = "Failed to query file size for '" + entry.path().string() + "': " + size_ec.message();
            return false;
        }

        if (result->contains("files") && (*result)["files"].is_array() && (*result)["files"].size() >= max_results) {
            mark_result_truncated(result, truncated);
            return true;
        }

        if (!append_bounded_array_item(result,
                                       "files",
                                       {
            {"path", rel_path},
            {"size", size}
        },
                                       output_limit,
                                       truncated)) {
            return true;
        }
    }

    return true;
}

bool spawn_process(const std::string& executable,
                   const std::vector<std::string>& args,
                   const std::string& cwd,
                   ChildProcess* child,
                   std::string* err) {
    if (!child) {
        if (err) *err = "Null child process pointer";
        return false;
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        if (err) *err = std::string("Failed to create pipes: ") + strerror(errno);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
        if (err) *err = std::string("Fork failed: ") + strerror(errno);
        return false;
    }

    if (pid == 0) {
        setpgid(0, 0);

        close_fd(stdout_pipe[0]);
        close_fd(stderr_pipe[0]);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[1]);

        if (chdir(cwd.c_str()) != 0) {
            dprintf(STDERR_FILENO, "failed to chdir to '%s': %s\n", cwd.c_str(), strerror(errno));
            _exit(126);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execv(executable.c_str(), argv.data());
        dprintf(STDERR_FILENO, "failed to exec '%s': %s\n", executable.c_str(), strerror(errno));
        _exit(127);
    }

    close_fd(stdout_pipe[1]);
    close_fd(stderr_pipe[1]);

    set_nonblocking(stdout_pipe[0]);
    set_nonblocking(stderr_pipe[0]);

    child->pid = pid;
    child->stdout_fd = stdout_pipe[0];
    child->stderr_fd = stderr_pipe[0];
    return true;
}

bool stream_command_stdout_lines(const std::string& executable,
                                 const std::vector<std::string>& args,
                                 const std::string& cwd,
                                 const std::function<bool(const std::string&)>& on_line,
                                 std::string* stderr_text,
                                 int* exit_code,
                                 bool* killed_early,
                                 bool* stderr_truncated,
                                 std::string* err,
                                 size_t output_limit = 0) {
    ChildProcess child;
    if (!spawn_process(executable, args, cwd, &child, err)) {
        return false;
    }

    if (stderr_text) stderr_text->clear();
    if (exit_code) *exit_code = -1;
    if (killed_early) *killed_early = false;
    if (stderr_truncated) *stderr_truncated = false;

    std::string stdout_buffer;
    char buffer[4096];

    while (child.stdout_fd >= 0 || child.stderr_fd >= 0) {
        struct pollfd pfds[2];
        nfds_t count = 0;
        if (child.stdout_fd >= 0) {
            pfds[count++] = {child.stdout_fd, POLLIN | POLLHUP | POLLERR, 0};
        }
        if (child.stderr_fd >= 0) {
            pfds[count++] = {child.stderr_fd, POLLIN | POLLHUP | POLLERR, 0};
        }

        int poll_result = poll(pfds, count, -1);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            kill_child_group(child.pid);
            close_fd(child.stdout_fd);
            close_fd(child.stderr_fd);
            child.stdout_fd = -1;
            child.stderr_fd = -1;
            if (err) *err = std::string("poll failed: ") + strerror(errno);
            break;
        }

        nfds_t idx = 0;
        if (child.stdout_fd >= 0) {
            if (pfds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                while (true) {
                    ssize_t n = read(child.stdout_fd, buffer, sizeof(buffer));
                    if (n > 0) {
                        stdout_buffer.append(buffer, static_cast<size_t>(n));
                        if (!flush_excess_stdout_buffer(&stdout_buffer, on_line, killed_early,
                                                        child.pid, output_limit)) {
                            break;
                        }
                        size_t newline = stdout_buffer.find('\n');
                        while (newline != std::string::npos) {
                            std::string line = stdout_buffer.substr(0, newline);
                            stdout_buffer.erase(0, newline + 1);
                            if (on_line && !on_line(line)) {
                                if (killed_early) *killed_early = true;
                                kill_child_group(child.pid);
                                break;
                            }
                            newline = stdout_buffer.find('\n');
                        }
                        if (!flush_excess_stdout_buffer(&stdout_buffer, on_line, killed_early,
                                                        child.pid, output_limit)) {
                            break;
                        }
                        if (killed_early && *killed_early) {
                            break;
                        }
                        continue;
                    }
                    if (n == 0) {
                        close_fd(child.stdout_fd);
                        child.stdout_fd = -1;
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    close_fd(child.stdout_fd);
                    child.stdout_fd = -1;
                    if (err) *err = std::string("Failed reading stdout: ") + strerror(errno);
                    kill_child_group(child.pid);
                    break;
                }
            }
            ++idx;
        }

        if (killed_early && *killed_early) {
            break;
        }

        if (child.stderr_fd >= 0) {
            if (pfds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                while (true) {
                    ssize_t n = read(child.stderr_fd, buffer, sizeof(buffer));
                    if (n > 0) {
                        if (stderr_text && stderr_text->size() < kMaxCommandStderrBytes) {
                            const size_t keep = std::min(static_cast<size_t>(n), kMaxCommandStderrBytes - stderr_text->size());
                            stderr_text->append(buffer, keep);
                            if (keep < static_cast<size_t>(n) && stderr_truncated) {
                                *stderr_truncated = true;
                            }
                        } else if (stderr_truncated) {
                            *stderr_truncated = true;
                        }
                        continue;
                    }
                    if (n == 0) {
                        close_fd(child.stderr_fd);
                        child.stderr_fd = -1;
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    close_fd(child.stderr_fd);
                    child.stderr_fd = -1;
                    if (err) *err = std::string("Failed reading stderr: ") + strerror(errno);
                    kill_child_group(child.pid);
                    break;
                }
            }
        }
    }

    if ((!killed_early || !*killed_early) && !stdout_buffer.empty() && on_line && (!err || err->empty())) {
        if (!on_line(stdout_buffer)) {
            if (killed_early) *killed_early = true;
            kill_child_group(child.pid);
        }
    }

    close_fd(child.stdout_fd);
    close_fd(child.stderr_fd);

    int wait_status = 0;
    if (waitpid(child.pid, &wait_status, 0) < 0) {
        if (err && err->empty()) {
            *err = std::string("waitpid failed: ") + strerror(errno);
        }
        return false;
    }

    if (WIFEXITED(wait_status)) {
        if (exit_code) *exit_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        if (exit_code) *exit_code = 128 + WTERMSIG(wait_status);
    }

    return err == nullptr || err->empty();
}

bool stream_command_stdout_records(const std::string& executable,
                                   const std::vector<std::string>& args,
                                   const std::string& cwd,
                                   char delimiter,
                                   const std::function<bool(const std::string&)>& on_record,
                                   std::string* stderr_text,
                                   int* exit_code,
                                   bool* killed_early,
                                   std::string* err) {
    ChildProcess child;
    if (!spawn_process(executable, args, cwd, &child, err)) {
        return false;
    }

    if (stderr_text) stderr_text->clear();
    if (exit_code) *exit_code = -1;
    if (killed_early) *killed_early = false;

    std::string stdout_buffer;
    char buffer[4096];

    while (child.stdout_fd >= 0 || child.stderr_fd >= 0) {
        struct pollfd pfds[2];
        nfds_t count = 0;
        if (child.stdout_fd >= 0) {
            pfds[count++] = {child.stdout_fd, POLLIN | POLLHUP | POLLERR, 0};
        }
        if (child.stderr_fd >= 0) {
            pfds[count++] = {child.stderr_fd, POLLIN | POLLHUP | POLLERR, 0};
        }

        int poll_result = poll(pfds, count, -1);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            kill_child_group(child.pid);
            close_fd(child.stdout_fd);
            close_fd(child.stderr_fd);
            child.stdout_fd = -1;
            child.stderr_fd = -1;
            if (err) *err = std::string("poll failed: ") + strerror(errno);
            break;
        }

        nfds_t idx = 0;
        if (child.stdout_fd >= 0) {
            if (pfds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                while (true) {
                    ssize_t n = read(child.stdout_fd, buffer, sizeof(buffer));
                    if (n > 0) {
                        stdout_buffer.append(buffer, static_cast<size_t>(n));
                        size_t delim_pos = stdout_buffer.find(delimiter);
                        while (delim_pos != std::string::npos) {
                            std::string record = stdout_buffer.substr(0, delim_pos);
                            stdout_buffer.erase(0, delim_pos + 1);
                            if (on_record && !on_record(record)) {
                                if (killed_early) *killed_early = true;
                                kill_child_group(child.pid);
                                break;
                            }
                            delim_pos = stdout_buffer.find(delimiter);
                        }
                        if (killed_early && *killed_early) {
                            break;
                        }
                        continue;
                    }
                    if (n == 0) {
                        close_fd(child.stdout_fd);
                        child.stdout_fd = -1;
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    close_fd(child.stdout_fd);
                    child.stdout_fd = -1;
                    if (err) *err = std::string("Failed reading stdout: ") + strerror(errno);
                    kill_child_group(child.pid);
                    break;
                }
            }
            ++idx;
        }

        if (killed_early && *killed_early) {
            break;
        }

        if (child.stderr_fd >= 0) {
            if (pfds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                while (true) {
                    ssize_t n = read(child.stderr_fd, buffer, sizeof(buffer));
                    if (n > 0) {
                        if (stderr_text && stderr_text->size() < kMaxCommandStderrBytes) {
                            const size_t keep = std::min(static_cast<size_t>(n), kMaxCommandStderrBytes - stderr_text->size());
                            stderr_text->append(buffer, keep);
                        }
                        continue;
                    }
                    if (n == 0) {
                        close_fd(child.stderr_fd);
                        child.stderr_fd = -1;
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    close_fd(child.stderr_fd);
                    child.stderr_fd = -1;
                    if (err) *err = std::string("Failed reading stderr: ") + strerror(errno);
                    kill_child_group(child.pid);
                    break;
                }
            }
        }
    }

    if ((!killed_early || !*killed_early) && !stdout_buffer.empty() && on_record && (!err || err->empty())) {
        if (!on_record(stdout_buffer)) {
            if (killed_early) *killed_early = true;
            kill_child_group(child.pid);
        }
    }

    close_fd(child.stdout_fd);
    close_fd(child.stderr_fd);

    int wait_status = 0;
    if (waitpid(child.pid, &wait_status, 0) < 0) {
        if (err && err->empty()) {
            *err = std::string("waitpid failed: ") + strerror(errno);
        }
        return false;
    }

    if (WIFEXITED(wait_status)) {
        if (exit_code) *exit_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        if (exit_code) *exit_code = 128 + WTERMSIG(wait_status);
    }

    return err == nullptr || err->empty();
}

GitTextCommandResult run_git_text_command(const std::string& git_binary,
                                          const std::vector<std::string>& args,
                                          const std::string& workspace_abs,
                                          size_t output_limit) {
    GitTextCommandResult result;
    bool stderr_truncated = false;
    auto on_line = [&](const std::string& line) -> bool {
        return append_bounded_output_line(&result.stdout_text, line, output_limit, &result.truncated);
    };

    result.launched = stream_command_stdout_lines(git_binary, args, workspace_abs,
                                                  on_line, &result.stderr_text, &result.exit_code,
                                                  &result.killed_early, &stderr_truncated,
                                                  &result.err, output_limit);
    if (!result.stdout_text.empty() && result.stdout_text.back() == '\n') {
        result.stdout_text.pop_back();
    }
    result.stderr_text = trim_line_endings(std::move(result.stderr_text));
    result.truncated = result.truncated || stderr_truncated;
    return result;
}

bool starts_with_path_component(const std::string& path, const std::string& prefix) {
    if (prefix.empty()) {
        return true;
    }
    return path == prefix || path.starts_with(prefix + "/");
}

bool resolve_workspace_relative_git_path(const std::string& workspace_abs,
                                         const std::string& input,
                                         std::string* normalized_rel,
                                         std::string* err) {
    if (input.empty()) {
        if (err) *err = "Argument 'pathspecs' for git_add must not contain empty path strings.";
        return false;
    }
    if (input.front() == ':') {
        if (err) *err = "Git pathspec magic is not supported for git_add.";
        return false;
    }

    AgentConfig cfg;
    cfg.workspace_abs = workspace_abs;

    std::string safe_abs;
    std::string resolve_err;
    if (!workspace_resolve(cfg, input, &safe_abs, &resolve_err)) {
        if (err) *err = "Path must stay within the workspace: " + resolve_err;
        return false;
    }

    const fs::path rel_path = fs::path(safe_abs).lexically_relative(fs::path(workspace_abs));
    if (rel_path.empty()) {
        if (normalized_rel) *normalized_rel = ".";
    } else {
        if (normalized_rel) *normalized_rel = rel_path.generic_string();
    }
    return true;
}

bool get_repo_root_and_workspace_prefix(const std::string& git_binary,
                                        const std::string& workspace_abs,
                                        std::string* repo_root,
                                        std::string* workspace_prefix,
                                        std::string* err) {
    const std::vector<std::string> args = {git_binary, "rev-parse", "--show-toplevel"};
    GitTextCommandResult result = run_git_text_command(git_binary, args, workspace_abs, 0);
    if (!result.launched) {
        if (err) *err = result.err.empty() ? result.stderr_text : result.err;
        return false;
    }
    if (result.exit_code != 0) {
        if (err) *err = normalize_git_repo_error(result.stderr_text);
        return false;
    }

    const fs::path repo_root_path = fs::path(trim_line_endings(result.stdout_text)).lexically_normal();
    const fs::path workspace_path = fs::path(workspace_abs).lexically_normal();
    const fs::path relative = workspace_path.lexically_relative(repo_root_path);
    const std::string relative_text = relative.generic_string();
    if (relative.empty() && workspace_path != repo_root_path) {
        if (err) *err = "Current workspace is not inside the git repository root.";
        return false;
    }
    if (relative_text == ".." || relative_text.starts_with("../")) {
        if (err) *err = "Current workspace is not inside the git repository root.";
        return false;
    }

    if (repo_root) *repo_root = repo_root_path.string();
    if (workspace_prefix) *workspace_prefix = (relative_text == "." ? "" : relative_text);
    return true;
}

bool list_staged_paths(const std::string& git_binary,
                       const std::string& workspace_abs,
                       std::vector<std::string>* staged_paths,
                       std::string* stderr_text,
                       int* exit_code,
                       std::string* err) {
    const std::vector<std::string> args = {
        git_binary,
        "diff",
        "--cached",
        "--name-status",
        "-z",
        "--"
    };

    if (staged_paths) {
        staged_paths->clear();
    }

    bool killed_early = false;
    std::vector<std::string> records;
    const bool launched = stream_command_stdout_records(
        git_binary,
        args,
        workspace_abs,
        '\0',
        [&](const std::string& record) -> bool {
            records.push_back(record);
            return true;
        },
        stderr_text,
        exit_code,
        &killed_early,
        err);
    if (!launched) {
        return false;
    }

    for (size_t i = 0; i < records.size(); ++i) {
        const std::string& status = records[i];
        if (status.empty()) {
            continue;
        }

        const auto push_path = [&](const std::string& path_text) {
            if (!path_text.empty() && staged_paths) {
                staged_paths->push_back(fs::path(path_text).lexically_normal().generic_string());
            }
        };

        const char code = status[0];
        if ((code == 'R' || code == 'C') && status.size() >= 2) {
            if (i + 2 >= records.size()) {
                if (err) *err = "Failed to parse staged rename/copy paths before git commit.";
                return false;
            }
            push_path(records[i + 1]);
            push_path(records[i + 2]);
            i += 2;
            continue;
        }

        if (i + 1 >= records.size()) {
            if (err) *err = "Failed to parse staged paths before git commit.";
            return false;
        }
        push_path(records[i + 1]);
        i += 1;
    }

    return true;
}

bool resolve_head_sha(const std::string& git_binary,
                      const std::string& workspace_abs,
                      std::string* head_sha,
                      std::string* err) {
    const std::vector<std::string> args = {git_binary, "rev-parse", "HEAD"};
    GitTextCommandResult result = run_git_text_command(git_binary, args, workspace_abs, 0);
    if (!result.launched) {
        if (err) *err = result.err.empty() ? result.stderr_text : result.err;
        return false;
    }
    if (result.exit_code != 0) {
        const std::string normalized = normalize_git_repo_error(result.stderr_text);
        const std::string folded = ascii_lower(normalized);
        if (folded.find("unknown revision or path not in the working tree") != std::string::npos ||
            folded.find("ambiguous argument 'head'") != std::string::npos ||
            folded.find("needed a single revision") != std::string::npos) {
            if (head_sha) head_sha->clear();
            return true;
        }
        if (err) *err = normalized;
        return false;
    }

    if (head_sha) {
        *head_sha = trim_line_endings(result.stdout_text);
    }
    return true;
}

bool list_head_reflog_shas(const std::string& git_binary,
                           const std::string& workspace_abs,
                           std::vector<std::string>* shas,
                           std::string* err) {
    const std::vector<std::string> args = {git_binary, "reflog", "--format=%H", "HEAD"};
    GitTextCommandResult result = run_git_text_command(git_binary, args, workspace_abs, 0);
    if (!result.launched) {
        if (err) *err = result.err.empty() ? result.stderr_text : result.err;
        return false;
    }
    if (result.exit_code != 0) {
        const std::string normalized = normalize_git_repo_error(result.stderr_text);
        const std::string folded = ascii_lower(normalized);
        if (folded.find("unknown revision or path not in the working tree") != std::string::npos ||
            folded.find("ambiguous argument 'head'") != std::string::npos ||
            folded.find("needed a single revision") != std::string::npos ||
            folded.find("your current branch 'main' does not have any commits yet") != std::string::npos ||
            folded.find("your current branch does not have any commits yet") != std::string::npos) {
            if (shas) shas->clear();
            return true;
        }
        if (err) *err = normalized;
        return false;
    }

    if (shas) {
        shas->clear();
        std::istringstream lines(result.stdout_text);
        for (std::string line; std::getline(lines, line);) {
            line = trim_line_endings(std::move(line));
            if (!line.empty()) {
                shas->push_back(line);
            }
        }
    }
    return true;
}

struct GitCommitObservedState {
    std::string head_sha;
    std::vector<std::string> reflog_shas;
};

bool capture_git_commit_observed_state(const std::string& git_binary,
                                       const std::string& workspace_abs,
                                       const std::string& head_failure_message,
                                       const std::string& reflog_failure_message,
                                       GitCommitObservedState* state,
                                       std::string* err) {
    if (!state) {
        if (err) *err = "Internal error: missing git commit observed state output.";
        return false;
    }

    state->head_sha.clear();
    state->reflog_shas.clear();

    std::string head_err;
    if (!resolve_head_sha(git_binary, workspace_abs, &state->head_sha, &head_err)) {
        if (err) *err = head_err.empty() ? head_failure_message : head_err;
        return false;
    }

    std::string reflog_err;
    if (!list_head_reflog_shas(git_binary, workspace_abs, &state->reflog_shas, &reflog_err)) {
        if (err) *err = reflog_err.empty() ? reflog_failure_message : reflog_err;
        return false;
    }

    return true;
}

std::string first_new_commit_from_reflog(const std::vector<std::string>& before,
                                         const std::vector<std::string>& after) {
    if (after.size() < before.size()) {
        return "";
    }

    const size_t new_count = after.size() - before.size();
    for (size_t i = 0; i < before.size(); ++i) {
        if (after[new_count + i] != before[i]) {
            return "";
        }
    }

    if (new_count == 0) {
        return "";
    }
    return after[new_count - 1];
}

bool resolve_created_commit_sha_from_ancestry(const std::string& git_binary,
                                              const std::string& workspace_abs,
                                              const std::string& head_before,
                                              const std::string& head_after,
                                              std::string* commit_sha,
                                              std::string* err) {
    if (commit_sha) {
        commit_sha->clear();
    }
    if (head_after.empty()) {
        return true;
    }

    std::vector<std::string> args = {git_binary, "rev-list", "--reverse"};
    if (head_before.empty()) {
        args.push_back(head_after);
    } else {
        args.push_back("--ancestry-path");
        args.push_back(head_before + ".." + head_after);
    }

    GitTextCommandResult result = run_git_text_command(git_binary, args, workspace_abs, 0);
    if (!result.launched) {
        if (err) *err = result.err.empty() ? result.stderr_text : result.err;
        return false;
    }
    if (result.exit_code != 0) {
        if (err) *err = normalize_git_repo_error(result.stderr_text);
        return false;
    }

    std::istringstream lines(result.stdout_text);
    std::string first_commit;
    for (std::string line; std::getline(lines, line);) {
        line = trim_line_endings(std::move(line));
        if (!line.empty()) {
            first_commit = line;
            break;
        }
    }

    if (commit_sha) {
        *commit_sha = first_commit;
    }
    return true;
}

bool resolve_created_commit_sha(const std::string& git_binary,
                                const std::string& workspace_abs,
                                const std::string& head_before,
                                const std::string& head_after,
                                const std::vector<std::string>& reflog_before,
                                const std::vector<std::string>& reflog_after,
                                std::string* commit_sha,
                                std::string* err) {
    if (commit_sha) {
        commit_sha->clear();
    }

    const std::string from_reflog = first_new_commit_from_reflog(reflog_before, reflog_after);
    if (!from_reflog.empty()) {
        if (commit_sha) {
            *commit_sha = from_reflog;
        }
        return true;
    }

    return resolve_created_commit_sha_from_ancestry(
        git_binary, workspace_abs, head_before, head_after, commit_sha, err);
}

std::string join_relative_path(const std::string& base, const std::string& child) {
    if (base.empty()) {
        return fs::path(child).lexically_normal().generic_string();
    }
    return (fs::path(base) / child).lexically_normal().generic_string();
}

void parse_branch_header(const std::string& line, nlohmann::json* result) {
    if (!result || line.rfind("## ", 0) != 0) {
        return;
    }

    const std::string payload = line.substr(3);
    if (payload.rfind("No commits yet on ", 0) == 0) {
        (*result)["branch"] = payload.substr(std::strlen("No commits yet on "));
        return;
    }

    if (payload == "HEAD (no branch)") {
        (*result)["branch"] = "HEAD";
        return;
    }

    const size_t bracket_pos = payload.find(" [");
    const std::string ref_part = payload.substr(0, bracket_pos);
    const size_t dots = ref_part.find("...");
    if (dots == std::string::npos) {
        (*result)["branch"] = ref_part;
    } else {
        (*result)["branch"] = ref_part.substr(0, dots);
        (*result)["upstream"] = ref_part.substr(dots + 3);
    }

    if (bracket_pos == std::string::npos) {
        return;
    }

    const size_t close_bracket = payload.find(']', bracket_pos + 2);
    if (close_bracket == std::string::npos) {
        return;
    }

    const std::string stats = payload.substr(bracket_pos + 2, close_bracket - (bracket_pos + 2));
    const size_t ahead_pos = stats.find("ahead ");
    if (ahead_pos != std::string::npos) {
        (*result)["ahead"] = std::stoi(stats.substr(ahead_pos + std::strlen("ahead ")));
    }
    const size_t behind_pos = stats.find("behind ");
    if (behind_pos != std::string::npos) {
        (*result)["behind"] = std::stoi(stats.substr(behind_pos + std::strlen("behind ")));
    }
}

void parse_status_line(const std::string& line, nlohmann::json* entries) {
    if (!entries || line.size() < 3) {
        return;
    }

    const std::string status = line.substr(0, 2);
    std::string path = line.substr(3);
    nlohmann::json entry = {
        {"path", path},
        {"index_status", std::string(1, status[0])},
        {"worktree_status", std::string(1, status[1])}
    };

    // Only rename (R) and copy (C) status codes use " -> " as a path separator.
    // Untracked or modified files can legitimately have " -> " in their filename.
    if (status[0] == 'R' || status[0] == 'C' || status[1] == 'R' || status[1] == 'C') {
        // Git C-quotes any path that contains special characters (spaces, arrows, etc.).
        // When the ORIGINAL path starts with '"' it is C-quoted; the " -> " separator
        // must appear immediately after the closing quote.  When the original path is
        // unquoted, git guarantees that it contains no literal " -> ", so the first
        // occurrence is always the correct separator. Using this scheme handles all four
        // combinations (orig quoted/unquoted × dest quoted/unquoted) correctly.
        size_t arrow = std::string::npos;
        if (!path.empty() && path[0] == '"') {
            // Scan for the closing unescaped quote.
            size_t i = 1;
            while (i < path.size()) {
                if (path[i] == '\\') {
                    i += 2; // skip escape sequence
                } else if (path[i] == '"') {
                    // Separator must follow the closing quote immediately.
                    if (i + 1 < path.size() && path.substr(i + 1, 4) == " -> ") {
                        arrow = i + 1;
                    }
                    break;
                } else {
                    ++i;
                }
            }
        } else {
            // Unquoted original: " -> " cannot appear inside it per git's quoting rules.
            arrow = path.find(" -> ");
        }
        if (arrow != std::string::npos) {
            entry["orig_path"] = path.substr(0, arrow);
            entry["path"] = path.substr(arrow + 4);
        }
    }

    entries->push_back(entry);
}

bool status_record_uses_orig_path(const std::string& status) {
    return status.size() == 2 && (status[0] == 'R' || status[0] == 'C' || status[1] == 'R' || status[1] == 'C');
}

nlohmann::json make_status_entry(const std::string& status, const std::string& path) {
    return nlohmann::json{
        {"path", path},
        {"index_status", std::string(1, status[0])},
        {"worktree_status", std::string(1, status[1])}
    };
}

} // namespace

void set_rg_binary_for_testing(const std::string& path) {
    g_rg_binary_override = path;
}

void clear_rg_binary_for_testing() {
    g_rg_binary_override.clear();
}

nlohmann::json list_files_bounded(const std::string& workspace_abs,
                                  const std::string& directory,
                                  const std::vector<std::string>& extensions,
                                  size_t max_results,
                                  size_t output_limit) {
    const size_t limit = clamp_limit(max_results, kDefaultListMaxResults, kMaxListMaxResults);

    std::string abs_directory;
    std::string rel_directory;
    std::string err;
    if (!secure_resolve_directory(workspace_abs, directory, &abs_directory, &rel_directory, &err)) {
        return {
            {"ok", false},
            {"directory", normalize_directory_display(directory)},
            {"extensions", normalize_extensions(extensions)},
            {"files", nlohmann::json::array()},
            {"returned", 0},
            {"truncated", false},
            {"error", err}
        };
    }

    const std::vector<std::string> normalized_extensions = normalize_extensions(extensions);
    nlohmann::json result = {
        {"ok", true},
        {"directory", normalize_directory_display(rel_directory)},
        {"extensions", normalized_extensions},
        {"files", nlohmann::json::array()},
        {"returned", 0},
        {"truncated", false},
        {"error", ""}
    };
    bool truncated = false;
    if (!walk_directory_bounded(abs_directory, rel_directory, normalized_extensions, limit, &result, output_limit, &truncated, &err)) {
        return {
            {"ok", false},
            {"directory", normalize_directory_display(rel_directory)},
            {"extensions", normalized_extensions},
            {"files", nlohmann::json::array()},
            {"returned", 0},
            {"truncated", false},
            {"error", err}
        };
    }

    finalize_bounded_result(&result, "files", output_limit, truncated);
    return result;
}

nlohmann::json rg_search(const std::string& workspace_abs,
                         const std::string& query,
                         const std::string& directory,
                         size_t max_results,
                         size_t max_snippet_bytes,
                         size_t output_limit) {
    if (query.empty()) {
        return {
            {"ok", false},
            {"directory", normalize_directory_display(directory)},
            {"matches", nlohmann::json::array()},
            {"returned", 0},
            {"truncated", false},
            {"error", "Missing 'query' argument for rg_search."}
        };
    }

    std::string abs_directory;
    std::string rel_directory;
    std::string err;
    if (!secure_resolve_directory(workspace_abs, directory, &abs_directory, &rel_directory, &err)) {
        return {
            {"ok", false},
            {"directory", normalize_directory_display(directory)},
            {"matches", nlohmann::json::array()},
            {"returned", 0},
            {"truncated", false},
            {"error", err}
        };
    }

    const std::string rg_binary = !g_rg_binary_override.empty() ? g_rg_binary_override : find_executable_in_path("rg");
    if (rg_binary.empty()) {
        return {
            {"ok", false},
            {"directory", normalize_directory_display(rel_directory)},
            {"matches", nlohmann::json::array()},
            {"returned", 0},
            {"truncated", false},
            {"error", "ripgrep ('rg') is not installed or not executable in this environment."}
        };
    }

    const size_t limit = clamp_limit(max_results, kDefaultRgMaxResults, kMaxRgMaxResults);
    const size_t snippet_limit = clamp_limit(max_snippet_bytes, kDefaultSnippetBytes, kMaxSnippetBytes);

    std::vector<std::string> args = {
        rg_binary,
        "--json",
        "--line-number",
        "--color",
        "never",
        "-e",
        query,
        "."
    };

    nlohmann::json result = {
        {"ok", true},
        {"directory", normalize_directory_display(rel_directory)},
        {"matches", nlohmann::json::array()},
        {"returned", 0},
        {"truncated", false},
        {"error", ""}
    };
    std::string stderr_text;
    int exit_code = -1;
    bool killed_early = false;
    bool truncated = false;
    bool parse_failed = false;
    std::string parse_error;

    auto on_line = [&](const std::string& line) -> bool {
        if (line.empty()) {
            return true;
        }

        nlohmann::json event;
        try {
            event = nlohmann::json::parse(line);
        } catch (const std::exception& e) {
            parse_failed = true;
            parse_error = std::string("Failed to parse rg JSON output: ") + e.what();
            return false;
        }

        if (event.value("type", "") != "match" || !event.contains("data")) {
            return true;
        }

        const auto& data = event["data"];
        std::string file_path = ".";
        if (data.contains("path") && data["path"].contains("text") && data["path"]["text"].is_string()) {
            file_path = data["path"]["text"].get<std::string>();
        }

        int line_number = 0;
        if (data.contains("line_number") && data["line_number"].is_number_integer()) {
            line_number = data["line_number"].get<int>();
        }

        int column = 1;
        if (data.contains("submatches") && data["submatches"].is_array() && !data["submatches"].empty()) {
            const auto& first = data["submatches"][0];
            if (first.contains("start") && first["start"].is_number_integer()) {
                column = first["start"].get<int>() + 1;
            }
        }

        std::string snippet;
        if (data.contains("lines") && data["lines"].contains("text") && data["lines"]["text"].is_string()) {
            snippet = data["lines"]["text"].get<std::string>();
        }

        if (result["matches"].size() >= limit) {
            mark_result_truncated(&result, &truncated);
            return false;
        }

        if (!append_bounded_array_item(&result,
                                       "matches",
                                       {
            {"file", join_relative_path(rel_directory, file_path)},
            {"line", line_number},
            {"column", column},
            {"snippet", limit_text(std::move(snippet), snippet_limit)}
        },
                                       output_limit,
                                       &truncated)) {
            return false;
        }
        return true;
    };

    if (!stream_command_stdout_lines(rg_binary, args, abs_directory, on_line, &stderr_text, &exit_code, &killed_early,
                                     nullptr, &err)) {
        if (parse_failed) {
            err = parse_error;
        } else if (err.empty()) {
            err = trim_line_endings(stderr_text);
        }

        return {
            {"ok", false},
            {"directory", normalize_directory_display(rel_directory)},
            {"matches", nlohmann::json::array()},
            {"returned", 0},
            {"truncated", false},
            {"error", err.empty() ? "rg_search failed." : err}
        };
    }

    if (parse_failed) {
        return {
            {"ok", false},
            {"directory", normalize_directory_display(rel_directory)},
            {"matches", nlohmann::json::array()},
            {"returned", 0},
            {"truncated", false},
            {"error", parse_error}
        };
    }

    if (killed_early && truncated) {
        finalize_bounded_result(&result, "matches", output_limit, true);
        return result;
    }

    if (exit_code == 0 || exit_code == 1) {
        finalize_bounded_result(&result, "matches", output_limit, truncated);
        return result;
    }

    const std::string failure = trim_line_endings(stderr_text);
    return {
        {"ok", false},
        {"directory", normalize_directory_display(rel_directory)},
        {"matches", nlohmann::json::array()},
        {"returned", 0},
        {"truncated", false},
        {"error", failure.empty() ? "rg exited with code " + std::to_string(exit_code) : failure}
    };
}

nlohmann::json git_status(const std::string& workspace_abs, size_t max_entries, size_t output_limit) {
    const std::string git_binary = find_executable_in_path("git");
    if (git_binary.empty()) {
        return {
            {"ok", false},
            {"branch", ""},
            {"upstream", ""},
            {"ahead", 0},
            {"behind", 0},
            {"has_changes", false},
            {"entries", nlohmann::json::array()},
            {"truncated", false},
            {"error", "git is not installed or not executable in this environment."}
        };
    }

    const size_t limit = clamp_limit(max_entries, kDefaultGitMaxEntries, kMaxGitMaxEntries);
    std::vector<std::string> args = {
        git_binary,
        "status",
        "--porcelain=v1",
        "--branch",
        "-z"
    };

    nlohmann::json result = {
        {"ok", true},
        {"branch", ""},
        {"upstream", ""},
        {"ahead", 0},
        {"behind", 0},
        {"has_changes", false},
        {"entries", nlohmann::json::array()},
        {"returned", 0},
        {"truncated", false},
        {"error", ""}
    };
    std::string stderr_text;
    int exit_code = -1;
    bool killed_early = false;
    bool truncated = false;
    std::string err;
    bool parse_failed = false;
    std::string parse_error;
    bool awaiting_orig_path = false;
    nlohmann::json pending_entry;

    nlohmann::json header = {
        {"branch", ""},
        {"upstream", ""},
        {"ahead", 0},
        {"behind", 0}
    };

    auto on_record = [&](const std::string& record) -> bool {
        if (record.empty()) {
            return true;
        }

        if (record.rfind("## ", 0) == 0) {
            parse_branch_header(record, &header);
            return true;
        }

        if (awaiting_orig_path) {
            pending_entry["orig_path"] = record;
            awaiting_orig_path = false;

            if (result["entries"].size() >= limit) {
                mark_result_truncated(&result, &truncated);
                return false;
            }

            if (!append_bounded_array_item(&result, "entries", pending_entry, output_limit, &truncated)) {
                return false;
            }
            return true;
        }

        if (record.size() < 3) {
            parse_failed = true;
            parse_error = "Malformed git status record.";
            return false;
        }

        const std::string status = record.substr(0, 2);
        const std::string path = record.substr(3);
        nlohmann::json entry = make_status_entry(status, path);

        if (status_record_uses_orig_path(status)) {
            pending_entry = std::move(entry);
            awaiting_orig_path = true;
            return true;
        }

        if (result["entries"].size() >= limit) {
            mark_result_truncated(&result, &truncated);
            return false;
        }

        if (!append_bounded_array_item(&result, "entries", entry, output_limit, &truncated)) {
            return false;
        }
        return true;
    };

    if (!stream_command_stdout_records(git_binary, args, workspace_abs, '\0', on_record, &stderr_text, &exit_code, &killed_early, &err)) {
        if (err.empty()) {
            err = trim_line_endings(stderr_text);
        }
        return {
            {"ok", false},
            {"branch", header["branch"]},
            {"upstream", header["upstream"]},
            {"ahead", header["ahead"]},
            {"behind", header["behind"]},
            {"has_changes", false},
            {"entries", nlohmann::json::array()},
            {"truncated", false},
            {"error", err.empty() ? "git_status failed." : err}
        };
    }

    if (parse_failed) {
        return {
            {"ok", false},
            {"branch", header["branch"]},
            {"upstream", header["upstream"]},
            {"ahead", header["ahead"]},
            {"behind", header["behind"]},
            {"has_changes", false},
            {"entries", nlohmann::json::array()},
            {"truncated", false},
            {"error", parse_error}
        };
    }

    if (awaiting_orig_path) {
        return {
            {"ok", false},
            {"branch", header["branch"]},
            {"upstream", header["upstream"]},
            {"ahead", header["ahead"]},
            {"behind", header["behind"]},
            {"has_changes", false},
            {"entries", nlohmann::json::array()},
            {"truncated", false},
            {"error", "Malformed git status rename/copy record."}
        };
    }

    if (killed_early && truncated) {
        result["branch"] = header["branch"];
        result["upstream"] = header["upstream"];
        result["ahead"] = header["ahead"];
        result["behind"] = header["behind"];
        result["has_changes"] = !result["entries"].empty();
        finalize_bounded_result(&result, "entries", output_limit, true);
        return result;
    }

    if (exit_code != 0) {
        const std::string failure = normalize_git_repo_error(stderr_text);
        return {
            {"ok", false},
            {"branch", ""},
            {"upstream", ""},
            {"ahead", 0},
            {"behind", 0},
            {"has_changes", false},
            {"entries", nlohmann::json::array()},
            {"truncated", false},
            {"error", failure.empty() ? "git status exited with code " + std::to_string(exit_code) : failure}
        };
    }

    result["branch"] = header["branch"];
    result["upstream"] = header["upstream"];
    result["ahead"] = header["ahead"];
    result["behind"] = header["behind"];
    result["has_changes"] = !result["entries"].empty();
    finalize_bounded_result(&result, "entries", output_limit, truncated);
    return result;
}

nlohmann::json git_diff(const std::string& workspace_abs,
                        bool cached,
                        const std::vector<std::string>& pathspecs,
                        size_t context_lines,
                        size_t output_limit) {
    context_lines = clamp_git_context_lines(context_lines);
    const std::string git_binary = find_executable_in_path("git");
    if (git_binary.empty()) {
        return {
            {"ok", false},
            {"stdout", ""},
            {"stderr", ""},
            {"exit_code", -1},
            {"truncated", false},
            {"error", "git is not installed or not executable in this environment."}
        };
    }

    std::vector<std::string> args = {
        git_binary, "--no-pager", "diff",
        "--no-ext-diff",
        "--no-textconv",
        "--no-color",
        "-U" + std::to_string(context_lines)
    };
    if (cached) {
        args.push_back("--cached");
    }
    args.push_back("--");
    for (const auto& ps : pathspecs) {
        args.push_back(ps);
    }

    std::string stdout_text;
    std::string stderr_text;
    int exit_code = -1;
    bool killed_early = false;
    bool truncated = false;
    std::string err;

    auto on_line = [&](const std::string& line) -> bool {
        return append_bounded_output_line(&stdout_text, line, output_limit, &truncated);
    };

    if (!stream_command_stdout_lines(git_binary, args, workspace_abs,
                                     on_line, &stderr_text, &exit_code,
                                     &killed_early, nullptr, &err, output_limit)) {
        if (err.empty()) {
            err = trim_line_endings(stderr_text);
        }
        return {
            {"ok", false},
            {"stdout", ""},
            {"stderr", trim_line_endings(stderr_text)},
            {"exit_code", exit_code},
            {"truncated", false},
            {"error", err.empty() ? "git diff failed." : err}
        };
    }

    if (killed_early && truncated) {
        exit_code = 0;
    }

    if (exit_code != 0) {
        const std::string failure = normalize_git_repo_error(stderr_text);
        return {
            {"ok", false},
            {"stdout", ""},
            {"stderr", trim_line_endings(stderr_text)},
            {"exit_code", exit_code},
            {"truncated", false},
            {"error", failure.empty() ? "git diff exited with code " + std::to_string(exit_code) : failure}
        };
    }

    // Trim trailing newline added by last on_line call
    if (!stdout_text.empty() && stdout_text.back() == '\n') {
        stdout_text.pop_back();
    }

    return {
        {"ok", true},
        {"stdout", stdout_text},
        {"stderr", trim_line_endings(stderr_text)},
        {"exit_code", exit_code},
        {"truncated", truncated},
        {"error", ""}
    };
}

nlohmann::json git_show(const std::string& workspace_abs,
                        const std::string& rev,
                        bool patch,
                        bool stat,
                        const std::vector<std::string>& pathspecs,
                        size_t context_lines,
                        size_t output_limit) {
    context_lines = clamp_git_context_lines(context_lines);
    if (rev.empty()) {
        return {
            {"ok", false},
            {"stdout", ""},
            {"stderr", ""},
            {"exit_code", -1},
            {"truncated", false},
            {"error", "Missing 'rev' argument for git_show."}
        };
    }
    if (rev.front() == '-') {
        return {
            {"ok", false},
            {"stdout", ""},
            {"stderr", ""},
            {"exit_code", -1},
            {"truncated", false},
            {"error", "Argument 'rev' for git_show must not start with '-'."}
        };
    }

    const std::string git_binary = find_executable_in_path("git");
    if (git_binary.empty()) {
        return {
            {"ok", false},
            {"stdout", ""},
            {"stderr", ""},
            {"exit_code", -1},
            {"truncated", false},
            {"error", "git is not installed or not executable in this environment."}
        };
    }

    if (!patch && stat) {
        std::string stdout_text;
        std::string stderr_text;
        bool truncated = false;

        auto on_line = [&](const std::string& line) -> bool {
            return append_bounded_output_line(&stdout_text, line, output_limit, &truncated);
        };

        const std::vector<std::string> show_args = {
            git_binary, "--no-pager", "show",
            "-s",
            "--no-ext-diff",
            "--no-textconv",
            "--no-color",
            rev
        };

        std::string show_stderr;
        int show_exit_code = -1;
        bool show_killed_early = false;
        bool show_stderr_truncated = false;
        std::string show_err;
        if (!stream_command_stdout_lines(git_binary, show_args, workspace_abs,
                                         on_line, &show_stderr, &show_exit_code,
                                         &show_killed_early, &show_stderr_truncated,
                                         &show_err, output_limit)) {
            if (show_err.empty()) {
                show_err = trim_line_endings(show_stderr);
            }
            return {
                {"ok", false},
                {"stdout", ""},
                {"stderr", trim_line_endings(show_stderr)},
                {"exit_code", show_exit_code},
                {"truncated", false},
                {"error", show_err.empty() ? "git show failed." : show_err}
            };
        }

        show_stderr = trim_line_endings(std::move(show_stderr));
        append_bounded_stderr_segment(&stderr_text, show_stderr, &truncated);
        if (show_stderr_truncated) {
            truncated = true;
        }
        if (show_killed_early && truncated) {
            show_exit_code = 0;
        }
        if (show_exit_code != 0) {
            const std::string failure = normalize_git_repo_error(show_stderr);
            return {
                {"ok", false},
                {"stdout", ""},
                {"stderr", show_stderr},
                {"exit_code", show_exit_code},
                {"truncated", false},
                {"error", failure.empty() ? "git show exited with code " + std::to_string(show_exit_code) : failure}
            };
        }
        if (show_killed_early && truncated) {
            if (!stdout_text.empty() && stdout_text.back() == '\n') {
                stdout_text.pop_back();
            }
            return {
                {"ok", true},
                {"stdout", stdout_text},
                {"stderr", stderr_text},
                {"exit_code", show_exit_code},
                {"truncated", true},
                {"error", ""}
            };
        }

        if (!append_bounded_output_line(&stdout_text, "", output_limit, &truncated)) {
            if (!stdout_text.empty() && stdout_text.back() == '\n') {
                stdout_text.pop_back();
            }
            return {
                {"ok", true},
                {"stdout", stdout_text},
                {"stderr", stderr_text},
                {"exit_code", 0},
                {"truncated", true},
                {"error", ""}
            };
        }

        std::vector<std::string> stat_args = {
            git_binary, "--no-pager", "diff-tree",
            "--stat",
            "--root",
            "-r",
            "--no-color",
            "--no-commit-id",
            "--no-ext-diff",
            "--no-textconv",
            rev
        };
        if (!pathspecs.empty()) {
            stat_args.push_back("--");
            for (const auto& ps : pathspecs) {
                stat_args.push_back(ps);
            }
        }

        std::string stat_stderr;
        int stat_exit_code = -1;
        bool stat_killed_early = false;
        bool stat_stderr_truncated = false;
        std::string stat_err;
        if (!stream_command_stdout_lines(git_binary, stat_args, workspace_abs,
                                         on_line, &stat_stderr, &stat_exit_code,
                                         &stat_killed_early, &stat_stderr_truncated,
                                         &stat_err, output_limit)) {
            if (stat_err.empty()) {
                stat_err = trim_line_endings(stat_stderr);
            }
            return {
                {"ok", false},
                {"stdout", ""},
                {"stderr", trim_line_endings(stat_stderr)},
                {"exit_code", stat_exit_code},
                {"truncated", false},
                {"error", stat_err.empty() ? "git show failed." : stat_err}
            };
        }

        stat_stderr = trim_line_endings(std::move(stat_stderr));
        append_bounded_stderr_segment(&stderr_text, stat_stderr, &truncated);
        if (stat_stderr_truncated) {
            truncated = true;
        }
        if (stat_killed_early && truncated) {
            stat_exit_code = 0;
        }
        if (stat_exit_code != 0) {
            const std::string failure = normalize_git_repo_error(stat_stderr);
            return {
                {"ok", false},
                {"stdout", ""},
                {"stderr", stat_stderr},
                {"exit_code", stat_exit_code},
                {"truncated", false},
                {"error", failure.empty() ? "git show exited with code " + std::to_string(stat_exit_code) : failure}
            };
        }

        if (!stdout_text.empty() && stdout_text.back() == '\n') {
            stdout_text.pop_back();
        }

        return {
            {"ok", true},
            {"stdout", stdout_text},
            {"stderr", stderr_text},
            {"exit_code", stat_exit_code},
            {"truncated", truncated},
            {"error", ""}
        };
    }

    std::vector<std::string> args = {
        git_binary, "--no-pager", "show",
        "--no-ext-diff",
        "--no-textconv",
        "--no-color",
        "-U" + std::to_string(context_lines)
    };
    if (!patch) {
        args.push_back("--no-patch");
    }
    if (stat) {
        args.push_back("--stat");
    }
    args.push_back(rev);
    if (!pathspecs.empty()) {
        args.push_back("--");
        for (const auto& ps : pathspecs) {
            args.push_back(ps);
        }
    }

    std::string stdout_text;
    std::string stderr_text;
    int exit_code = -1;
    bool killed_early = false;
    bool truncated = false;
    std::string err;

    auto on_line = [&](const std::string& line) -> bool {
        return append_bounded_output_line(&stdout_text, line, output_limit, &truncated);
    };

    if (!stream_command_stdout_lines(git_binary, args, workspace_abs,
                                     on_line, &stderr_text, &exit_code,
                                     &killed_early, nullptr, &err, output_limit)) {
        if (err.empty()) {
            err = trim_line_endings(stderr_text);
        }
        return {
            {"ok", false},
            {"stdout", ""},
            {"stderr", trim_line_endings(stderr_text)},
            {"exit_code", exit_code},
            {"truncated", false},
            {"error", err.empty() ? "git show failed." : err}
        };
    }

    if (killed_early && truncated) {
        exit_code = 0;
    }

    if (exit_code != 0) {
        const std::string failure = normalize_git_repo_error(stderr_text);
        return {
            {"ok", false},
            {"stdout", ""},
            {"stderr", trim_line_endings(stderr_text)},
            {"exit_code", exit_code},
            {"truncated", false},
            {"error", failure.empty() ? "git show exited with code " + std::to_string(exit_code) : failure}
        };
    }

    // Trim trailing newline
    if (!stdout_text.empty() && stdout_text.back() == '\n') {
        stdout_text.pop_back();
    }

    return {
        {"ok", true},
        {"stdout", stdout_text},
        {"stderr", trim_line_endings(stderr_text)},
        {"exit_code", exit_code},
        {"truncated", truncated},
        {"error", ""}
    };
}

nlohmann::json git_add(const std::string& workspace_abs,
                       const std::vector<std::string>& pathspecs,
                       size_t output_limit) {
    if (pathspecs.empty()) {
        return make_git_add_error_result(
            "", "", -1, false, "Argument 'pathspecs' for git_add must contain at least one pathspec.");
    }

    const std::string git_binary = find_executable_in_path("git");
    if (git_binary.empty()) {
        return make_git_add_error_result(
            "", "", -1, false, "git is not installed or not executable in this environment.");
    }

    std::vector<std::string> safe_paths;
    safe_paths.reserve(pathspecs.size());
    for (const auto& pathspec : pathspecs) {
        std::string normalized_rel;
        std::string validate_err;
        if (!resolve_workspace_relative_git_path(workspace_abs, pathspec, &normalized_rel, &validate_err)) {
            return make_git_add_error_result("", "", -1, false, std::move(validate_err));
        }
        safe_paths.push_back(std::move(normalized_rel));
    }

    std::vector<std::string> args = {git_binary, "add", "--"};
    args.insert(args.end(), safe_paths.begin(), safe_paths.end());

    GitTextCommandResult command = run_git_text_command(git_binary, args, workspace_abs, output_limit);
    if (!command.launched) {
        if (command.err.empty()) {
            command.err = command.stderr_text;
        }
        return make_git_add_error_result(
            "", command.stderr_text, command.exit_code, command.truncated,
            command.err.empty() ? "git add failed." : command.err);
    }

    if (command.killed_early && command.truncated) {
        return make_git_add_error_result(command.stdout_text,
                                         command.stderr_text,
                                         -1,
                                         command.truncated,
                                         "git add output exceeded the configured limit before the command completed.");
    }

    if (command.exit_code != 0) {
        const std::string failure = normalize_git_repo_error(command.stderr_text);
        return make_git_add_error_result(command.stdout_text,
                                         command.stderr_text,
                                         command.exit_code,
                                         command.truncated,
                                         failure.empty() ? "git add exited with code " + std::to_string(command.exit_code)
                                                         : failure);
    }

    return {
        {"ok", true},
        {"stdout", command.stdout_text},
        {"stderr", command.stderr_text},
        {"exit_code", command.exit_code},
        {"truncated", command.truncated},
        {"error", ""}
    };
}

nlohmann::json git_commit(const std::string& workspace_abs,
                          const std::string& message,
                          size_t output_limit) {
    if (message.empty()) {
        return make_git_commit_error_result(
            "", "", -1, "", false, "Argument 'message' for git_commit must not be empty.");
    }

    const std::string git_binary = find_executable_in_path("git");
    if (git_binary.empty()) {
        return make_git_commit_error_result(
            "", "", -1, "", false, "git is not installed or not executable in this environment.");
    }

    std::string repo_root;
    std::string workspace_prefix;
    std::string repo_err;
    if (!get_repo_root_and_workspace_prefix(git_binary, workspace_abs, &repo_root, &workspace_prefix, &repo_err)) {
        return make_git_commit_error_result(
            "", "", -1, "", false, repo_err.empty() ? "Failed to resolve git repository root." : repo_err);
    }

    std::vector<std::string> staged_paths;
    std::string staged_stderr;
    int staged_exit_code = -1;
    std::string staged_err;
    if (!list_staged_paths(git_binary, workspace_abs, &staged_paths, &staged_stderr, &staged_exit_code, &staged_err)) {
        return make_git_commit_error_result("",
                                            trim_line_endings(staged_stderr),
                                            staged_exit_code,
                                            "",
                                            false,
                                            staged_err.empty() ? "Failed to inspect staged paths before git commit."
                                                               : staged_err);
    }
    if (staged_exit_code != 0) {
        const std::string failure = normalize_git_repo_error(staged_stderr);
        return make_git_commit_error_result("",
                                            trim_line_endings(staged_stderr),
                                            staged_exit_code,
                                            "",
                                            false,
                                            failure.empty() ? "Failed to inspect staged paths before git commit."
                                                            : failure);
    }
    for (const auto& staged_path : staged_paths) {
        if (!starts_with_path_component(staged_path, workspace_prefix)) {
            return make_git_commit_error_result(
                "", "", -1, "", false, "Refusing to commit staged path outside the workspace: " + staged_path);
        }
    }

    GitCommitObservedState before_state;
    std::string before_state_err;
    if (!capture_git_commit_observed_state(git_binary,
                                           workspace_abs,
                                           "Failed to resolve HEAD before git commit.",
                                           "Failed to inspect HEAD reflog before git commit.",
                                           &before_state,
                                           &before_state_err)) {
        return make_git_commit_error_result("", "", -1, "", false, before_state_err);
    }

    std::vector<std::string> args = {git_binary};
    if (!workspace_prefix.empty()) {
        args.push_back("-c");
        args.push_back("core.hooksPath=/dev/null");
    }
    args.insert(args.end(), {
        "commit",
        "-m", message
    });

    GitTextCommandResult command = run_git_text_command(git_binary, args, workspace_abs, output_limit);
    if (!command.launched) {
        if (command.err.empty()) {
            command.err = command.stderr_text;
        }
        return make_git_commit_error_result(
            "", command.stderr_text, command.exit_code, "", command.truncated,
            command.err.empty() ? "git commit failed." : command.err);
    }

    GitCommitObservedState after_state;
    std::string after_state_err;
    if (!capture_git_commit_observed_state(git_binary,
                                           workspace_abs,
                                           "Failed to resolve HEAD after git commit.",
                                           "Failed to inspect HEAD reflog after git commit.",
                                           &after_state,
                                           &after_state_err)) {
        return make_git_commit_error_result(
            command.stdout_text, command.stderr_text, -1, "", command.truncated, after_state_err);
    }

    std::string created_commit_sha;
    std::string created_commit_err;
    if (!resolve_created_commit_sha(git_binary,
                                    workspace_abs,
                                    before_state.head_sha,
                                    after_state.head_sha,
                                    before_state.reflog_shas,
                                    after_state.reflog_shas,
                                    &created_commit_sha,
                                    &created_commit_err)) {
        return make_git_commit_error_result(
            command.stdout_text,
            command.stderr_text,
            -1,
            "",
            command.truncated,
            created_commit_err.empty() ? "Failed to determine which commit git commit created." : created_commit_err);
    }

    const bool head_advanced = !created_commit_sha.empty();
    if (command.killed_early && command.truncated) {
        if (head_advanced) {
            return {
                {"ok", true},
                {"stdout", command.stdout_text},
                {"stderr", command.stderr_text},
                {"exit_code", 0},
                {"commit_sha", created_commit_sha},
                {"truncated", command.truncated},
                {"error", ""}
            };
        }
        return make_git_commit_error_result(command.stdout_text,
                                            command.stderr_text,
                                            -1,
                                            "",
                                            command.truncated,
                                            "git commit output exceeded the configured limit before the command completed.");
    }

    if (command.exit_code != 0) {
        const std::string failure = normalize_git_commit_error(command.stdout_text, command.stderr_text);
        return make_git_commit_error_result(command.stdout_text,
                                            command.stderr_text,
                                            command.exit_code,
                                            "",
                                            command.truncated,
                                            failure.empty() ? "git commit exited with code " + std::to_string(command.exit_code)
                                                            : failure);
    }

    if (!head_advanced) {
        return make_git_commit_error_result(
            command.stdout_text, command.stderr_text, -1, "", command.truncated, "git commit did not create a new HEAD commit.");
    }

    return {
        {"ok", true},
        {"stdout", command.stdout_text},
        {"stderr", command.stderr_text},
        {"exit_code", command.exit_code},
        {"commit_sha", created_commit_sha},
        {"truncated", command.truncated},
        {"error", ""}
    };
}
