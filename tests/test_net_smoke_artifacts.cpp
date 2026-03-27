#include <gtest/gtest.h>

#include "net_smoke_artifacts.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string read_file_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

class ScopedEnvVar {
public:
    ScopedEnvVar(std::string name, const char* value) : name_(std::move(name)) {
        const char* old_value = std::getenv(name_.c_str());
        if (old_value != nullptr) {
            had_old_value_ = true;
            old_value_ = old_value;
        }

        if (value != nullptr) {
            setenv(name_.c_str(), value, 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    ~ScopedEnvVar() {
        if (had_old_value_) {
            setenv(name_.c_str(), old_value_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    bool had_old_value_ = false;
    std::string old_value_;
};

}  // namespace

class NetSmokeArtifactTest : public ::testing::Test {
protected:
    fs::path temp_dir;
    std::vector<fs::path> artifact_dirs_to_cleanup;

    void SetUp() override {
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        temp_dir = fs::temp_directory_path() / ("nano_net_smoke_artifacts_" + unique);
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        for (const fs::path& artifact_dir : artifact_dirs_to_cleanup) {
            std::error_code ec;
            fs::remove_all(artifact_dir, ec);
        }

        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    fs::path make_workspace(const std::string& name) {
        const fs::path workspace = temp_dir / name;
        fs::create_directories(workspace);
        return workspace;
    }
};

TEST_F(NetSmokeArtifactTest, KeepArtifactsCopiesRequiredFilesToStableDirectory) {
    ScopedEnvVar keep_artifacts("NCA_NET_SMOKE_KEEP_ARTIFACTS", "1");

    const fs::path workspace = make_workspace("keep");
    const fs::path session_path = workspace / "session.json";
    const fs::path trace_path = workspace / "trace.jsonl";

    {
        std::ofstream output(session_path, std::ios::binary);
        output << "{\"session\":true}\n";
    }
    {
        std::ofstream output(trace_path, std::ios::binary);
        output << "{\"kind\":\"run_finished\"}\n";
    }

    std::string err;
    fs::path artifact_dir;
    ASSERT_TRUE(cleanup_net_smoke_workspace(workspace,
                                           session_path,
                                           trace_path,
                                           "NetPlanningSmokeTest.KeepArtifactsCopiesRequiredFilesToStableDirectory",
                                           &err,
                                           &artifact_dir))
        << err;
    ASSERT_FALSE(artifact_dir.empty());
    artifact_dirs_to_cleanup.push_back(artifact_dir);

    std::string root_err;
    const fs::path artifact_root = net_smoke_artifact_root_from_current_executable(&root_err);
    ASSERT_TRUE(root_err.empty()) << root_err;
    EXPECT_EQ(artifact_dir.parent_path(), artifact_root);
    EXPECT_FALSE(fs::exists(workspace));
    EXPECT_TRUE(fs::exists(artifact_dir / "session.json"));
    EXPECT_TRUE(fs::exists(artifact_dir / "trace.jsonl"));
    EXPECT_EQ(read_file_text(artifact_dir / "session.json"), "{\"session\":true}\n");
    EXPECT_EQ(read_file_text(artifact_dir / "trace.jsonl"), "{\"kind\":\"run_finished\"}\n");

    const std::string prefix = sanitize_net_smoke_artifact_component(
        "NetPlanningSmokeTest.KeepArtifactsCopiesRequiredFilesToStableDirectory");
    EXPECT_TRUE(artifact_dir.filename().string().starts_with(prefix + "-"));
}

TEST_F(NetSmokeArtifactTest, DefaultCleanupRemovesWorkspaceWithoutStableArtifacts) {
    ScopedEnvVar keep_artifacts("NCA_NET_SMOKE_KEEP_ARTIFACTS", nullptr);

    const fs::path workspace = make_workspace("default");
    const fs::path session_path = workspace / "session.json";
    const fs::path trace_path = workspace / "trace.jsonl";
    const std::string test_name =
        "NetSmokeArtifactTest.DefaultCleanup_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    {
        std::ofstream output(session_path, std::ios::binary);
        output << "{}\n";
    }
    {
        std::ofstream output(trace_path, std::ios::binary);
        output << "{}\n";
    }

    std::string err;
    fs::path artifact_dir;
    ASSERT_TRUE(cleanup_net_smoke_workspace(workspace, session_path, trace_path, test_name, &err, &artifact_dir))
        << err;
    EXPECT_TRUE(artifact_dir.empty());
    EXPECT_FALSE(fs::exists(workspace));

    std::string root_err;
    const fs::path artifact_root = net_smoke_artifact_root_from_current_executable(&root_err);
    ASSERT_TRUE(root_err.empty()) << root_err;
    if (!fs::exists(artifact_root)) {
        SUCCEED();
        return;
    }

    const std::string prefix = sanitize_net_smoke_artifact_component(test_name) + "-";
    bool found_matching_dir = false;
    for (const fs::directory_entry& entry : fs::directory_iterator(artifact_root)) {
        if (entry.path().filename().string().starts_with(prefix)) {
            found_matching_dir = true;
            break;
        }
    }
    EXPECT_FALSE(found_matching_dir);
}

TEST_F(NetSmokeArtifactTest, KeepArtifactsFailurePreservesWorkspace) {
    ScopedEnvVar keep_artifacts("NCA_NET_SMOKE_KEEP_ARTIFACTS", "1");

    const fs::path workspace = make_workspace("missing");
    const fs::path session_path = workspace / "session.json";
    const fs::path trace_path = workspace / "trace.jsonl";

    {
        std::ofstream output(session_path, std::ios::binary);
        output << "{\"session\":true}\n";
    }

    std::string err;
    fs::path artifact_dir;
    EXPECT_FALSE(cleanup_net_smoke_workspace(workspace,
                                             session_path,
                                             trace_path,
                                             "NetPlanningSmokeTest.KeepArtifactsFailurePreservesWorkspace",
                                             &err,
                                             &artifact_dir));
    EXPECT_TRUE(artifact_dir.empty());
    EXPECT_NE(err.find("trace.jsonl"), std::string::npos);
    EXPECT_TRUE(fs::exists(workspace));
    EXPECT_TRUE(fs::exists(session_path));
}
