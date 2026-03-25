#include "state_store.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

StateStoreLoadResult InMemoryStateStore::load() const {
    if (!has_session_) {
        return {};
    }
    return {
        StateStoreLoadStatus::Loaded,
        session_,
        ""
    };
}

bool InMemoryStateStore::save(const SessionState& session, std::string* err) {
    (void)err;
    session_ = session;
    has_session_ = true;
    return true;
}

JsonFileStateStore::JsonFileStateStore(std::string path) : path_(std::move(path)) {}

StateStoreLoadResult JsonFileStateStore::load() const {
    const fs::path session_path(path_);
    std::error_code exists_ec;
    const bool exists = fs::exists(session_path, exists_ec);
    if (exists_ec) {
        return {
            StateStoreLoadStatus::Error,
            SessionState{},
            "Failed to inspect session file '" + path_ + "': " + exists_ec.message()
        };
    }
    if (!exists) {
        return {};
    }

    std::ifstream input(session_path, std::ios::binary);
    if (!input.is_open()) {
        return {
            StateStoreLoadStatus::Error,
            SessionState{},
            "Could not open session file '" + path_ + "'."
        };
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return {
            StateStoreLoadStatus::Error,
            SessionState{},
            "Failed while reading session file '" + path_ + "'."
        };
    }

    try {
        const nlohmann::json parsed = nlohmann::json::parse(buffer.str());
        SessionState session;
        std::string err;
        if (!session_state_from_json(parsed, &session, &err)) {
            return {
                StateStoreLoadStatus::Error,
                SessionState{},
                "Invalid session file '" + path_ + "': " + err
            };
        }
        return {
            StateStoreLoadStatus::Loaded,
            std::move(session),
            ""
        };
    } catch (const nlohmann::json::exception& e) {
        return {
            StateStoreLoadStatus::Error,
            SessionState{},
            "Invalid JSON in session file '" + path_ + "': " + e.what()
        };
    }
}

bool JsonFileStateStore::save(const SessionState& session, std::string* err) {
    const fs::path session_path(path_);
    const fs::path parent_path = session_path.parent_path();
    std::string serialized_session;

    try {
        serialized_session = session_state_to_json(session).dump(2) + "\n";
    } catch (const std::exception& e) {
        if (err) {
            *err = std::string("Invalid session state for save: ") + e.what();
        }
        return false;
    }

    if (!parent_path.empty()) {
        std::error_code mkdir_ec;
        fs::create_directories(parent_path, mkdir_ec);
        if (mkdir_ec) {
            if (err) {
                *err = "Could not create session directory '" + parent_path.string() + "': " + mkdir_ec.message();
            }
            return false;
        }
    }

    const fs::path temp_path = session_path.string() + ".tmp";
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (err) {
            *err = "Could not open temp session file '" + temp_path.string() + "' for writing.";
        }
        return false;
    }

    output << serialized_session;
    output.flush();
    if (!output) {
        output.close();
        std::error_code cleanup_ec;
        fs::remove(temp_path, cleanup_ec);
        if (err) {
            *err = "Failed while writing session file '" + temp_path.string() + "'.";
        }
        return false;
    }
    output.close();

    std::error_code rename_ec;
    fs::rename(temp_path, session_path, rename_ec);
    if (rename_ec) {
        std::error_code cleanup_ec;
        fs::remove(temp_path, cleanup_ec);
        if (err) {
            *err = "Could not move temp session file into place for '" + path_ + "': " + rename_ec.message();
        }
        return false;
    }

    return true;
}
