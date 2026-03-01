#pragma once

#include "config.hpp"
#include <string>

// Initialize workspace: creates it if missing, and captures absolute path to config->workspace_abs.
bool workspace_init(AgentConfig* cfg, std::string* err);

// Resolve a relative path against workspace.
// Returns false if absolute, if trying to escape workspace boundaries, etc.
bool workspace_resolve(const AgentConfig& cfg, const std::string& rel, std::string* out_abs, std::string* err);
