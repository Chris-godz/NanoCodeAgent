#include "build_test_tools.hpp"
#include "process_env.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <regex>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct TailBuffer {
    size_t max_bytes = 0;
    size_t total_bytes = 0;
    std::string data;

    void append(const char* chunk, size_t size) {
        total_bytes += size;
        if (max_bytes == 0 || size == 0) {
            return;
        }

        if (size >= max_bytes) {
            data.assign(chunk + (size - max_bytes), max_bytes);
            return;
        }

        data.append(chunk, size);
        if (data.size() > max_bytes) {
            data.erase(0, data.size() - max_bytes);
        }
    }

    bool truncated() const {
        return total_bytes > data.size();
    }
};

struct ChildProcess {
    pid_t pid = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;
};

struct ChildExitState {
    bool exited = false;
    int wait_status = 0;
};

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

std::string_view trim_line_endings(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string build_subcommand_label(const std::vector<std::string>& subcommands) {
    std::string joined = "./build.sh";
    for (const auto& part : subcommands) {
        joined += " ";
        joined += part;
    }
    return joined;
}

std::string make_success_summary(const std::vector<std::string>& subcommands) {
    if (subcommands.empty()) {
        return "build.sh command sequence completed successfully.";
    }
    return build_subcommand_label(subcommands) + " completed successfully.";
}

std::string make_failure_summary(const std::vector<std::string>& subcommands, int exit_code) {
    return build_subcommand_label(subcommands) + " failed with exit code " + std::to_string(exit_code) + ".";
}

std::string make_timeout_summary(const std::vector<std::string>& subcommands, int timeout_ms) {
    return build_subcommand_label(subcommands) + " timed out after " + std::to_string(timeout_ms) + " ms.";
}

std::string make_start_failure_summary(const std::vector<std::string>& subcommands, const std::string& message) {
    return build_subcommand_label(subcommands) + " could not start: " + message;
}

BuildScriptResult make_start_failure(const std::vector<std::string>& subcommands, const std::string& message) {
    BuildScriptResult result;
    result.status = "failed";
    result.summary = make_start_failure_summary(subcommands, message);
    result.stderr_text = message;
    return result;
}

std::string join_outputs(const std::string& stdout_text, const std::string& stderr_text) {
    if (stdout_text.empty()) {
        return stderr_text;
    }
    if (stderr_text.empty()) {
        return stdout_text;
    }
    return stdout_text + "\n" + stderr_text;
}

bool collect_child_output(ChildProcess child,
                          std::chrono::steady_clock::time_point deadline,
                          TailBuffer* stdout_tail,
                          TailBuffer* stderr_tail,
                          ChildExitState* child_exit,
                          bool* timed_out,
                          std::string* error) {
    set_nonblocking(child.stdout_fd);
    set_nonblocking(child.stderr_fd);

    struct pollfd pfds[2];
    pfds[0].fd = child.stdout_fd;
    pfds[0].events = POLLIN;
    pfds[1].fd = child.stderr_fd;
    pfds[1].events = POLLIN;

    char buffer[4096];

    while (true) {
        if (child_exit && !child_exit->exited) {
            int status = 0;
            while (true) {
                const pid_t wait_result = waitpid(child.pid, &status, WNOHANG);
                if (wait_result == child.pid) {
                    child_exit->exited = true;
                    child_exit->wait_status = status;
                    break;
                }
                if (wait_result == 0) {
                    break;
                }
                if (wait_result < 0 && errno == EINTR) {
                    continue;
                }
                if (wait_result < 0) {
                    if (error) {
                        *error = std::string("waitpid failed: ") + strerror(errno);
                    }
                    kill_child_group(child.pid);
                    return false;
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            if (timed_out) {
                *timed_out = true;
            }
            if (error) {
                *error = "Execution globally timed out";
            }
            kill_child_group(child.pid);
            return false;
        }

        const bool child_alive = !(child_exit && child_exit->exited);
        if (!child_alive && pfds[0].fd < 0 && pfds[1].fd < 0) {
            return true;
        }

        if (pfds[0].fd < 0 && pfds[1].fd < 0) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            const auto sleep_for = std::min(remaining, std::chrono::milliseconds(10));
            if (sleep_for > std::chrono::milliseconds(0)) {
                std::this_thread::sleep_for(sleep_for);
            }
            continue;
        }

        const int wait_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        int ret = poll(pfds, 2, wait_ms);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (error) {
                *error = std::string("poll failed: ") + strerror(errno);
            }
            kill_child_group(child.pid);
            return false;
        }
        if (ret == 0) {
            continue;
        }

        bool saw_activity = false;
        for (int i = 0; i < 2; ++i) {
            if (pfds[i].fd < 0) {
                continue;
            }
            if ((pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }

            while (true) {
                const ssize_t count = read(pfds[i].fd, buffer, sizeof(buffer));
                if (count > 0) {
                    saw_activity = true;
                    if (i == 0) {
                        stdout_tail->append(buffer, static_cast<size_t>(count));
                    } else {
                        stderr_tail->append(buffer, static_cast<size_t>(count));
                    }
                    continue;
                }
                if (count == 0) {
                    pfds[i].fd = -1;
                    break;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (error) {
                    *error = std::string("read failed: ") + strerror(errno);
                }
                kill_child_group(child.pid);
                return false;
            }
        }
        if (!saw_activity && child_exit && child_exit->exited && pfds[0].fd < 0 && pfds[1].fd < 0) {
            return true;
        }
    }
}

bool reap_child_blocking(pid_t pid, int* wait_status, std::string* error) {
    if (!wait_status) {
        return false;
    }

    while (true) {
        const pid_t wait_result = waitpid(pid, wait_status, 0);
        if (wait_result == pid) {
            return true;
        }
        if (wait_result < 0 && errno == EINTR) {
            continue;
        }
        if (wait_result < 0 && errno == ECHILD) {
            return true;
        }
        if (wait_result < 0) {
            if (error) {
                *error = std::string("waitpid failed: ") + strerror(errno);
            }
            return false;
        }
    }
}

BuildScriptResult run_single_build_step(const std::string& workspace_abs,
                                        const std::string& subcommand,
                                        std::chrono::steady_clock::time_point deadline,
                                        int timeout_ms,
                                        size_t max_output_bytes) {
    const fs::path script_path = fs::path(workspace_abs) / "build.sh";
    if (!fs::exists(script_path)) {
        return make_start_failure({subcommand}, "./build.sh does not exist in the workspace.");
    }
    if (access(script_path.c_str(), X_OK) != 0) {
        return make_start_failure({subcommand}, std::string("./build.sh is not executable: ") + strerror(errno));
    }

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        if (stdout_pipe[0] >= 0) close_fd(stdout_pipe[0]);
        if (stdout_pipe[1] >= 0) close_fd(stdout_pipe[1]);
        if (stderr_pipe[0] >= 0) close_fd(stderr_pipe[0]);
        if (stderr_pipe[1] >= 0) close_fd(stderr_pipe[1]);
        return make_start_failure({subcommand}, "failed to create pipes.");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
        return make_start_failure({subcommand}, std::string("fork failed: ") + strerror(errno));
    }

    if (pid == 0) {
        setpgid(0, 0);
        if (chdir(workspace_abs.c_str()) != 0) {
            _exit(126);
        }

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);

        process_env::reset_child_environment();

        const std::string script = script_path.string();
        char* const argv[] = {
            const_cast<char*>(script.c_str()),
            const_cast<char*>(subcommand.c_str()),
            nullptr
        };
        execv(script.c_str(), argv);
        _exit(127);
    }

    close_fd(stdout_pipe[1]);
    close_fd(stderr_pipe[1]);

    TailBuffer stdout_tail{max_output_bytes, 0, ""};
    TailBuffer stderr_tail{max_output_bytes, 0, ""};
    ChildExitState child_exit;
    bool timed_out = false;
    std::string runtime_error;
    const ChildProcess child{pid, stdout_pipe[0], stderr_pipe[0]};
    const bool collected = collect_child_output(child, deadline, &stdout_tail,
                                                &stderr_tail, &child_exit, &timed_out, &runtime_error);

    close_fd(stdout_pipe[0]);
    close_fd(stderr_pipe[0]);

    int wstatus = child_exit.wait_status;
    if (!child_exit.exited) {
        const bool reaped = reap_child_blocking(pid, &wstatus, &runtime_error);
        child_exit.exited = reaped;
        if (!reaped) {
            BuildScriptResult result;
            result.stdout_text = stdout_tail.data;
            result.stderr_text = stderr_tail.data;
            result.truncated = stdout_tail.truncated() || stderr_tail.truncated();
            result.status = timed_out ? "timed_out" : "failed";
            result.timed_out = timed_out;
            result.summary = timed_out ? make_timeout_summary({subcommand}, timeout_ms)
                                       : make_start_failure_summary({subcommand}, runtime_error.empty() ? "failed while waiting for process exit." : runtime_error);
            if (result.stderr_text.empty() && !runtime_error.empty()) {
                result.stderr_text = runtime_error;
            }
            return result;
        }
    }

    BuildScriptResult result;
    result.stdout_text = stdout_tail.data;
    result.stderr_text = stderr_tail.data;
    result.truncated = stdout_tail.truncated() || stderr_tail.truncated();
    result.timed_out = timed_out;

    if (WIFEXITED(wstatus)) {
        result.exit_code = WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        result.exit_code = 128 + WTERMSIG(wstatus);
    }

    if (!collected && timed_out) {
        result.ok = false;
        result.timed_out = true;
        result.status = "timed_out";
        result.summary = make_timeout_summary({subcommand}, timeout_ms);
        if (result.stderr_text.empty() && !runtime_error.empty()) {
            result.stderr_text = runtime_error;
        }
        return result;
    }

    if (!collected && !timed_out) {
        result.ok = false;
        result.status = "failed";
        result.summary = make_start_failure_summary({subcommand}, runtime_error.empty() ? "failed while collecting process output." : runtime_error);
        if (result.stderr_text.empty() && !runtime_error.empty()) {
            result.stderr_text = runtime_error;
        }
        return result;
    }

    if (timed_out) {
        result.ok = false;
        result.status = "timed_out";
        result.summary = make_timeout_summary({subcommand}, timeout_ms);
        return result;
    }

    result.ok = (result.exit_code == 0);
    result.status = result.ok ? "ok" : "failed";
    result.summary = result.ok
        ? make_success_summary({subcommand})
        : make_failure_summary({subcommand}, result.exit_code);
    return result;
}

void append_with_separator(std::string* dst, const std::string& src) {
    if (!dst || src.empty()) {
        return;
    }
    if (!dst->empty() && dst->back() != '\n') {
        dst->push_back('\n');
    }
    dst->append(src);
}

void trim_result_to_limit(BuildScriptResult* result, size_t max_output_bytes) {
    if (!result || max_output_bytes == 0) {
        return;
    }

    auto trim_field = [&](std::string* field) {
        if (!field || field->size() <= max_output_bytes) {
            return;
        }
        field->erase(0, field->size() - max_output_bytes);
        result->truncated = true;
    };

    trim_field(&result->stdout_text);
    trim_field(&result->stderr_text);
}

} // namespace

BuildScriptResult run_build_script_sequence(const std::string& workspace_abs,
                                            const std::vector<std::string>& subcommands,
                                            int timeout_ms,
                                            size_t max_output_bytes) {
    BuildScriptResult result;
    if (subcommands.empty()) {
        result.status = "failed";
        result.summary = "No build.sh subcommands were provided.";
        result.stderr_text = "No build.sh subcommands were provided.";
        return result;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(timeout_ms);

    for (size_t i = 0; i < subcommands.size(); ++i) {
        BuildScriptResult step = run_single_build_step(workspace_abs, subcommands[i], deadline,
                                                       timeout_ms, max_output_bytes);
        append_with_separator(&result.stdout_text, step.stdout_text);
        append_with_separator(&result.stderr_text, step.stderr_text);
        result.truncated = result.truncated || step.truncated;
        result.exit_code = step.exit_code;

        if (!step.ok) {
            result.ok = false;
            result.status = step.status;
            result.timed_out = step.timed_out;
            if (step.timed_out) {
                result.summary = make_timeout_summary(subcommands, timeout_ms);
            } else {
                result.summary = step.summary.empty() ? make_failure_summary(subcommands, step.exit_code)
                                                      : step.summary;
            }
            trim_result_to_limit(&result, max_output_bytes);
            return result;
        }
    }

    result.ok = true;
    result.status = "ok";
    result.summary = make_success_summary(subcommands);
    trim_result_to_limit(&result, max_output_bytes);
    return result;
}

ParsedTestSummary parse_ctest_summary(const std::string& stdout_text,
                                      const std::string& stderr_text) {
    ParsedTestSummary summary;
    const std::string combined = join_outputs(stdout_text, stderr_text);
    static const std::regex counts_regex(R"((\d+)% tests passed,\s+(\d+) tests failed out of (\d+))");
    static const std::regex failed_regex(R"(^\s*\d+\s*-\s*(.*)\s+\([^)]+\)\s*$)");

    std::smatch match;
    if (std::regex_search(combined, match, counts_regex) && match.size() == 4) {
        const int failed = std::stoi(match[2].str());
        const int total = std::stoi(match[3].str());
        summary.failed_count = failed;
        summary.passed_count = total - failed;
    }

    const std::string marker = "The following tests FAILED:";
    const size_t marker_pos = combined.find(marker);
    if (marker_pos == std::string::npos) {
        return summary;
    }

    std::string_view tail(combined.c_str() + marker_pos + marker.size(), combined.size() - marker_pos - marker.size());
    size_t line_start = 0;
    while (line_start < tail.size()) {
        size_t line_end = tail.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = tail.size();
        }
        const std::string_view line = trim_line_endings(tail.substr(line_start, line_end - line_start));
        if (!line.empty()) {
            std::cmatch failed_match;
            if (std::regex_match(line.data(), line.data() + line.size(), failed_match, failed_regex) &&
                failed_match.size() == 2) {
                summary.failed_tests.push_back(failed_match[1].str());
            }
        }
        line_start = line_end + 1;
    }

    return summary;
}
