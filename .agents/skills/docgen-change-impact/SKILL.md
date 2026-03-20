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

# Step 0 (Required)

Before any analysis, read the Documentation Automation Rules from this repository:

```bash
sed -n '/## Documentation Automation Rules/,/## AI Customization Layout/p' AGENTS.md
```

If that command returns nothing (e.g. the heading is missing), **fail explicitly** with a clear error. Do not proceed without the rules section.

# Inputs

- **Change facts**: `docs/generated/change_facts.json` (from `scripts/docgen/change_facts.py` or `changed_context.py --format json`). Contains `diff_ref`, `changed_files`, `public_surface_signals`, `large_refactor_signals`.
- Optionally: `docs/generated/repo_understanding_summary.md` for repository context.

# Output Target (Doc Scope Decision)

Write **`docs/generated/doc_scope_decision.json`** only. All paths must be **repository-relative**. Never emit absolute paths. **Target docs are fixed**: only `README.md` and `book/src/**/*.md`. Do not output a list of "candidate docs" or "affected docs" across the whole repo.

# Doc Scope Decision Schema

Produce a single JSON file with:

- **update_readme** (boolean): whether this change requires updating README.md.
- **update_book** (boolean): whether this change requires updating any part of the book (book/src).
- **book_dirs_or_chapters** (array of strings): which parts of the book are in scope (e.g. `["book/src"]` or specific paths under book/src).
- **approved_targets** (array of strings): explicit list of all target documents approved for this run (repo-relative). Must include both `README.md` and `README_zh.md` if update_readme is true (they are always updated together as a bilingual pair); must include each path in book_dirs_or_chapters and book/src/SUMMARY.md when any book path is in scope. This is the canonical boundary for downstream rework; rationale must not contradict it.
- **directory_actions** (optional): `"none"`, `"add"`, `"delete"`, `"merge"`, or `"reorg"` if the change implies directory-level changes.
- **rationale** (string): short justification for the scope decision. Must not deny any path in approved_targets (e.g. do not say "no new chapter" if a chapter path is in approved_targets).
- **scope_inconsistency** (optional, string): if you detect rationale and approved_targets would conflict, set this to a brief description so downstream can treat it as unresolved.

# Required Steps (reason from the change, do not rely on fixed rules)

1. (Step 0) Read Documentation Automation Rules as above; fail if missing.
2. Read `docs/generated/change_facts.json` and **analyze what the change represents**: Did it **add new modules**, **new user-facing capabilities**, or **new concepts**? Or only modify existing behavior?
3. Decide whether docs need updating; if so, README, book, or both (target set: README + book only).
4. For the book:
   - If the change **only** updates existing behavior: set existing chapter(s) in `book_dirs_or_chapters`, `directory_actions` can be `"none"`.
   - If the change **introduces new modules or new capabilities** that deserve their own explanation: set `directory_actions` to **`"add"`** and specify that a **new chapter** (or new section) is needed in `book_dirs_or_chapters` (e.g. a new file under book/src and an entry in SUMMARY.md). Infer the need from the changed files and their meaning — do not rely on path keywords.
5. Write `docs/generated/doc_scope_decision.json`; in `rationale`, state what you inferred (e.g. "New doc automation pipeline added; book should add a chapter for it").

# Failure Modes

- Using fixed rules (e.g. "if path contains X then update_book true") instead of **inferring from the change** whether new modules/concepts were added.
- Treating all large diffs as requiring doc rewrites (over-reaction).
- Missing that a change **added new structure** and therefore the book should **add a new chapter**, not only edit existing text.
- Ignoring test changes that reveal new contracts.
- Only reading diff without reading the old doc (context loss).
- Using absolute paths in the report.

# Validation Handoff

- Downstream: `reference_context` script consumes doc_scope_decision.json; then doc_restructure_or_update (tutorial-update) updates README and book only.

# Dependencies

- Requires `docs/generated/change_facts.json` (from `scripts/docgen/change_facts.py` or `changed_context.py --format json`).
- **Codex App/IDE note**: interactive mode can show diffs visually; `codex exec` mode should pass the diff range explicitly via environment or argument (e.g. `REF=main`).
