#pragma once

#include "state.hpp"

#include <string>

enum class StateStoreLoadStatus {
    Loaded,
    Missing,
    Error,
};

struct StateStoreLoadResult {
    StateStoreLoadStatus status = StateStoreLoadStatus::Missing;
    SessionState session;
    std::string error;
};

class StateStore {
public:
    virtual ~StateStore() = default;

    virtual StateStoreLoadResult load() const = 0;
    virtual bool save(const SessionState& session, std::string* err) = 0;
};

class InMemoryStateStore final : public StateStore {
public:
    StateStoreLoadResult load() const override;
    bool save(const SessionState& session, std::string* err) override;

private:
    bool has_session_ = false;
    SessionState session_;
};

class JsonFileStateStore final : public StateStore {
public:
    explicit JsonFileStateStore(std::string path);

    StateStoreLoadResult load() const override;
    bool save(const SessionState& session, std::string* err) override;

    const std::string& path() const { return path_; }

private:
    std::string path_;
};
