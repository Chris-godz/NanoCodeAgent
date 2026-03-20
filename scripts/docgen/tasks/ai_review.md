# Task: AI Review of Updated Documentation (README + book)

You are the docgen-reviewer agent. Review the **updated target docs** (README.md and book/src files that were updated this run) and produce a critique report. Do not rewrite the documents; only identify issues and give actionable feedback. Focus on teaching logic and readability, not just format.

## Step 0 — Rules and skill

1. Read the Documentation Automation Rules from AGENTS.md (section between `## Documentation Automation Rules` and `## AI Customization Layout`).
2. Read `docs/documentation-collaboration-style.md` (especially the reviewer criteria section: 审稿判断标准).
3. Read `.agents/skills/docgen-reviewer/SKILL.md`. Do not rewrite content; only find problems and suggest fixes.
4. Read `.agents/skills/doc-clarity-reviewer/SKILL.md`. Apply clarity checklist: map before details, narrative over inventory, design intent explained, dive-deeper entry; do not reduce review to format or spelling only.
5. If any target book chapter contains Mermaid diagrams, read `docs/templates/diagram-gallery.md` and apply the Diagram Reviewer SOP from `docs/documentation-collaboration-style.md`.

## Input

- Read `docs/generated/doc_scope_decision.json` to see which target docs were in scope (update_readme, update_book, book_dirs_or_chapters, approved_targets).
- Read the **actual updated files**: README.md and README_zh.md (if update_readme) and the in-scope files under book/src/ (if update_book).
- Read `docs/generated/verify_report.json` for path/link/command check results. Pay attention to `approved_targets`, `target_docs_checked`, `not_checked`, and `verify_coverage_note`. If a target in `approved_targets` was not checked (listed in `not_checked`), this is a **verify coverage gap** — classify it as `pipeline_editable`, not `doc_editable`, because it cannot be fixed by editing the document.
- Treat `book/src/SUMMARY.md` as a boundary-support file by default. Its normal links to other chapters do **not** expand this run’s scope and do not by themselves imply a verify coverage gap. Only raise a scope/coverage issue from `SUMMARY.md` when this run actually changed the summary structure (for example add/delete/merge/reorg of chapter entries) and the scope artifact failed to include the newly added or removed chapter path.
- If `docs/generated/verify_report.json` shows Mermaid render failures or Mermaid fence errors, classify them as **`pipeline_editable`** blocking issues in review output. Do not duplicate them as readability findings.
- If a target book chapter contains Mermaid diagrams, read the corresponding diagram spec artifact under `docs/generated/diagram_specs/` when present (for example `docs/generated/diagram_specs/book__src__01-overview/block-01.md`).
- If a target book chapter contains Mermaid diagrams, read the Mermaid artifact report referenced by `docs/generated/verify_report.json` (`artifact_report` from the `verify_mermaid.py` check) and inspect the rendered screenshot PNGs listed there. Visual review must be based on those screenshots, not only on Mermaid source.
- For every reviewed key diagram, explicitly record the repository-relative input paths you actually used: the diagram spec path, the Mermaid artifact report path, and the screenshot path(s). The report must mention these literal paths so the E2E evidence chain can audit that review consumed them.

## Output

- Write exactly one file: `docs/generated/docgen_review_report.md`.
- **Required**: Also write `docs/generated/docgen_review_report.json` so the orchestrator can classify and route issues. Each item in `must_fix`, `should_fix`, and `nice_to_have` must include an **`issue_class`** field with one of:
  - **`doc_editable`** — fixable by editing target docs (README / book). Examples: wrong section order, missing overview, command-before-prerequisites, prose clarity, scope-vs-wording inconsistency that can be resolved by rewording the doc.
  - **`pipeline_editable`** — requires changing a pipeline script, regenerating a generated artifact (e.g. `docs/generated/verify_report.json`), or fixing a contract between pipeline stages. Examples: verify coverage gap, stale generated evidence, scope/verify/summary contract mismatch. These must **not** be sent to doc rework.
  - **`ambiguous`** — cannot safely determine whether the fix belongs to doc editing or pipeline changes.
  - **`stale_or_resolved`** — the issue refers to a state that no longer matches the current scope or document content.
- Full JSON structure: `overall_assessment` (string), `clarity` (object), `must_fix: [{ "file", "section_or_line", "issue", "suggested_fix", "issue_class" }]`, `should_fix: [...]`, `nice_to_have: [...]`, `per_file_notes`. If you only write the .md, the orchestrator may use heuristics but will lack issue_class routing.
- The markdown report structure must include:
  - **Overall assessment** — brief summary (1–2 sentences).
  - **Clarity / 理解效果** — whether the doc gives a map first, avoids command-list-as-protagonist, explains design intent, is skimmable with a clear dive-deeper entry (per doc-clarity-reviewer and documentation-collaboration-style). Cite specific sections when flagging issues.
  - For chapters with diagrams, the **Clarity / 理解效果** section must also state whether each key diagram uses the correct diagram type, whether it helps understanding, whether it should remain one figure or be split, whether the screenshot-based visual review passes, and how these five rubric questions were answered: one main question, detail downshift, short scannable titles, noisy center, caption responsibility.
  - For chapters with diagrams, include a **Diagram evidence used** subsection listing the exact repo-relative diagram spec path, artifact report path, and screenshot path(s) that informed the review.
  - **Must fix** — issues that block publication (with specific section or line references where possible).
  - **Should fix** — issues that significantly improve quality.
  - **Nice to have** — minor polish suggestions.
  - **Per-file notes** — short conclusion per target file reviewed (path and 1–2 lines).
- For **book chapters**: focus on structure and clarity, user-facing readability, **teaching completeness**, **understanding effect** (overview-first, narrative over inventory), omissions relative to the change, factual drift from repo/code. Give **substantive critique**, not a generic checklist.
- For **book chapters with Mermaid diagrams**: classify diagram issues explicitly. Examples of `doc_editable` diagram issues: wrong diagram type, diagram-text mismatch, overly mixed semantics, unstable reading path, figure that should be split into two, missing or ineffective caption, test-coverage figure that reads like a file index, overly crowded screenshot, labels too long to scan, or an overview figure that visually reads like an implementation dump.
- Every diagram finding must explicitly label itself as **`semantic quality`** or **`visual quality`** in the issue text or section heading.
- Keep this distinction explicit: Mermaid syntax/render failures are verify-layer blocking issues; overview overload, mixed semantics, missing detail downshift, and diagram-text mismatch are semantic doc-editable review issues; crowding, long labels, weak visual center, and noisy screenshots are visual doc-editable review issues.
- For **README**: focus on skeleton integrity, bilingual consistency, conservative patch quality (no unnecessary rewrites), and factual accuracy of updated sections. Do not flag README for missing tutorial structure.

## README-Specific Review (apply when `update_readme` is true)

README is a **fixed-skeleton homepage**, not a tutorial. Apply these additional checks:

### Skeleton Integrity Check

The canonical top-level section order for `README.md` is:
Why This Repo Exists → Big Picture → Main Flows → Current Status → Documentation Automation → Quick Start → Roadmap → Future Directions

Check for:
- Any **new top-level section** (`## ...`) added that is not in the canonical order → `doc_editable`, must_fix
- Any **existing top-level section deleted** → `doc_editable`, must_fix
- Any **section order change** → `doc_editable`, must_fix

Do **not** apply tutorial-structure criteria (e.g. "missing overview section", "needs Big Picture subsection") to README — those rules are for book chapters only.

### Homepage Positioning Check

README is the **project entry page** for a first-time visitor. Its purpose is to answer "what is this, why does it exist, and where do I start?" — not to document every script or artifact. Flag the following as quality issues:

**Patch quality (must_fix if severe, should_fix if moderate):**
- The patch **polluted the homepage** with implementation details that belong in book chapters (e.g., listing individual script names, generated artifact paths, or internal pipeline stages as primary content of a top-level section)
- A section now opens with a list of commands, script calls, or file paths instead of a narrative explanation — "command-list-as-protagonist" in a homepage section → `doc_editable`, should_fix
- A section was **expanded disproportionately** (e.g., a brief "Quick Start" now contains detailed installation steps that belong in a getting-started chapter) → `doc_editable`, should_fix

**Entry-page role (should_fix):**
- The README no longer reads as a "project entry point" — a first-time reader cannot quickly understand what the project is about, why it exists, and where to go next → `doc_editable`, should_fix
- Excessive use of internal jargon or pipeline-specific terminology in sections that should serve a general audience → `doc_editable`, nice_to_have

**Scope creep (must_fix if a new `## ...` section was added for implementation details):**
- A new top-level section was added to house details that belong in a book chapter, not the homepage — this is both a skeleton violation and a homepage positioning issue → `doc_editable`, must_fix

### Bilingual Consistency Check

`README.md` and `README_zh.md` are a bilingual pair that must stay in sync. Check **all three directions**:

**Direction 1 — README.md changed, README_zh.md not updated:**
- If a section in `README.md` was added or modified but `README_zh.md` was not correspondingly patched → `doc_editable`, must_fix
- If Roadmap checkboxes in `README.md` differ from those in `README_zh.md` → `doc_editable`, must_fix

**Direction 2 — README_zh.md changed, README.md not updated:**
- If a section in `README_zh.md` was added or modified but `README.md` was not correspondingly updated → `doc_editable`, must_fix (bilingual drift can go both ways)

**Direction 3 — Both changed, but section correspondence is broken:**
- If a section exists in both files but covers materially different content (not just translation difference) → `doc_editable`, must_fix
- If a section heading exists in one file under a different parent section than in the other → `doc_editable`, should_fix

**Structural gaps:**
- If a top-level section exists in `README.md` but is absent from `README_zh.md` → `doc_editable`, should_fix (structural gap that should be added)
- If a top-level section exists in `README_zh.md` but is absent from `README.md` → `doc_editable`, should_fix

## Constraints

- Do not modify README, README_zh.md, or book files. Do not create files outside `docs/generated/`.
- Use only repository-relative paths in the report. Never use absolute paths.
- If no target docs were updated (scope said no update), still write the report with an overall assessment.
