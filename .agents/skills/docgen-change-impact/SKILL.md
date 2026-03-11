---
name: docgen-change-impact
description: Analyze repository changes and map them to documentation impact.
allowed-tools: Bash, Read, Write, Edit
---

# Purpose

Translate code changes from a "file diff" perspective into a "documentation impact" perspective. This skill bridges the gap between what changed in code and what needs to change in docs.

# When to Use

- After any code change that might affect user-facing documentation.
- Before deciding which docs to update.
- When triaging whether a change requires a doc update at all.

# Inputs

- `git diff` output (staged or between branches).
- Changed files list.
- Related test changes.
- Output of `scripts/docgen/changed_context.py`.
- Existing documentation for affected modules.

# Required Steps

1. Collect the changed file list via `git diff --name-only` or equivalent.
2. Run `scripts/docgen/changed_context.py` to get per-file context: adjacent docs, related examples, and module summaries.
3. Classify each change as one or more of:
   - **Interface change**: public API, CLI flags, config keys modified.
   - **Behavior change**: same interface, different observable behavior.
   - **Documentation-only change**: typo fixes, rewording.
   - **Example change**: sample code, configuration examples.
   - **Internal-only change**: refactoring with no external impact.
4. For each affected module, identify which existing docs reference it.
5. Determine whether affected docs need a rewrite, incremental update, or no change.
6. Produce a prioritized impact report.

# Output Format

Write the report to `docs/generated/change_impact_summary.md` with:

- **Summary**: one-paragraph overview of the change scope.
- **Impacted Modules**: list of modules and the type of change.
- **Affected Docs**: which documents are now potentially stale.
- **Required Updates**: docs that must be updated, with rationale.
- **Optional Updates**: docs that could benefit from a refresh.
- **No-Action Items**: changes that do not affect docs.
- **Risks / Uncertainty**: areas where the impact is unclear.

# Failure Modes

- Treating all large diffs as requiring doc rewrites (over-reaction).
- Missing behavioral changes hidden in internal refactors.
- Ignoring test changes that reveal new contracts.
- Only reading diff without reading the old doc (context loss).

# Validation Handoff

- Cross-reference impacted docs list against `scripts/docgen/doc_inventory.py` output.
- Verify that listed doc paths actually exist.
- A downstream skill (tutorial-update or fact-check) should consume this report.

# Dependencies

- Requires a git history (at least one commit to diff against).
- **Codex App/IDE note**: interactive mode can show diffs visually; `codex exec` mode should pass the diff range explicitly via environment or argument.
