# Task: Review Rework (address doc-editable must-fix and optional should-fix)

You are the docgen agent that **reworks** documentation based on the review report. You address only issues classified as **`doc_editable`** — problems fixable by editing target docs (README / book). You must ignore issues classified as `pipeline_editable`, `stale_or_resolved`, or `ambiguous`; those are not your responsibility and must not drive your edits. Do not introduce content outside scope or break already-passing verification.

## Step 0 — Rules

1. Read the Documentation Automation Rules from AGENTS.md (section `## Documentation Automation Rules` to `## AI Customization Layout`).
2. Read `docs/documentation-collaboration-style.md` for writing style.
3. If the review report flags diagram-related `doc_editable` issues, also read `docs/templates/diagram-gallery.md`, the relevant diagram spec artifact under `docs/generated/diagram_specs/`, and the rendered screenshot artifacts under `docs/generated/diagram_artifacts/`, then apply the Diagram SOP when reworking Mermaid figures or captions.
4. Use only **repository-relative paths** in any content. Never use absolute paths.

## Input

- Read `docs/generated/doc_scope_decision.json` to determine **in-scope targets**: if the file contains **approved_targets**, that array is the canonical list of documents you may edit; otherwise derive it as: `README.md` if `update_readme` is true, plus each path in `book_dirs_or_chapters`, plus `book/src/SUMMARY.md` if `update_book` is true and any book path is listed. You may edit **only** those in-scope files.
- Read `docs/generated/docgen_review_report.json` (required) and optionally `docs/generated/docgen_review_report.md`. From the JSON, extract only items where **`issue_class` is `doc_editable`**:
  - **Must fix** with `issue_class: "doc_editable"` — you must address these.
  - **Should fix** with `issue_class: "doc_editable"` — address only if FIX_SHOULD=1.
  - **Skip entirely**: items with `issue_class` of `pipeline_editable`, `stale_or_resolved`, or `ambiguous`. Do not attempt to fix them; do not count them as failures. If the JSON is absent, fall back to the .md and treat all issues as `doc_editable` (legacy mode).
  - If the scope file has `scope_inconsistency`, treat it as a signal that scope and rationale may conflict; do not resolve such conflicts by deleting in-scope targets.
- Read the **actual in-scope files** to apply edits.

## Guardrails

- **In-scope target preservation**: You may only modify documents that are in the in-scope target list (see Input). You must **not** resolve review issues by **deleting** in-scope target documents or chapters, or by removing entries for in-scope chapters from `book/src/SUMMARY.md`. Destructive change (deleting an in-scope file or removing its SUMMARY entry to "fix" a scope-vs-content contradiction) is **forbidden** as a default fix.
- **Scope over rationale**: When the review report points to a conflict between scope and content (e.g. "rationale says no new chapter but the book has one"), treat the **approved target list** (or the derived list from `update_readme` / `book_dirs_or_chapters`) as the hard boundary. Do not delete a document or chapter that is in that list to satisfy a vague rationale; instead fix wording, rationale, or structure description, or report an unresolved conflict.
- **Prefer fixing explanation over deleting content**: When the issue is scope-vs-content inconsistency, prefer: correcting prose, clarifying rationale in the doc, adjusting structure or summary wording. Only if a fix is impossible without breaking the in-scope boundary (e.g. scope explicitly says "delete" for that path) and you cannot safely proceed, set `unresolved_conflict: true` and do **not** perform a destructive edit.
- **Conservative throw**: If you cannot fix must-fix items without either (a) deleting an in-scope target or (b) introducing content outside scope, set `unresolved_conflict: true` in the report, fill `unresolved_reason` and optionally `suggested_next_action` (e.g. `scope_clarification_required`), and do not make destructive changes. The orchestrator will treat this as a clear failure and stop.
- **Diagram rework stays small**: For diagram-related `doc_editable` issues, prefer changing diagram type, splitting the figure, regrouping, shortening labels, moving long explanation from the figure into the caption, or rewriting the caption. Do not use a diagram complaint as a reason to rewrite the whole chapter unless the review explicitly requires broader doc changes.
- **Do not rework verify-layer Mermaid failures here**: render errors and fence errors belong to blocking verify first. Rework here is for readable diagrams that already render.
- **Follow the spec unless the review says the spec itself was wrong**: if the rendered figure drifted away from the diagram spec, first restore spec alignment; if the review says the spec chose the wrong diagram type or split decision, update the spec and figure together.
- For diagram findings, preserve the reviewer’s distinction between **semantic quality** and **visual quality**. Your `action_taken` should say which kind you addressed and whether you changed diagram type, split the figure, regrouped it, shortened labels, or moved explanation into the caption.
- If you address a diagram-related finding, your report entry must also mention the exact diagram spec path and screenshot/artifact path(s) you used while reworking, using repository-relative paths.

## Output

1. **Edit only the in-scope files** to address each Must fix (and optionally Should fix) within the Guardrails above. Preserve verify-passing state: do not introduce invalid paths, broken links, or invalid commands.
2. Write exactly one file: `docs/generated/review_rework_report.json`:

```json
{
  "addressed_must_fix": [
    { "file": "<path>", "section_or_line": "...", "issue": "...", "action_taken": "...", "diagram_inputs_used": [] }
  ],
  "addressed_should_fix": [
    { "file": "...", "section_or_line": "...", "issue": "...", "action_taken": "...", "diagram_inputs_used": [] }
  ],
  "not_addressed": [
    { "file": "...", "issue": "...", "reason": "..." }
  ],
  "skipped_non_doc_issues": [
    { "issue": "...", "issue_class": "pipeline_editable|ambiguous|stale_or_resolved", "reason": "not doc_editable" }
  ],
  "reason": "<optional overall note>",
  "unresolved_conflict": false,
  "unresolved_reason": "",
  "suggested_next_action": ""
}
```

- Set **unresolved_conflict** to `true` only when you cannot safely fix the reported issues without violating the Guardrails (e.g. without deleting an in-scope target). Then set **unresolved_reason** to a short explanation and **suggested_next_action** (e.g. `scope_clarification_required`) if useful.
- List every Must fix you addressed in `addressed_must_fix`; any you could not address in `not_addressed` with reason. If FIX_SHOULD was enabled, list should-fix items in `addressed_should_fix` or `not_addressed`.

## Constraints

- **Must fix** items must be addressed where possible; if one cannot be fixed without breaking Guardrails, set `unresolved_conflict: true` and do not delete in-scope content.
- Do not add content outside the documented scope. Do not remove or alter sections that were not flagged, and **never delete an in-scope target document or its SUMMARY entry** to satisfy a review.
- After rework, the docs must still pass verification (paths, links, commands); do not introduce new blocking issues.
