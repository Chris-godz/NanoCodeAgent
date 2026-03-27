#pragma once

#include <filesystem>
#include <string>

bool net_smoke_keep_artifacts_enabled();
std::filesystem::path net_smoke_artifact_root_from_current_executable(std::string* err);
std::string sanitize_net_smoke_artifact_component(const std::string& raw);
bool cleanup_net_smoke_workspace(const std::filesystem::path& workspace,
                                 const std::filesystem::path& session_path,
                                 const std::filesystem::path& trace_path,
                                 const std::string& test_name,
                                 std::string* err,
                                 std::filesystem::path* artifact_dir);
