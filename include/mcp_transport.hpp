#pragma once

#include <sys/types.h>

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class McpTransportStdio {
public:
    McpTransportStdio(std::string server_name,
                     std::vector<std::string> command_args,
                     std::string working_directory,
                     int write_timeout_ms);
    ~McpTransportStdio();

    McpTransportStdio(const McpTransportStdio&) = delete;
    McpTransportStdio& operator=(const McpTransportStdio&) = delete;

    McpTransportStdio(McpTransportStdio&& other) noexcept;
    McpTransportStdio& operator=(McpTransportStdio&& other) noexcept;

    bool start(std::string* err);
    bool send_message(const nlohmann::json& message, std::string* err);
    bool read_message(int timeout_ms, size_t max_message_bytes, nlohmann::json* out, std::string* err);
    void close();

    bool running() const { return running_; }
    const std::string& server_name() const { return server_name_; }

private:
    bool try_parse_message(size_t max_message_bytes, nlohmann::json* out, std::string* err);
    void absorb_stderr(const char* data, size_t size);
    void drain_available_stderr();
    std::string format_error_with_stderr(const std::string& base_message);
    bool fail_with_error(const std::string& base_message, std::string* err);

    std::string server_name_;
    std::vector<std::string> command_args_;
    std::string working_directory_;
    int write_timeout_ms_ = 3000;
    pid_t pid_ = -1;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
    int stderr_fd_ = -1;
    bool running_ = false;
    std::string stdout_buffer_;
    std::string stderr_buffer_;
    size_t stderr_bytes_seen_ = 0;
};
