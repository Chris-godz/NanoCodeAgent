#include "net_smoke_artifacts.hpp"

#include <chrono>
#include <cctype>
#include <cstdlib>

namespace fs = std::filesystem;

namespace {

constexpr const char* kKeepArtifactsEnv = "NCA_NET_SMOKE_KEEP_ARTIFACTS";

bool copy_required_artifact(const fs::path& from, const fs::path& to, std::string* err) {
    std::error_code ec;
    const bool exists = fs::exists(from, ec);
    if (ec) {
        if (err) {
            *err = "Could not inspect required artifact '" + from.string() + "': " + ec.message();
        }
        return false;
    }
    if (!exists) {
        if (err) {
            *err = "Required artifact missing: " + from.string();
        }
        return false;
    }
    const bool is_regular = fs::is_regular_file(from, ec);
    if (ec) {
        if (err) {
            *err = "Could not inspect required artifact '" + from.string() + "': " + ec.message();
        }
        return false;
    }
    if (!is_regular) {
        if (err) {
            *err = "Required artifact is not a file: " + from.string();
        }
        return false;
    }

    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (err) {
            *err = "Could not copy artifact '" + from.string() + "' to '" + to.string() + "': " + ec.message();
        }
        return false;
    }
    return true;
}

bool copy_net_smoke_artifacts(const fs::path& session_path,
                              const fs::path& trace_path,
                              const std::string& test_name,
                              std::string* err,
                              fs::path* artifact_dir) {
    const fs::path artifact_root = net_smoke_artifact_root_from_current_executable(err);
    if (artifact_root.empty()) {
        return false;
    }

    std::error_code ec;
    fs::create_directories(artifact_root, ec);
    if (ec) {
        if (err) {
            *err = "Could not create artifact root '" + artifact_root.string() + "': " + ec.message();
        }
        return false;
    }

    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    const fs::path target_dir =
        artifact_root / (sanitize_net_smoke_artifact_component(test_name) + "-" + std::to_string(timestamp));
    fs::create_directories(target_dir, ec);
    if (ec) {
        if (err) {
            *err = "Could not create artifact directory '" + target_dir.string() + "': " + ec.message();
        }
        return false;
    }

    auto cleanup_partial_artifact_dir = [&target_dir]() {
        std::error_code cleanup_ec;
        fs::remove_all(target_dir, cleanup_ec);
    };

    if (!copy_required_artifact(trace_path, target_dir / "trace.jsonl", err)) {
        cleanup_partial_artifact_dir();
        return false;
    }
    if (!copy_required_artifact(session_path, target_dir / "session.json", err)) {
        cleanup_partial_artifact_dir();
        return false;
    }

    if (artifact_dir != nullptr) {
        *artifact_dir = target_dir;
    }
    return true;
}

}  // namespace

bool net_smoke_keep_artifacts_enabled() {
    if (const char* env_value = std::getenv(kKeepArtifactsEnv)) {
        return std::string(env_value) == "1";
    }
    return false;
}

fs::path net_smoke_artifact_root_from_current_executable(std::string* err) {
    std::error_code ec;
    const fs::path executable_path = fs::read_symlink("/proc/self/exe", ec);
    if (ec) {
        if (err) {
            *err = "Could not resolve /proc/self/exe: " + ec.message();
        }
        return {};
    }
    const fs::path build_dir = executable_path.parent_path().parent_path();
    if (build_dir.empty()) {
        if (err) {
            *err = "Could not derive build directory from executable path '" + executable_path.string() + "'.";
        }
        return {};
    }
    return build_dir / "test-artifacts" / "net_smoke";
}

std::string sanitize_net_smoke_artifact_component(const std::string& raw) {
    std::string sanitized;
    sanitized.reserve(raw.size());
    for (unsigned char ch : raw) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        sanitized = "unnamed_test";
    }
    return sanitized;
}

bool cleanup_net_smoke_workspace(const fs::path& workspace,
                                 const fs::path& session_path,
                                 const fs::path& trace_path,
                                 const std::string& test_name,
                                 std::string* err,
                                 fs::path* artifact_dir) {
    if (artifact_dir != nullptr) {
        artifact_dir->clear();
    }
    if (workspace.empty()) {
        return true;
    }

    if (net_smoke_keep_artifacts_enabled()) {
        if (!copy_net_smoke_artifacts(session_path, trace_path, test_name, err, artifact_dir)) {
            return false;
        }
    }

    std::error_code ec;
    fs::remove_all(workspace, ec);
    if (ec) {
        if (err) {
            *err = "Could not remove net smoke workspace '" + workspace.string() + "': " + ec.message();
        }
        return false;
    }
    return true;
}
