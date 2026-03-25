#include "mcp_transport.hpp"

#include "logger.hpp"
#include "process_env.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaxLoggedStderrBytes = 16 * 1024;

void close_fd(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
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

std::string trim_copy(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

class ScopedSigpipeIgnore {
public:
    ScopedSigpipeIgnore() {
        struct sigaction ignore_action {};
        ignore_action.sa_handler = SIG_IGN;
        sigemptyset(&ignore_action.sa_mask);
        sigaction(SIGPIPE, &ignore_action, &previous_action_);
        active_ = true;
    }

    ~ScopedSigpipeIgnore() {
        if (active_) {
            sigaction(SIGPIPE, &previous_action_, nullptr);
        }
    }

private:
    struct sigaction previous_action_ {};
    bool active_ = false;
};

}  // namespace

McpTransportStdio::McpTransportStdio(std::string server_name,
                                     std::vector<std::string> command_args,
                                     std::string working_directory,
                                     int write_timeout_ms)
    : server_name_(std::move(server_name)),
      command_args_(std::move(command_args)),
      working_directory_(std::move(working_directory)),
      write_timeout_ms_(write_timeout_ms > 0 ? write_timeout_ms : 1) {}

McpTransportStdio::~McpTransportStdio() {
    close();
}

McpTransportStdio::McpTransportStdio(McpTransportStdio&& other) noexcept
    : server_name_(std::move(other.server_name_)),
      command_args_(std::move(other.command_args_)),
      working_directory_(std::move(other.working_directory_)),
      write_timeout_ms_(other.write_timeout_ms_),
      pid_(other.pid_),
      stdin_fd_(other.stdin_fd_),
      stdout_fd_(other.stdout_fd_),
      stderr_fd_(other.stderr_fd_),
      running_(other.running_),
      stdout_buffer_(std::move(other.stdout_buffer_)),
      stderr_buffer_(std::move(other.stderr_buffer_)),
      stderr_bytes_seen_(other.stderr_bytes_seen_) {
    other.pid_ = -1;
    other.stdin_fd_ = -1;
    other.stdout_fd_ = -1;
    other.stderr_fd_ = -1;
    other.running_ = false;
    other.stderr_bytes_seen_ = 0;
}

McpTransportStdio& McpTransportStdio::operator=(McpTransportStdio&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    close();
    server_name_ = std::move(other.server_name_);
    command_args_ = std::move(other.command_args_);
    working_directory_ = std::move(other.working_directory_);
    write_timeout_ms_ = other.write_timeout_ms_;
    pid_ = other.pid_;
    stdin_fd_ = other.stdin_fd_;
    stdout_fd_ = other.stdout_fd_;
    stderr_fd_ = other.stderr_fd_;
    running_ = other.running_;
    stdout_buffer_ = std::move(other.stdout_buffer_);
    stderr_buffer_ = std::move(other.stderr_buffer_);
    stderr_bytes_seen_ = other.stderr_bytes_seen_;

    other.pid_ = -1;
    other.stdin_fd_ = -1;
    other.stdout_fd_ = -1;
    other.stderr_fd_ = -1;
    other.running_ = false;
    other.stderr_bytes_seen_ = 0;
    return *this;
}

bool McpTransportStdio::start(std::string* err) {
    if (running_) {
        return true;
    }
    if (command_args_.empty() || command_args_.front().empty()) {
        if (err) {
            *err = "MCP server command must not be empty.";
        }
        return false;
    }

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        close_fd(stdin_pipe[0]);
        close_fd(stdin_pipe[1]);
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
        if (err) {
            *err = std::string("Failed to create MCP stdio pipes: ") + strerror(errno);
        }
        return false;
    }

    pid_t child_pid = fork();
    if (child_pid < 0) {
        close_fd(stdin_pipe[0]);
        close_fd(stdin_pipe[1]);
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
        if (err) {
            *err = std::string("Failed to fork MCP server process: ") + strerror(errno);
        }
        return false;
    }

    if (child_pid == 0) {
        setpgid(0, 0);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);

        if (chdir(working_directory_.c_str()) != 0) {
            dprintf(STDERR_FILENO,
                    "failed to chdir to '%s': %s\n",
                    working_directory_.c_str(),
                    strerror(errno));
            _exit(126);
        }

        process_env::reset_child_environment();

        std::vector<char*> argv;
        argv.reserve(command_args_.size() + 1);
        for (std::string& arg : command_args_) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        dprintf(STDERR_FILENO,
                "failed to exec MCP server '%s': %s\n",
                command_args_.front().c_str(),
                strerror(errno));
        _exit(127);
    }

    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    set_nonblocking(stdin_pipe[1]);
    set_nonblocking(stdout_pipe[0]);
    set_nonblocking(stderr_pipe[0]);

    pid_ = child_pid;
    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];
    stderr_fd_ = stderr_pipe[0];
    running_ = true;
    stdout_buffer_.clear();
    stderr_buffer_.clear();
    stderr_bytes_seen_ = 0;
    return true;
}

bool McpTransportStdio::send_message(const nlohmann::json& message, std::string* err) {
    if (!running_ || stdin_fd_ < 0) {
        if (err) {
            *err = "MCP transport is not running.";
        }
        return false;
    }

    const std::string body = message.dump();
    const std::string frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;

    ScopedSigpipeIgnore sigpipe_guard;
    size_t written_total = 0;
    const auto start_time = std::chrono::steady_clock::now();
    while (written_total < frame.size()) {
        const ssize_t written = write(stdin_fd_, frame.data() + written_total, frame.size() - written_total);
        if (written > 0) {
            written_total += static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && errno == EPIPE) {
            return fail_with_error("Broken pipe while writing to MCP server '" + server_name_ + "'.", err);
        }
        if ((written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) || written == 0) {
            const auto now = std::chrono::steady_clock::now();
            const int elapsed_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count());
            const int time_left = write_timeout_ms_ - elapsed_ms;
            if (time_left <= 0) {
                return fail_with_error("Timed out writing to MCP server '" + server_name_ + "'.", err);
            }

            struct pollfd pfd {
                stdin_fd_,
                POLLOUT | POLLHUP | POLLERR,
                0
            };
            const int poll_result = poll(&pfd, 1, time_left);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return fail_with_error(std::string("poll failed while writing MCP server '") + server_name_ + "': " +
                                           strerror(errno),
                                       err);
            }
            if (poll_result == 0) {
                return fail_with_error("Timed out writing to MCP server '" + server_name_ + "'.", err);
            }
            if ((pfd.revents & (POLLHUP | POLLERR)) != 0 && (pfd.revents & POLLOUT) == 0) {
                return fail_with_error("Broken pipe while writing to MCP server '" + server_name_ + "'.", err);
            }
            continue;
        }
        return fail_with_error(std::string("Failed writing to MCP server '") + server_name_ + "': " + strerror(errno),
                               err);
    }

    return true;
}

bool McpTransportStdio::try_parse_message(size_t max_message_bytes,
                                          nlohmann::json* out,
                                          std::string* err) {
    const std::size_t header_end = stdout_buffer_.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;
    }

    const std::string headers = stdout_buffer_.substr(0, header_end);
    std::size_t content_length = 0;
    bool saw_content_length = false;

    std::size_t cursor = 0;
    while (cursor <= headers.size()) {
        const std::size_t line_end = headers.find("\r\n", cursor);
        const std::string line = headers.substr(cursor,
                                                line_end == std::string::npos ? std::string::npos
                                                                              : line_end - cursor);
        if (!line.empty()) {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                if (err) {
                    *err = "Malformed MCP header line from server '" + server_name_ + "'.";
                }
                return true;
            }

            const std::string key = to_lower_copy(trim_copy(line.substr(0, colon)));
            const std::string value = trim_copy(line.substr(colon + 1));
            if (key == "content-length") {
                try {
                    content_length = static_cast<std::size_t>(std::stoull(value));
                    saw_content_length = true;
                } catch (const std::exception&) {
                    if (err) {
                        *err = "Invalid Content-Length from MCP server '" + server_name_ + "'.";
                    }
                    return true;
                }
            }
        }

        if (line_end == std::string::npos) {
            break;
        }
        cursor = line_end + 2;
    }

    if (!saw_content_length) {
        if (err) {
            *err = "Missing Content-Length header from MCP server '" + server_name_ + "'.";
        }
        return true;
    }
    if (content_length > max_message_bytes) {
        if (err) {
            *err = "MCP message from server '" + server_name_ + "' exceeded the configured message cap.";
        }
        return true;
    }

    const std::size_t frame_size = header_end + 4 + content_length;
    if (stdout_buffer_.size() < frame_size) {
        return false;
    }

    const std::string body = stdout_buffer_.substr(header_end + 4, content_length);
    stdout_buffer_.erase(0, frame_size);

    try {
        *out = nlohmann::json::parse(body);
        return true;
    } catch (const nlohmann::json::exception& e) {
        if (err) {
            *err = "Malformed JSON from MCP server '" + server_name_ + "': " + std::string(e.what());
        }
        return true;
    }
}

void McpTransportStdio::absorb_stderr(const char* data, size_t size) {
    if (size == 0) {
        return;
    }

    stderr_bytes_seen_ += size;
    if (stderr_buffer_.size() < kMaxLoggedStderrBytes) {
        const std::size_t keep = std::min(size, kMaxLoggedStderrBytes - stderr_buffer_.size());
        stderr_buffer_.append(data, keep);
    }

    LOG_DEBUG("MCP stderr [{}]: {}", server_name_, std::string(data, size));
}

void McpTransportStdio::drain_available_stderr() {
    if (stderr_fd_ < 0) {
        return;
    }

    char buffer[4096];
    while (true) {
        const ssize_t n = read(stderr_fd_, buffer, sizeof(buffer));
        if (n > 0) {
            absorb_stderr(buffer, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            close_fd(stderr_fd_);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        close_fd(stderr_fd_);
        return;
    }
}

std::string McpTransportStdio::format_error_with_stderr(const std::string& base_message) {
    std::string formatted = base_message;
    std::string stderr_summary = trim_copy(stderr_buffer_);
    if (stderr_summary.empty()) {
        return formatted;
    }

    if (stderr_bytes_seen_ > stderr_buffer_.size()) {
        stderr_summary += " (stderr truncated)";
    }
    formatted += " stderr: " + stderr_summary;
    return formatted;
}

bool McpTransportStdio::fail_with_error(const std::string& base_message, std::string* err) {
    drain_available_stderr();
    if (err) {
        *err = format_error_with_stderr(base_message);
    }
    close();
    return false;
}

bool McpTransportStdio::read_message(int timeout_ms,
                                     size_t max_message_bytes,
                                     nlohmann::json* out,
                                     std::string* err) {
    if (!running_) {
        if (err) {
            *err = "MCP transport is not running.";
        }
        return false;
    }
    if (!out) {
        if (err) {
            *err = "MCP read_message requires a non-null output JSON pointer.";
        }
        return false;
    }

    std::string parse_err;
    if (try_parse_message(max_message_bytes, out, &parse_err)) {
        if (!parse_err.empty()) {
            return fail_with_error(parse_err, err);
        }
        return true;
    }

    const auto start_time = std::chrono::steady_clock::now();
    char buffer[4096];

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const int elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count());
        const int time_left = timeout_ms - elapsed_ms;
        if (time_left <= 0) {
            return fail_with_error("Timed out waiting for MCP server '" + server_name_ + "'.", err);
        }

        struct pollfd pfds[2];
        nfds_t count = 0;
        if (stdout_fd_ >= 0) {
            pfds[count++] = {stdout_fd_, POLLIN | POLLHUP | POLLERR, 0};
        }
        if (stderr_fd_ >= 0) {
            pfds[count++] = {stderr_fd_, POLLIN | POLLHUP | POLLERR, 0};
        }

        if (count == 0) {
            return fail_with_error("MCP server '" + server_name_ + "' closed its pipes unexpectedly.", err);
        }

        const int poll_result = poll(pfds, count, time_left);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail_with_error(std::string("poll failed while reading MCP server '") + server_name_ + "': " +
                                       strerror(errno),
                                   err);
        }
        if (poll_result == 0) {
            continue;
        }

        nfds_t index = 0;
        if (stdout_fd_ >= 0) {
            if (pfds[index].revents & (POLLIN | POLLHUP | POLLERR)) {
                while (true) {
                    const ssize_t n = read(stdout_fd_, buffer, sizeof(buffer));
                    if (n > 0) {
                        stdout_buffer_.append(buffer, static_cast<size_t>(n));
                        parse_err.clear();
                        if (try_parse_message(max_message_bytes, out, &parse_err)) {
                            if (!parse_err.empty()) {
                                return fail_with_error(parse_err, err);
                            }
                            return true;
                        }
                        continue;
                    }
                    if (n == 0) {
                        close_fd(stdout_fd_);
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    close_fd(stdout_fd_);
                    return fail_with_error(std::string("Failed reading MCP stdout from '") + server_name_ + "': " +
                                               strerror(errno),
                                           err);
                }
            }
            ++index;
        }

        if (stderr_fd_ >= 0) {
            if (pfds[index].revents & (POLLIN | POLLHUP | POLLERR)) {
                while (true) {
                    const ssize_t n = read(stderr_fd_, buffer, sizeof(buffer));
                    if (n > 0) {
                        absorb_stderr(buffer, static_cast<size_t>(n));
                        continue;
                    }
                    if (n == 0) {
                        close_fd(stderr_fd_);
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    close_fd(stderr_fd_);
                    break;
                }
            }
        }

        if (stdout_fd_ < 0 && stdout_buffer_.empty()) {
            return fail_with_error("Broken pipe while reading MCP response from server '" + server_name_ + "'.", err);
        }
    }
}

void McpTransportStdio::close() {
    close_fd(stdin_fd_);
    close_fd(stdout_fd_);
    close_fd(stderr_fd_);

    if (pid_ > 0) {
        kill_child_group(pid_);
        int wait_status = 0;
        waitpid(pid_, &wait_status, 0);
    }

    pid_ = -1;
    running_ = false;
}
