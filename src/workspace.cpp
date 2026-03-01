#include "workspace.hpp"
#include <filesystem>
#include <system_error>
#include <algorithm>

namespace fs = std::filesystem;

bool workspace_init(AgentConfig* cfg, std::string* err) {
    if (!cfg) {
        if (err) *err = "Null config pointer";
        return false;
    }

    try {
        fs::path ws_path = fs::path(cfg->workspace);
        if (!fs::exists(ws_path)) {
            std::error_code ec;
            fs::create_directories(ws_path, ec);
            if (ec) {
                if (err) *err = "Failed to create workspace directory: " + ec.message();
                return false;
            }
        }
        
        cfg->workspace_abs = fs::weakly_canonical(ws_path).string();
        return true;
    } catch (const fs::filesystem_error& e) {
        if (err) *err = e.what();
        return false;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

bool workspace_resolve(const AgentConfig& cfg, const std::string& rel, std::string* out_abs, std::string* err) {
    if (cfg.workspace_abs.empty()) {
        if (err) *err = "Workspace is not initialized";
        return false;
    }

    fs::path rel_path(rel);
    if (rel_path.is_absolute()) {
        if (err) *err = "Path is absolute, only relative paths are allowed";
        return false;
    }

    if (rel_path.empty()) {
        if (err) *err = "Path is empty";
        return false;
    }

    try {
        fs::path base_path(cfg.workspace_abs);
        fs::path combined_path = base_path / rel_path;
        
        fs::path normalized = combined_path.lexically_normal();
        
        // Use mismatch to see if base_path is a prefix of normalized
        auto it = std::mismatch(base_path.begin(), base_path.end(), normalized.begin());
        if (it.first != base_path.end()) {
            if (err) *err = "Path escapes the workspace boundary";
            return false;
        }

        if (out_abs) {
            *out_abs = normalized.string();
        }
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}
