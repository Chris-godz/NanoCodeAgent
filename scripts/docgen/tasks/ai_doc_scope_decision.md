# Task: Doc Scope Decision (README + book only)

You are the doc-scope-decision agent. **Analyze** the change from the facts, then decide whether and how documentation should change. Target docs are only `README.md` and `book/src/**/*.md`. Do not output "candidate docs" or any `docs/*.md` as targets.

## Step 0 — Rules and skill

1. Read the Documentation Automation Rules: `sed -n '/## Documentation Automation Rules/,/## AI Customization Layout/p' AGENTS.md` or read that section from AGENTS.md.
2. Read `.agents/skills/docgen-change-impact/SKILL.md` (doc scope decision output).

## Input

- Read `docs/generated/change_facts.json`: `diff_ref`, `changed_files` (paths and change_type), `public_surface_signals`, `large_refactor_signals`.
- Optionally read `docs/generated/repo_understanding_summary.md` if present.

## Your job: reason from the change, not from rules

1. **Analyze what actually changed**
   - From the changed file paths and (if you can infer) their roles: did this change **introduce new modules**, **new user-facing capabilities**, or **new concepts** that users need to learn?
   - Or is it only edits to existing behavior (fixes, refactors with no new surface)?

2. **Decide README**
   - Does the project’s entry point (README) need to mention or link to something new? If yes, set `update_readme` true.

3. **Decide book: existing vs new structure**
   - If the change **only** touches existing concepts (e.g. fix CLI flags, update an existing chapter’s topic): set `update_book` true and list the **existing** chapter(s) to update in `book_dirs_or_chapters`; `directory_actions` can be `"none"`.
   - If the change **adds new modules or new capabilities** (e.g. a new pipeline, a new workflow, a new subsystem that users should read about): set `update_book` true, set `directory_actions` to **`"add"`**, and in `book_dirs_or_chapters` specify that a **new chapter or section** is needed (e.g. a new file under `book/src/` and an entry in `book/src/SUMMARY.md`). You are not limited to existing chapters — you are deciding that the book **should gain new structure** because the repo did.
   - Similarly, if the change removes or merges user-facing concepts, use `"delete"` or `"merge"` / `"reorg"` and say which parts of the book are affected.

4. **Rationale**
   - In `rationale`, briefly state what you inferred (e.g. "Change adds doc automation pipeline; book should add a chapter describing it") so the next step can add and fill that structure.

## Output

- Write **exactly one file**: `docs/generated/doc_scope_decision.json`.
- Schema (repository-relative only; no absolute paths):
  - **update_readme** (boolean)
  - **update_book** (boolean)
  - **book_dirs_or_chapters** (array of strings): existing paths to update, and/or a description/path for **new** chapter(s) if `directory_actions` is `"add"` (e.g. `["book/src", "book/src/06-doc-automation.md"]` or a short note that a new chapter is needed).
  - **approved_targets** (array of strings): **explicit list** of all target documents approved for this run (repo-relative). Must be consistent with the booleans and book list: if `update_readme` is true include both `"README.md"` **and `"README_zh.md"`** (README is a bilingual file — both versions are always updated together); if `update_book` is true include every path in `book_dirs_or_chapters` (concrete file paths under book/src), and if any book path is listed also include `"book/src/SUMMARY.md"`. Downstream rework uses this as the hard boundary; do not list a path here if the rationale says that document or chapter should not exist.
  - **directory_actions** (optional): `"none"` | `"add"` | `"delete"` | `"merge"` | `"reorg"` when the change implies new/removed/restructured book content.
  - **rationale** (string): short justification that reflects your analysis. Must **not** contradict `approved_targets`: e.g. do not state "no new chapter" or "do not add a chapter" if `approved_targets` includes a chapter path; use wording like "update existing chapter" or "add a chapter" as appropriate.
  - **scope_inconsistency** (optional, string): if you detect an internal conflict (e.g. rationale would contradict the approved target list), set this to a brief description so downstream and rework can treat it as an unresolved conflict instead of guessing.

## Constraints

- Do **not** list candidate docs or affected docs across the whole repo. Target set is only README and book.
- Use only **repository-relative paths**. Never use absolute paths.
- **Consistency**: `rationale` must not deny any path in `approved_targets`. If in doubt, set `scope_inconsistency` rather than outputting contradictory rationale and targets.
- **Bilingual pair**: `README.md` and `README_zh.md` are always approved or excluded together. Never include one without the other.
- Do not create or modify any file other than `docs/generated/doc_scope_decision.json`.
