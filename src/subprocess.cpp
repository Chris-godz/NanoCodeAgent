#include "subprocess.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

}  // namespace

SubprocessResult run_subprocess_capture(const std::string& working_dir,
                                        const std::string& command,
                                        int timeout_ms,
                                        std::size_t max_stdout_bytes,
                                        std::size_t max_stderr_bytes) {
    SubprocessResult result;

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.error = "Failed to create subprocess pipes.";
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        result.error = "Failed to fork subprocess.";
        return result;
    }

    if (pid == 0) {
        setpgid(0, 0);
        if (chdir(working_dir.c_str()) != 0) {
            _exit(126);
        }

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        const char* args[] = {"bash", "-lc", command.c_str(), nullptr};
        execvp(args[0], const_cast<char* const*>(args));
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    set_nonblocking(stdout_pipe[0]);
    set_nonblocking(stderr_pipe[0]);

    pollfd poll_fds[2];
    poll_fds[0].fd = stdout_pipe[0];
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = stderr_pipe[0];
    poll_fds[1].events = POLLIN;

    char buffer[4096];
    bool killed = false;
    const auto start = std::chrono::steady_clock::now();

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        const int remaining_ms =
            timeout_ms <= 0 ? -1 : std::max(0, timeout_ms - static_cast<int>(elapsed_ms));
        if (timeout_ms > 0 && remaining_ms == 0) {
            if (killpg(pid, SIGKILL) != 0) {
                kill(pid, SIGKILL);
            }
            result.timed_out = true;
            result.error = "Subprocess timed out.";
            killed = true;
            break;
        }

        const int poll_result = poll(poll_fds, 2, remaining_ms);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            result.error = "Polling subprocess pipes failed.";
            break;
        }

        if (poll_result == 0) {
            continue;
        }

        if (poll_fds[0].fd >= 0 && (poll_fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            const ssize_t bytes_read = read(poll_fds[0].fd, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                result.stdout_bytes += static_cast<std::size_t>(bytes_read);
                result.stdout_text.append(buffer, static_cast<std::size_t>(bytes_read));
                if (result.stdout_bytes > max_stdout_bytes) {
                    if (killpg(pid, SIGKILL) != 0) {
                        kill(pid, SIGKILL);
                    }
                    result.truncated = true;
                    result.error = "Subprocess stdout exceeded configured limit.";
                    killed = true;
                    break;
                }
            } else if (bytes_read == 0 ||
                       (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                close(poll_fds[0].fd);
                poll_fds[0].fd = -1;
            }
        }

        if (poll_fds[1].fd >= 0 && (poll_fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            const ssize_t bytes_read = read(poll_fds[1].fd, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                result.stderr_bytes += static_cast<std::size_t>(bytes_read);
                result.stderr_text.append(buffer, static_cast<std::size_t>(bytes_read));
                if (result.stderr_bytes > max_stderr_bytes) {
                    if (killpg(pid, SIGKILL) != 0) {
                        kill(pid, SIGKILL);
                    }
                    result.truncated = true;
                    result.error = "Subprocess stderr exceeded configured limit.";
                    killed = true;
                    break;
                }
            } else if (bytes_read == 0 ||
                       (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                close(poll_fds[1].fd);
                poll_fds[1].fd = -1;
            }
        }

        if (poll_fds[0].fd == -1 && poll_fds[1].fd == -1) {
            break;
        }
    }

    if (poll_fds[0].fd >= 0) {
        close(poll_fds[0].fd);
    }
    if (poll_fds[1].fd >= 0) {
        close(poll_fds[1].fd);
    }

    int wait_status = 0;
    waitpid(pid, &wait_status, 0);
    if (WIFEXITED(wait_status)) {
        result.exit_code = WEXITSTATUS(wait_status);
        result.ok = !killed && result.exit_code == 0;
    } else if (WIFSIGNALED(wait_status)) {
        result.exit_code = 128 + WTERMSIG(wait_status);
        result.ok = false;
    }

    return result;
}
