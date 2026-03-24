#pragma once

#include "tool_registry.hpp"

#include <cstddef>

#include <optional>
#include <string>
#include <vector>

struct SkillManifest {
    std::string name;
    std::string description;
    std::optional<std::vector<std::string>> allowed_tools;
};

struct LoadedSkill {
    SkillManifest manifest;
    std::string path;
    std::string body;
    std::vector<std::string> warnings;
};

struct SkillRuntimeContext {
    std::string repo_root;
    std::string skills_root;
    bool used_workspace_fallback = false;
    std::vector<std::string> discovered_skill_names;
    std::vector<LoadedSkill> enabled_skills;
    std::vector<std::string> skipped_skill_names;
};

class SkillLoader {
public:
    static constexpr std::size_t kMaxEnabledSkills = 8;

    explicit SkillLoader(const ToolRegistry& registry) : registry_(registry) {}

    bool load_runtime_context(const std::string& workspace_abs,
                              const std::vector<std::string>& requested_skills,
                              SkillRuntimeContext* out,
                              std::string* err) const;

    bool build_system_prompt(const std::string& base_prompt,
                             const SkillRuntimeContext& context,
                             size_t max_prompt_bytes,
                             std::string* out_prompt,
                             std::string* err) const;

private:
    struct SkillEntry {
        std::string name;
        std::string path;
    };

    bool discover_skill_entries(const std::string& workspace_abs,
                                SkillRuntimeContext* context,
                                std::vector<SkillEntry>* out,
                                std::string* err) const;

    bool resolve_skill_root(const std::string& workspace_abs,
                            SkillRuntimeContext* context,
                         std::string* err) const;

    bool parse_skill_file(const std::string& path,
                          LoadedSkill* out,
                          std::string* err) const;

    const ToolRegistry& registry_;
};
