#include "trace.hpp"

#include "logger.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;

namespace {

TraceWriteResult make_trace_write_success() {
    return {};
}

TraceWriteResult make_trace_write_failure(std::string error) {
    return {
        false,
        std::move(error),
    };
}

}  // namespace

TraceWriteResult NullTraceSink::write(const TraceEvent& event) {
    (void)event;
    return make_trace_write_success();
}

void FanoutTraceSink::add_sink(TraceSink* sink) {
    if (sink != nullptr) {
        sinks_.push_back(sink);
    }
}

TraceWriteResult FanoutTraceSink::write(const TraceEvent& event) {
    for (TraceSink* sink : sinks_) {
        const TraceWriteResult result = sink->write(event);
        if (!result.ok) {
            return result;
        }
    }
    return make_trace_write_success();
}

SessionTraceSink::SessionTraceSink(SessionState* session_state) : session_state_(session_state) {}

TraceWriteResult SessionTraceSink::write(const TraceEvent& event) {
    if (session_state_ == nullptr) {
        return make_trace_write_success();
    }
    session_state_->trace.push_back(event);
    touch_session(*session_state_);
    return make_trace_write_success();
}

JsonlTraceSink::JsonlTraceSink(std::string path) : path_(std::move(path)) {}

bool JsonlTraceSink::prepare(std::string* err) {
    const fs::path trace_path(path_);
    const fs::path parent = trace_path.parent_path();
    if (!parent.empty()) {
        std::error_code mkdir_ec;
        fs::create_directories(parent, mkdir_ec);
        if (mkdir_ec) {
            if (err) {
                *err = "Could not create trace directory '" + parent.string() + "': " + mkdir_ec.message();
            }
            return false;
        }
    }

    std::ofstream output(trace_path, std::ios::app | std::ios::binary);
    if (!output.is_open()) {
        if (err) {
            *err = "Could not open trace file '" + path_ + "'.";
        }
        return false;
    }
    return true;
}

TraceWriteResult JsonlTraceSink::write(const TraceEvent& event) {
    std::ofstream output(path_, std::ios::app | std::ios::binary);
    if (!output.is_open()) {
        return make_trace_write_failure("Could not append trace event to '" + path_ + "'.");
    }
    output << nlohmann::json(event).dump() << "\n";
    if (!output) {
        return make_trace_write_failure("Failed while writing trace event to '" + path_ + "'.");
    }
    return make_trace_write_success();
}

TraceEvent make_trace_event(std::string kind,
                            std::string message,
                            const nlohmann::json& payload) {
    TraceEvent event;
    event.kind = std::move(kind);
    event.message = std::move(message);
    event.created_at = state_now_timestamp();
    event.payload = payload.is_object() ? payload : nlohmann::json::object();
    return event;
}
