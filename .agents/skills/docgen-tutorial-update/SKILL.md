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

- Repository understanding summary (`docs/generated/repo_understanding_summary.md`).
- Change impact summary (`docs/generated/change_impact_summary.md`), if applicable.
- Target document to update (existing content).
- Adjacent documents and examples for context.
- Relevant document template from `docs/templates/`.

# Required Steps

1. Read the target document and its adjacent context.
2. Read the repo understanding and change impact summaries.
3. Select the appropriate template from `docs/templates/`.
4. Produce an outline before writing prose:
   - Section headings and key points per section.
5. Write each section, grounding every claim in source code or test evidence.
6. Add or update code examples, ensuring they reflect current APIs.
7. Add FAQ / common pitfalls section where relevant.
8. Unify terminology and style with existing docs.

# Output Format

- Updated document written in-place or to `docs/generated/` for review.
- The document should follow the selected template structure.
- Each section should be self-contained but link to related docs where appropriate.

# Constraints

- Do NOT fabricate CLI flags, config keys, or file paths.
- Do NOT rewrite sections unrelated to the current change unless necessary for coherence.
- Preserve existing narrative structure when the change is incremental.
- Mark uncertain claims with `<!-- TODO: verify -->` comments.

# Failure Modes

- Writing prose without reading the old document first (narrative breakage).
- Copying code examples verbatim from old docs without checking if they still work.
- Over-generalizing implementation details into universal rules.
- Terminology drift between sections.

# Validation Handoff

- Run `scripts/docgen/verify_paths.py` to confirm all referenced paths exist.
- Run `scripts/docgen/verify_links.py` to confirm internal links resolve.
- Pass the output to `docgen-fact-check` for factual verification.

# Dependencies

- Upstream: `docgen-repo-understanding` (recommended) and `docgen-change-impact` (if change-driven).
- Downstream: `docgen-fact-check` and `docgen-reviewer`.
- **Codex App/IDE note**: template selection benefits from interactive file picking in IDE mode. In `codex exec` mode, the template path should be specified explicitly.
