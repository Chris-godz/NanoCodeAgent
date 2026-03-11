---
name: docgen-repo-understanding
description: Build a global semantic model of the repository for documentation purposes.
allowed-tools: Bash, Read, Write, Edit
---

# Purpose

Establish a comprehensive, factual understanding of the entire repository before any documentation is written or updated. This skill produces a structured summary that downstream skills (change-impact, tutorial-update) consume as context.

# When to Use

- Before any large or cross-cutting documentation task.
- When onboarding to a repository for the first time.
- When significant structural changes make the previous understanding stale.

# Inputs

- Top-level README and docs directory.
- Core source entry points (`src/`, `include/`).
- Test entry points (`tests/`).
- Examples (if present).
- Output of `scripts/docgen/repo_map.py`.
- Output of `scripts/docgen/doc_inventory.py`.
- Output of `scripts/docgen/example_inventory.py`.

# Required Steps

0. **Read documentation automation rules** — extract only the targeted section from `AGENTS.md`:
   ```bash
   sed -n '/## Documentation Automation Rules/,/## AI Customization Layout/p' AGENTS.md
   ```
   If the heading `## Documentation Automation Rules` is not found, **stop and report an error**. Do NOT fall back to reading the entire file or generic engineering rules.

1. Run `scripts/docgen/repo_map.py --root . --output docs/generated/repo_map_test.md` to get the directory tree summary, key entry files, core module locations, and test/example distribution.
2. Run `scripts/docgen/doc_inventory.py --root . --output docs/generated/doc_inventory_test.md` to get the current docs index and staleness risk.
3. Run `scripts/docgen/example_inventory.py --root .` to map examples to tutorial sections.
4. Read the top-level README, architecture docs, and key header files.
5. Synthesize a structured understanding summary.

# Output Format

Write the summary to `docs/generated/repo_understanding_summary.md` with the following sections:

- **Project Summary**: one-paragraph description of what the project does.
- **Architecture Overview**: main modules, their roles, and how they interact.
- **Glossary**: key terms and their definitions as used in this repo.
- **Common Misconceptions**: things a newcomer is most likely to misunderstand.
- **Documentation Coverage**: which modules are well-documented, which are gaps.
- **Key Module List**: prioritized list of modules that documentation should focus on.

# Path Convention

- All file references in the generated summary MUST use **repository-relative paths** (e.g., `src/main.cpp`, `include/agent_loop.hpp`).
- **Never** emit absolute paths like `/home/user/project/src/main.cpp`.
- If listing "files read during this run", use relative paths only.

# Failure Modes

- Reading only README without examining source code leads to surface-level understanding.
- Ignoring tests misses behavioral contracts.
- Over-relying on file names without reading content causes misclassification.
- Using `sed -n '1,260p' AGENTS.md` instead of targeted section reading causes rule miss.
- Emitting absolute paths in generated output breaks portability.

# Validation Handoff

- The summary must reference actual file paths that exist in the repo.
- Run `scripts/docgen/verify_paths.py docs/generated/repo_understanding_summary.md --root .` to confirm all referenced paths are valid.
- Run `scripts/docgen/verify_links.py docs/generated/repo_understanding_summary.md --root .` to confirm internal links resolve.
- A human reviewer or downstream skill should spot-check at least 3 claims against source code.

# Dependencies

- **Codex App/IDE note**: this skill relies on the agent being able to execute Python scripts and read arbitrary repo files. In `codex exec` mode, ensure the working directory is the repo root and Python 3 is available.
- **Preferred entry point**: use `scripts/docgen/run_repo_understanding.sh` which automates steps 0-5 and runs verification.
