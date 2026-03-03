#include "bash_tool.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <poll.h>
#include <cstring>
#include <vector>
#include <chrono>

// Expose standard underlying clearenv mechanism in unistd
extern char** environ;

static bool contains_dangerous_patterns(const std::string& cmd) {
    const std::vector<std::string> patterns = {
        "rm -rf /",
        ":(){ :|:& };:", // Fork bomb
        "mkfs",
        "dd if=/dev/zero"
    };

    for (const auto& pattern : patterns) {
        if (cmd.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void reset_child_environment() {
    // Attempt standard libc clearenv() equivalent clearing POSIX variable injections
#if defined(__linux__) || defined(__GLIBC__)
    clearenv();
#else
    static char* empty[] = {nullptr};
    environ = empty;
#endif

    // Reseeding sandbox-safe minimum viable runtime states
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("HOME", "/tmp", 1);
}

BashResult bash_execute_safe(const std::string& workspace_abs,
                             const std::string& command,
                             int timeout_ms,
                             size_t max_out,
                             size_t max_err) {
    BashResult result{false, -1, "", "", false, false, 0, 0, ""};

    if (contains_dangerous_patterns(command)) {
        result.err = "Command rejected: Contains obvious dangerous pattern mapping violation";
        return result;
    }

    int pipe_out[2], pipe_err[2];
    if (pipe(pipe_out) == -1 || pipe(pipe_err) == -1) {
        result.err = "Failed to create dual-pipeline architecture";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.err = "Fork failed: Unable to spawn child process constraint runner";
        return result;
    }

    if (pid == 0) {
        // --- Child Process Zone ---
        
        // 1) Escaping default terminal signals tying to independent child group bounds 
        setpgid(0, 0);

        // 2) Establish CWD Workspace lock 
        if (chdir(workspace_abs.c_str()) != 0) {
            _exit(126); // Lock failure, immediate abort without stdout trace 
        }

        // 3) Rebind STDOUT/STDERR strictly avoiding crossing host streams
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_err[1], STDERR_FILENO);
        
        // Unbinding leftover FDs for absolute cleanliness
        close(pipe_out[0]); close(pipe_out[1]);
        close(pipe_err[0]); close(pipe_err[1]);

        // 4) Cleanse & Overwrite Environment bypassing parent leakage traps
        reset_child_environment();

        // 5) Trigger standard Shell to parse instructions comprehensively maintaining formatting
        const char* args[] = {"sh", "-lc", command.c_str(), nullptr};
        execvp(args[0], const_cast<char* const*>(args));

        _exit(127); // Standard sh spawn fail fallback
    }

    // --- Parent Process Zone ---
    
    // Close exclusively child-held write tips
    close(pipe_out[1]);
    close(pipe_err[1]);

    // Construct parent poll reader mechanisms configuring Anti-deadlocks
    set_nonblocking(pipe_out[0]);
    set_nonblocking(pipe_err[0]);

    struct pollfd pfds[2];
    pfds[0].fd = pipe_out[0]; pfds[0].events = POLLIN;
    pfds[1].fd = pipe_err[0]; pfds[1].events = POLLIN;

    auto start_time = std::chrono::steady_clock::now();
    
    char buffer[4096];
    bool killed = false;

    while (!killed) {
        // Calculate remaining dynamic timeout budget
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        int time_left = timeout_ms - static_cast<int>(elapsed_ms);

        if (time_left <= 0) {
            if (killpg(pid, SIGKILL) != 0) kill(pid, SIGKILL);
            result.timed_out = true;
            result.err = "Execution globally timed out";
            killed = true;
            break;
        }

        // Wait with poll without totally hanging
        int ret = poll(pfds, 2, time_left);
        if (ret == -1) {
            if (errno == EINTR) continue;
            break; 
        }
        if (ret == 0) { // Poll strictly exhausted configured timing limit constraint
            continue; 
        }

        bool active_fds = false;

        // Extracting stdout securely checking buffer threshold traps
        if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = read(pipe_out[0], buffer, sizeof(buffer));
            if (n > 0) {
                result.out_bytes += n;
                result.out_tail.append(buffer, n);
                active_fds = true;
                
                if (result.out_bytes > max_out) {
                    if (killpg(pid, SIGKILL) != 0) kill(pid, SIGKILL);
                    result.truncated = true;
                    result.err = "Stdout bandwidth size ceiling limit triggered process termination";
                    killed = true;
                    break;
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                pfds[0].fd = -1; // End standard track
            } else {
                active_fds = true;
            }
        }

        // Extracting stderr applying same buffer boundaries
        if (!killed && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = read(pipe_err[0], buffer, sizeof(buffer));
            if (n > 0) {
                result.err_bytes += n;
                result.err_tail.append(buffer, n);
                active_fds = true;
                
                if (result.err_bytes > max_err) {
                    if (killpg(pid, SIGKILL) != 0) kill(pid, SIGKILL);
                    result.truncated = true;
                    result.err = "Stderr bandwidth cap triggered termination limit";
                    killed = true;
                    break;
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                pfds[1].fd = -1; 
            } else {
                active_fds = true;
            }
        }

        // If both tracking pathways collapsed or closed gracefully, drop tracking cycle loop
        if (pfds[0].fd == -1 && pfds[1].fd == -1) {
            break;
        }
    }

    // Always clear residual bindings unconditionally
    if (pipe_out[0] >= 0) close(pipe_out[0]);
    if (pipe_err[0] >= 0) close(pipe_err[0]);

    // Harvest execution remnants bypassing zombie process leakage scenarios 
    int wstatus;
    waitpid(pid, &wstatus, 0);

    if (WIFEXITED(wstatus)) {
        result.ok = (WEXITSTATUS(wstatus) == 0 && !killed);
        result.exit_code = WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        result.ok = false;
        result.exit_code = 128 + WTERMSIG(wstatus);
    }

    return result;
}
