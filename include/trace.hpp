#pragma once

#include "state.hpp"

#include <memory>
#include <string>
#include <vector>

struct TraceWriteResult {
    bool ok = true;
    std::string error;
};

class TraceSink {
public:
    virtual ~TraceSink() = default;
    virtual TraceWriteResult write(const TraceEvent& event) = 0;
};

class NullTraceSink : public TraceSink {
public:
    TraceWriteResult write(const TraceEvent& event) override;
};

class FanoutTraceSink : public TraceSink {
public:
    void add_sink(TraceSink* sink);
    TraceWriteResult write(const TraceEvent& event) override;

private:
    std::vector<TraceSink*> sinks_;
};

class SessionTraceSink : public TraceSink {
public:
    explicit SessionTraceSink(SessionState* session_state);
    TraceWriteResult write(const TraceEvent& event) override;

private:
    SessionState* session_state_ = nullptr;
};

class JsonlTraceSink : public TraceSink {
public:
    explicit JsonlTraceSink(std::string path);

    bool prepare(std::string* err);
    TraceWriteResult write(const TraceEvent& event) override;

private:
    std::string path_;
};

TraceEvent make_trace_event(std::string kind,
                            std::string message,
                            const nlohmann::json& payload = nlohmann::json::object());
