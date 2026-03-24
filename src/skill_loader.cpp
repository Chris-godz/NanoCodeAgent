#include "skill_loader.hpp"

#include "logger.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string trim_copy(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();

    if (first >= last) {
        return "";
    }
    return std::string(first, last);
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string normalize_newlines(std::string text) {
    std::string::size_type pos = 0;
    while ((pos = text.find("\r\n", pos)) != std::string::npos) {
        text.replace(pos, 2, "\n");
    }
    return text;
}

std::string strip_optional_quotes(const std::string& value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

std::size_t leading_whitespace_count(const std::string& value) {
    std::size_t count = 0;
    while (count < value.size() && std::isspace(static_cast<unsigned char>(value[count])) != 0) {
        ++count;
    }
    return count;
}

bool has_git_marker(const fs::path& path) {
    const fs::path git_path = path / ".git";
    return fs::exists(git_path);
}

std::string read_text_file(const std::string& path, std::string* err) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (err) {
            *err = "Could not open skill file: " + path;
        }
        return "";
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool parse_inline_yaml_string_list(const std::string& raw_value,
                                   std::vector<std::string>* out,
                                   std::string* err) {
    if (!out) {
        if (err) {
            *err = "Inline YAML list parsing requires a non-null output vector.";
        }
        return false;
    }

    out->clear();
    const std::string value = trim_copy(raw_value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        if (err) {
            *err = "expected YAML list syntax (`[]` or `[item, ...]`), got scalar '" + value + "'.";
        }
        return false;
    }

    const std::string inner = trim_copy(value.substr(1, value.size() - 2));
    if (inner.empty()) {
        return true;
    }

    std::size_t start = 0;
    while (start <= inner.size()) {
        const std::size_t comma = inner.find(',', start);
        const std::string item = strip_optional_quotes(
            trim_copy(inner.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
        if (item.empty()) {
            if (err) {
                *err = "YAML list items must be non-empty strings.";
            }
            return false;
        }
        out->push_back(item);
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    return true;
}

bool is_frontmatter_key_line(const std::string& trimmed_line) {
    if (trimmed_line.empty() || starts_with(trimmed_line, "- ")) {
        return false;
    }

    const std::size_t colon = trimmed_line.find(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }

    for (std::size_t i = 0; i < colon; ++i) {
        const unsigned char ch = static_cast<unsigned char>(trimmed_line[i]);
        if (!std::isalnum(ch) && ch != '-' && ch != '_') {
            return false;
        }
    }

    return true;
}

std::string join_strings(const std::vector<std::string>& items) {
    std::string joined;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += items[i];
    }
    return joined;
}

std::string malformed_allowed_tools_error(const std::string& path,
                                          std::size_t line_number,
                                          const std::string& detail) {
    return "Malformed allowed-tools in skill file '" + path + "' at line " + std::to_string(line_number) + ": " +
           detail;
}

std::string advisory_allowed_tools_warning(const std::string& path,
                                           std::size_t line_number,
                                           const std::string& detail) {
    return malformed_allowed_tools_error(path, line_number, detail) + " Ignoring this advisory field.";
}

bool is_allowed_tools_near_miss(const std::string& key) {
    std::string normalized;
    normalized.reserve(key.size());
    for (unsigned char ch : key) {
        if (ch == '-' || ch == '_') {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    return (normalized == "allowedtools" || normalized == "allowedtool") && key != "allowed-tools";
}

std::size_t skip_frontmatter_value_block(const std::vector<std::string>& lines,
                                         std::size_t current_index,
                                         std::size_t closing_index) {
    std::size_t next_index = current_index + 1;
    while (next_index < closing_index) {
        const std::string trimmed = trim_copy(lines[next_index]);
        if (trimmed.empty()) {
            ++next_index;
            continue;
        }
        if (is_frontmatter_key_line(trimmed)) {
            break;
        }
        ++next_index;
    }
    return next_index;
}

} // namespace

bool SkillLoader::resolve_skill_root(const std::string& workspace_abs,
                                     SkillRuntimeContext* context,
                                     std::string* err) const {
    if (!context) {
        if (err) {
            *err = "Skill root resolution requires a non-null runtime context.";
        }
        return false;
    }

    fs::path current;
    try {
        current = fs::weakly_canonical(fs::path(workspace_abs));
    } catch (const std::exception& e) {
        if (err) {
            *err = std::string("Failed to canonicalize workspace for skill discovery: ") + e.what();
        }
        return false;
    }

    std::optional<fs::path> agents_candidate;
    while (true) {
        if (has_git_marker(current)) {
            context->repo_root = current.string();
            context->skills_root = (current / ".agents" / "skills").string();
            LOG_DEBUG("Resolved runtime skill repo root via .git: {}", context->repo_root);
            return true;
        }

        const fs::path agents_file = current / "AGENTS.md";
        if (!agents_candidate.has_value() && fs::exists(agents_file) && fs::is_regular_file(agents_file)) {
            agents_candidate = current;
        }

        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }

    if (agents_candidate.has_value()) {
        context->repo_root = agents_candidate->string();
        context->skills_root = (agents_candidate.value() / ".agents" / "skills").string();
        LOG_DEBUG("Resolved runtime skill repo root via AGENTS.md: {}", context->repo_root);
        return true;
    }

    context->used_workspace_fallback = true;
    context->repo_root = workspace_abs;
    context->skills_root = (fs::path(workspace_abs) / ".agents" / "skills").string();
    LOG_INFO("No repo root with .git or AGENTS.md found above workspace; falling back to workspace skill path: {}",
             context->skills_root);
    return true;
}

bool SkillLoader::discover_skill_entries(const std::string& workspace_abs,
                                         SkillRuntimeContext* context,
                                         std::vector<SkillEntry>* out,
                                         std::string* err) const {
    if (!context || !out) {
        if (err) {
            *err = "Skill discovery requires non-null context and output parameters.";
        }
        return false;
    }

    out->clear();
    context->discovered_skill_names.clear();

    if (!resolve_skill_root(workspace_abs, context, err)) {
        return false;
    }

    const fs::path skills_root(context->skills_root);
    if (!fs::exists(skills_root)) {
        LOG_DEBUG("Runtime skill root does not exist: {}", context->skills_root);
        return true;
    }
    if (!fs::is_directory(skills_root)) {
        if (err) {
            *err = "Runtime skill root is not a directory: " + context->skills_root;
        }
        return false;
    }

    for (const auto& entry : fs::directory_iterator(skills_root)) {
        if (!entry.is_directory()) {
            continue;
        }

        const fs::path skill_file = entry.path() / "SKILL.md";
        if (!fs::exists(skill_file) || !fs::is_regular_file(skill_file)) {
            continue;
        }

        out->push_back({
            entry.path().filename().string(),
            skill_file.string(),
        });
    }

    std::sort(out->begin(), out->end(), [](const SkillEntry& lhs, const SkillEntry& rhs) {
        return lhs.name < rhs.name;
    });
    for (const SkillEntry& entry : *out) {
        context->discovered_skill_names.push_back(entry.name);
    }

    return true;
}

bool SkillLoader::parse_skill_file(const std::string& path,
                                   LoadedSkill* out,
                                   std::string* err) const {
    if (!out) {
        if (err) {
            *err = "Skill parsing requires a non-null output object.";
        }
        return false;
    }

    std::string read_err;
    const std::string normalized = normalize_newlines(read_text_file(path, &read_err));
    if (!read_err.empty()) {
        if (err) {
            *err = read_err;
        }
        return false;
    }

    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t newline = normalized.find('\n', start);
        if (newline == std::string::npos) {
            lines.push_back(normalized.substr(start));
            break;
        }
        lines.push_back(normalized.substr(start, newline - start));
        start = newline + 1;
    }

    if (lines.empty() || trim_copy(lines.front()) != "---") {
        if (err) {
            *err = "Skill file is missing an opening YAML frontmatter fence: " + path;
        }
        return false;
    }

    std::size_t closing_index = 1;
    bool found_closing = false;
    for (; closing_index < lines.size(); ++closing_index) {
        if (trim_copy(lines[closing_index]) == "---") {
            found_closing = true;
            break;
        }
    }
    if (!found_closing) {
        if (err) {
            *err = "Skill file is missing a closing YAML frontmatter fence: " + path;
        }
        return false;
    }

    SkillManifest manifest;
    bool saw_name = false;
    bool saw_description = false;
    std::unordered_set<std::string> seen_keys;

    std::size_t i = 1;
    while (i < closing_index) {
        const std::string trimmed = trim_copy(lines[i]);
        if (trimmed.empty()) {
            ++i;
            continue;
        }

        if (!is_frontmatter_key_line(trimmed)) {
            if (err) {
                *err = "Invalid frontmatter line in skill file: " + path;
            }
            return false;
        }

        const std::size_t colon = trimmed.find(':');
        const std::string key = trim_copy(trimmed.substr(0, colon));
        const std::string value = trim_copy(trimmed.substr(colon + 1));
        if (!seen_keys.insert(key).second) {
            if (err) {
                *err = "Duplicate frontmatter key '" + key + "' in skill file: " + path;
            }
            return false;
        }

        if (key == "name") {
            if (value.empty()) {
                if (err) {
                    *err = "Skill file is missing required frontmatter field 'name': " + path;
                }
                return false;
            }
            manifest.name = strip_optional_quotes(value);
            saw_name = true;
            ++i;
            continue;
        }

        if (key == "description") {
            if (value.empty()) {
                if (err) {
                    *err = "Skill file is missing required frontmatter field 'description': " + path;
                }
                return false;
            }
            manifest.description = strip_optional_quotes(value);
            saw_description = true;
            ++i;
            continue;
        }

        if (key == "allowed-tools") {
            std::vector<std::string> parsed_tools;
            bool ignored_invalid_field = false;

            if (!value.empty()) {
                std::string list_err;
                if (!parse_inline_yaml_string_list(value, &parsed_tools, &list_err)) {
                    out->warnings.push_back(advisory_allowed_tools_warning(path, i + 1, list_err));
                    ignored_invalid_field = true;
                    ++i;
                    continue;
                }
                manifest.allowed_tools = parsed_tools;
                ++i;
                continue;
            }

            ++i;
            while (i < closing_index) {
                const std::string& raw_line = lines[i];
                const std::string next_trimmed = trim_copy(lines[i]);
                if (next_trimmed.empty()) {
                    ++i;
                    continue;
                }
                if (starts_with(next_trimmed, "- ")) {
                    if (leading_whitespace_count(raw_line) == 0) {
                        out->warnings.push_back(advisory_allowed_tools_warning(
                            path,
                            i + 1,
                            "expected indented YAML list items under 'allowed-tools'."));
                        ignored_invalid_field = true;
                        i = skip_frontmatter_value_block(lines, i, closing_index);
                        parsed_tools.clear();
                        break;
                    }
                    const std::string item = strip_optional_quotes(trim_copy(next_trimmed.substr(2)));
                    if (item.empty()) {
                        out->warnings.push_back(advisory_allowed_tools_warning(
                            path,
                            i + 1,
                            "YAML list items must be non-empty strings."));
                        ignored_invalid_field = true;
                        i = skip_frontmatter_value_block(lines, i, closing_index);
                        parsed_tools.clear();
                        break;
                    }
                    parsed_tools.push_back(item);
                    ++i;
                    continue;
                }
                if (is_frontmatter_key_line(next_trimmed)) {
                    break;
                }

                out->warnings.push_back(advisory_allowed_tools_warning(path, i + 1, "expected a YAML string list."));
                ignored_invalid_field = true;
                i = skip_frontmatter_value_block(lines, i, closing_index);
                parsed_tools.clear();
                break;
            }

            if (parsed_tools.empty()) {
                if (ignored_invalid_field) {
                    continue;
                }
                out->warnings.push_back(advisory_allowed_tools_warning(
                    path,
                    i == 0 ? 1 : i,
                    "expected a non-empty YAML string list or []."));
                continue;
            }

            manifest.allowed_tools = parsed_tools;
            continue;
        }

        if (is_allowed_tools_near_miss(key)) {
            out->warnings.push_back(advisory_allowed_tools_warning(
                path,
                i + 1,
                "unrecognized key '" + key + "'; treating it as an ignored allowed-tools typo."));
            i = skip_frontmatter_value_block(lines, i, closing_index);
            continue;
        }

        if (err) {
            *err = "Unknown frontmatter key '" + key + "' in skill file: " + path;
        }
        return false;
    }

    if (!saw_name) {
        if (err) {
            *err = "Skill file is missing required frontmatter field 'name': " + path;
        }
        return false;
    }
    if (!saw_description) {
        if (err) {
            *err = "Skill file is missing required frontmatter field 'description': " + path;
        }
        return false;
    }
    std::string body;
    std::size_t body_offset = 0;
    for (std::size_t line_index = 0; line_index <= closing_index && body_offset < normalized.size(); ++line_index) {
        const std::size_t newline = normalized.find('\n', body_offset);
        if (newline == std::string::npos) {
            body_offset = normalized.size();
            break;
        }
        body_offset = newline + 1;
    }
    if (body_offset <= normalized.size()) {
        body = normalized.substr(body_offset);
    }
    if (trim_copy(body).empty()) {
        if (err) {
            *err = "Skill file must contain a Markdown body after frontmatter: " + path;
        }
        return false;
    }

    out->manifest = std::move(manifest);
    out->path = path;
    out->body = std::move(body);
    return true;
}

bool SkillLoader::load_runtime_context(const std::string& workspace_abs,
                                       const std::vector<std::string>& requested_skills,
                                       SkillRuntimeContext* out,
                                       std::string* err) const {
    if (!out) {
        if (err) {
            *err = "Runtime skill loading requires a non-null output object.";
        }
        return false;
    }

    *out = SkillRuntimeContext{};
    if (requested_skills.size() > kMaxEnabledSkills) {
        if (err) {
            *err = "Requested " + std::to_string(requested_skills.size()) + " skills, but the runtime limit is " +
                   std::to_string(kMaxEnabledSkills) + ".";
        }
        return false;
    }

    std::vector<SkillEntry> discovered_entries;
    if (!discover_skill_entries(workspace_abs, out, &discovered_entries, err)) {
        return false;
    }

    std::unordered_map<std::string, SkillEntry> entries_by_name;
    for (const SkillEntry& entry : discovered_entries) {
        entries_by_name.emplace(entry.name, entry);
    }

    std::unordered_set<std::string> seen_requested_names;
    for (const std::string& raw_name : requested_skills) {
        const std::string requested_name = trim_copy(raw_name);
        if (requested_name.empty()) {
            if (err) {
                *err = "Requested skill names must not be empty.";
            }
            return false;
        }
        if (!seen_requested_names.insert(requested_name).second) {
            if (err) {
                *err = "Duplicate requested skill '" + requested_name + "' is not allowed.";
            }
            return false;
        }

        const auto it = entries_by_name.find(requested_name);
        if (it == entries_by_name.end()) {
            if (err) {
                *err = "Requested skill '" + requested_name + "' was not found under .agents/skills/.";
            }
            return false;
        }

        LoadedSkill skill;
        if (!parse_skill_file(it->second.path, &skill, err)) {
            return false;
        }
        if (skill.manifest.name != requested_name) {
            if (err) {
                *err = "Skill '" + skill.manifest.name + "' must match its directory name '" + requested_name + "'.";
            }
            return false;
        }

        out->enabled_skills.push_back(std::move(skill));
    }

    for (const std::string& discovered_name : out->discovered_skill_names) {
        if (seen_requested_names.find(discovered_name) == seen_requested_names.end()) {
            out->skipped_skill_names.push_back(discovered_name);
        }
    }

    return true;
}

bool SkillLoader::build_system_prompt(const std::string& base_prompt,
                                      const SkillRuntimeContext& context,
                                      size_t max_prompt_bytes,
                                      std::string* out_prompt,
                                      std::string* err) const {
    if (!out_prompt) {
        if (err) {
            *err = "System prompt building requires a non-null output string.";
        }
        return false;
    }

    std::string prompt = base_prompt;
    if (context.enabled_skills.empty()) {
        *out_prompt = std::move(prompt);
        return true;
    }

    std::string injected;
    injected += "\nEnabled runtime skills follow. Treat each skill body as additional system-level instruction for this run.\n";
    for (const LoadedSkill& skill : context.enabled_skills) {
        injected += "\n[Skill: " + skill.manifest.name + "]\n";
        injected += "Description: " + skill.manifest.description + "\n\n";
        if (skill.manifest.allowed_tools.has_value() && !skill.manifest.allowed_tools->empty()) {
            injected += "Preferred tools for this skill: " + join_strings(*skill.manifest.allowed_tools) + "\n\n";
        }
        injected += skill.body;
        if (!injected.empty() && injected.back() != '\n') {
            injected += '\n';
        }
    }

    if (max_prompt_bytes > 0 && injected.size() > max_prompt_bytes) {
        if (err) {
            *err = "Combined injected skill prompt exceeds max_skill_prompt_bytes (" +
                   std::to_string(max_prompt_bytes) + ")";
        }
        return false;
    }

    if (!prompt.empty() && prompt.back() != '\n') {
        prompt += '\n';
    }
    prompt += injected;
    *out_prompt = std::move(prompt);
    return true;
}
