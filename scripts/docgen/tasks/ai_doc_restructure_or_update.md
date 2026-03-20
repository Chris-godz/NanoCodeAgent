# Task: Doc Restructure or Update (README + book only)

You are the docgen writer agent. You **apply** the scope decision: update README and/or book chapters.
Target docs are only `README.md`, `README_zh.md`, and Markdown chapters under `book/src/`.
Do not write to `docs/generated/candidates/`.

README and book use **different skills and different rules**. Read and follow the correct skill for each.

---

## Step 0 — Rules and skills (README vs book split)

Read the Documentation Automation Rules from AGENTS.md (section `## Documentation Automation Rules` to `## AI Customization Layout`).

**If `update_readme` is true in `docs/generated/doc_scope_decision.json`:**
- Read `.agents/skills/docgen-readme-patch/SKILL.md`.
- Do **not** read `docgen-tutorial-update` or `docgen-overview-writer` for README work. Those skills are for book chapters only.

**If `update_book` is true in `docs/generated/doc_scope_decision.json`:**
- Read `docs/documentation-collaboration-style.md` for writing style and expression order.
- Read `.agents/skills/docgen-tutorial-update/SKILL.md`.
- Read `.agents/skills/docgen-overview-writer/SKILL.md`. Apply overview-first order: Why → Big Picture → Main Flow → Module Roles → What you usually do → Boundaries → Dive deeper.
- If you add or revise Mermaid diagrams, also read `docs/templates/diagram-gallery.md` and `docs/templates/diagram-spec-template.md`, then follow the diagram rules in `docs/documentation-collaboration-style.md` (diagram type selection, one-diagram-one-idea, split-when-needed, caption responsibility, anti-patterns, visual-quality rules).

---

## Input

- Read `docs/generated/doc_scope_decision.json`: `update_readme`, `update_book`, `book_dirs_or_chapters`, `directory_actions`, `rationale`, `approved_targets`.
- Read `docs/generated/reference_context.json`: examples, tests, templates, source_paths, existing_doc_excerpts (reference only — do not treat these as target docs).
- Read `docs/generated/change_facts.json` for what changed.

---

## README (apply only if `update_readme` is true)

Follow the `docgen-readme-patch` skill rules:

- README has a **fixed top-level skeleton** (Why / Big Picture / Main Flows / Current Status / Documentation Automation / Quick Start / Roadmap / Future Directions). Do NOT add, delete, or reorder top-level sections.
- Apply only conservative, fact-driven patches **within existing sections**.
- Update Roadmap checkboxes and Current Status bullets when the change completes or adds capabilities.
- **Bilingual sync is mandatory**: every semantic change to `README.md` must have an equivalent patch in `README_zh.md`. Edit both files. Roadmap checkboxes must be identical in both. Do not translate section headings.
- If a change does not fit any existing section, do NOT create a new section. Leave the content out and note `scope_inconsistency` in your reasoning.

---

## Book (apply only if `update_book` is true)

Follow the `docgen-tutorial-update` and `docgen-overview-writer` skill rules:

- **If scope says only update existing content**: Edit the listed book chapter(s) in place. Preserve narrative; add or adjust sections as needed.
- **If scope says add new structure** (`directory_actions` is `"add"` and rationale or `book_dirs_or_chapters` indicates a new chapter):
  - **Create** the new chapter file under `book/src/` (e.g. `book/src/06-documentation-automation.md` or a name that matches the new capability).
  - **Update** `book/src/SUMMARY.md` to add an entry for the new chapter.
  - **Write** the new chapter content from the change and reference_context.
- If scope says delete/merge/reorg, remove or merge the indicated book content and adjust SUMMARY.md accordingly.

---

## Constraints

- Use only **repository-relative paths** in any new content. Never use absolute paths.
- Ground claims in the reference context (source, examples, tests); do not invent CLI flags or config keys.
- When changing existing sections, preserve narrative flow; mark uncertain claims with `<!-- TODO: verify -->` if needed.
- Do **not** write any file under `docs/generated/candidates/`. Do not create or modify docs outside README, README_zh.md, and book/src.
- **Book writing style**: Lead with Why and Big Picture; then Main Flow and Module Roles; then What you usually do (with prerequisites stated before or in that section); then Boundaries/Pitfalls; then Dive deeper. Do not open with a list of commands or scripts; build the reader's mental model first.
- **Diagram spec stage is mandatory**: For every key Mermaid figure that remains part of an in-scope chapter, write or refresh a diagram spec artifact under `docs/generated/diagram_specs/` using a slugged doc-path directory, for example `docs/generated/diagram_specs/book__src__01-overview/block-01.md`. Do this before revising Mermaid, and also when you keep an existing figure unchanged but still rely on it in the updated chapter. The spec must state the single question, reader outcome, explicit omissions, planned blocks/participants, and why the figure stays single or gets split.
- **Book diagram style**: Only use Mermaid when it adds structure, flow, boundary, or coverage understanding. Pick the correct diagram type, keep one diagram to one primary idea, keep labels short and scannable, move long explanation into the caption, and add a caption explaining what the figure shows, what it omits, and how to read it.
- **Split diagrams when clarity breaks**: Do not apply a hard node limit. If a figure can no longer maintain one meaning, stable reading direction, low crossing, and clear grouping, split it instead of forcing more content into one Mermaid block.
- **Mermaid render correctness is blocking**: Updated Mermaid fences must pass `scripts/docgen/verify_mermaid.py`. If rendering fails, fix the diagram before considering review feedback.
- **Rendered artifacts are part of the contract**: `scripts/docgen/verify_mermaid.py` generates rendered SVG/PNG artifacts under `docs/generated/diagram_artifacts/`; reviewer and rework are expected to use them.
- **Do not run verify scripts in the writer stage**: This task should produce the draft and required diagram specs, then stop. `run_verify_report.py` and the closed loop will run verification next. Do not spend Step 6 invoking `verify_paths.py`, `verify_links.py`, or `verify_mermaid.py` yourself.
