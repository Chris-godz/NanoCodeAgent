---
name: docgen-tutorial-update
description: Update or generate tutorial and guide documentation based on repo understanding and change impact.
allowed-tools: Bash, Read, Write, Edit
---

# Purpose

Produce or update teaching-oriented documentation (tutorials, guides, README sections) that is factually grounded in the current repository state. This skill consumes upstream context from repo-understanding and change-impact skills.

# When to Use

- After change-impact identifies docs that need updating.
- When creating a new tutorial for a feature.
- When a README or quickstart needs synchronization with code changes.

# Inputs

- **Doc scope decision**: `docs/generated/doc_scope_decision.json` (update_readme, update_book, book_dirs_or_chapters, directory_actions).
- **Reference context**: `docs/generated/reference_context.json` (examples, tests, templates, source_paths, existing_doc_excerpts). Reference only; target docs are README and book only.
- Optionally: a generated repo-understanding summary artifact, plus `docs/generated/change_facts.json`.

# Required Steps

1. Read doc_scope_decision.json: which target docs, and whether `directory_actions` is `"add"`, `"delete"`, `"merge"`, `"reorg"`, or `"none"`.
2. Read reference_context.json for facts and existing doc excerpts; do not treat it as a list of target docs.
3. Read current README.md and in-scope book/src files (and `book/src/SUMMARY.md` if you will add or remove chapters).
4. If the target chapter includes Mermaid diagrams or clearly needs one, read `docs/documentation-collaboration-style.md`, `docs/templates/diagram-gallery.md`, and `docs/templates/diagram-spec-template.md`.
5. For any in-scope chapter that contains a key Mermaid figure, create or refresh a diagram spec artifact under `docs/generated/diagram_specs/` using a slugged doc-path directory, for example `docs/generated/diagram_specs/book__src__01-overview/block-01.md`. Do this before writing or revising Mermaid, and also when you keep an existing figure but rely on it in the updated chapter. The spec is mandatory writer output, not hidden reasoning.
6. Mermaid diagrams must pass render verification. Treat render errors as blocking verify failures, not reviewer polish items.
7. `scripts/docgen/verify_mermaid.py` now emits rendered SVG/PNG artifacts under `docs/generated/diagram_artifacts/`; these are required inputs for visual review.
8. **If scope says add new structure** (`directory_actions` is `"add"`): Create the new chapter file(s) under book/src, update SUMMARY.md to list them, and **write** the new chapter content from the change and reference context (what the new module/capability does). Do not only edit existing files — add new structure when the change introduced new modules or capabilities.
9. **If scope says only update existing**: Edit README.md and/or the listed book chapters in place.
10. If scope says delete/merge/reorg, adjust book content and SUMMARY.md accordingly.
11. Ground every claim in reference context or source; do not fabricate CLI/config/paths. Do not write to `docs/generated/candidates/`.
12. In the E2E writer stage, stop after drafting the target docs and required diagram specs. Do not run the verifier scripts yourself; the pipeline runs verify next.

# Output Format

- **README.md and book/src/**\*.md only. No output under docs/generated/candidates/.
- When scope decision says **add**: create new .md under book/src and add entry to SUMMARY.md; write the new chapter. When scope says **none** or only existing paths: in-place updates. When scope says delete/merge/reorg: adjust structure and SUMMARY.md.

# Constraints

- Do NOT fabricate CLI flags, config keys, or file paths.
- Do NOT rewrite sections unrelated to the current change unless necessary for coherence.
- Preserve existing narrative structure when the change is incremental.
- Mark uncertain claims with `<!-- TODO: verify -->` comments.
- When editing diagrams, prefer small corrective changes over whole-chapter rewrites: let the diagram spec decide one-diagram-one-idea, split-vs-single, and what details stay out of the figure.
- Do not use a hard node-count rule. If a diagram remains clear, grouped, and easy to read, a larger node set is acceptable; when that clarity breaks, split the diagram instead of compressing more detail into one figure.
- Reviewability matters as much as renderability: diagram labels should stay short and scannable, long explanation should move to the caption, and the rendered screenshot should read like a formal document figure rather than a debug sketch.

# Failure Modes

- Writing prose without reading the old document first (narrative breakage).
- Copying code examples verbatim from old docs without checking if they still work.
- Over-generalizing implementation details into universal rules.
- Terminology drift between sections.

# Validation Handoff

- Run `scripts/docgen/run_verify_report.py` for the in-scope target set, or run `scripts/docgen/verify_paths.py` / `scripts/docgen/verify_links.py` one target doc per invocation. Do not pass multiple markdown targets to a verifier that expects a single `doc_file`.
- Run `scripts/docgen/verify_mermaid.py` when target docs contain Mermaid diagrams, then keep the emitted artifact report and PNGs under `docs/generated/diagram_artifacts/` available for review (for example `docs/generated/diagram_artifacts/book__src__01-overview/report.json`).
- Pass the output to `docgen-fact-check` for factual verification.

# Dependencies

- Upstream: `docgen-repo-understanding` (recommended) and `docgen-change-impact` (if change-driven).
- Downstream: `docgen-fact-check` and `docgen-reviewer`.
- **Codex App/IDE note**: template selection benefits from interactive file picking in IDE mode. In `codex exec` mode, the template path should be specified explicitly.
