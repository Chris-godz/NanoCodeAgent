---
name: docgen-readme-patch
description: Conservative patch skill for the repository homepage README. Enforces skeleton preservation and bilingual sync. Do NOT use for book/tutorial chapters.
allowed-tools:
  - Bash
  - Read
  - Write
  - Edit
---

# Purpose

Apply conservative, fact-driven patches to `README.md` and its bilingual mirror `README_zh.md`. This skill is **not** for tutorial or guide writing. It preserves a fixed homepage skeleton and only updates content within existing sections.

Use `docgen-tutorial-update` for `book/src/**/*.md`. Never apply that skill to README.

# README Skeleton (Fixed — Do Not Modify Structure)

The homepage README has a canonical top-level section order. This order must be preserved exactly:

1. Project header (badges, one-line description)
2. `## Why This Repo Exists`
3. `## Big Picture`
4. `## Main Flows`
5. `## Current Status`
6. `## Documentation Automation` (with subsections `### What You Usually Do`, `### Boundaries And Pitfalls`, `### Dive Deeper`)
7. `## Quick Start`
8. `## Roadmap` (with Phase 0 / Phase 1 / Phase 2 / Docs Agent Milestone subsections)
9. `## Future Directions`

The Chinese mirror `README_zh.md` has a shorter skeleton (subset of the above), which is also fixed and must not be structurally changed by this skill.

# Rules

## Skeleton Preservation

- **Do NOT add new top-level sections** (`## ...`). If a change seems to warrant a new section, do not create it. Record it as `scope_inconsistency` in your output and stop.
- **Do NOT delete or rename existing top-level sections.**
- **Do NOT reorder existing top-level sections.**
- Subsections within an existing section (e.g., a new `### Phase 3` under `## Roadmap`) are allowed when the change clearly justifies them and they fit the existing section's purpose.

## Content Patch Rules

- Edit only the sections whose content is factually affected by the current change.
- Do not rewrite unaffected sections, even if prose could be improved.
- Do not change the writing style or restructure paragraphs — preserve existing narrative voice.
- Update Roadmap checkboxes (`- [x]` / `- [ ]`) to reflect completed items from `change_facts.json`.
- Add new Roadmap items under the appropriate Phase when the change introduces new capabilities, but only as bullet items — not as new subsections.
- Update `## Current Status` bullet list if the change adds or completes a baseline capability.

## Bilingual Sync (Mandatory)

Every semantic change to `README.md` **must** have a corresponding patch in `README_zh.md`:

- Each `README.md` section patch → equivalent Chinese patch in the same section of `README_zh.md`.
- If a section exists in `README.md` but not in `README_zh.md`, add the section to `README_zh.md` with translated content. Do not leave the two files structurally mismatched.
- If `README_zh.md` is missing top-level sections that `README.md` has, add them in the correct position with translated content (this is a structural sync, not a skeleton violation).
- Roadmap checkboxes must be identical (`[x]`/`[ ]`) in both files.
- Do not translate section headings — keep them in English in both files (e.g., `## Why This Repo Exists`, `## Roadmap`) to maintain section identity.

## What to Do If a Change Does Not Fit the Skeleton

If the change introduces something that cannot naturally be described within any existing section:

1. Do **not** create a new top-level section.
2. Record the issue in your output with `scope_inconsistency: "<description of what does not fit>"`.
3. Patch only the sections that are clearly applicable.
4. Leave the rest for a human or pipeline-level scope decision.

# Required Steps

1. Read `docs/generated/doc_scope_decision.json`: confirm `update_readme: true`.
2. Read `docs/generated/change_facts.json`: understand what changed.
3. Read `docs/generated/reference_context.json`: use for factual grounding only. Do not copy examples verbatim.
4. Read current `README.md` fully before editing.
5. Read current `README_zh.md` fully before editing.
6. Identify which sections are affected by the change.
7. Apply conservative patches to `README.md` within affected sections only.
8. Apply equivalent Chinese patches to `README_zh.md`.
9. Verify: top-level section list in both files matches the canonical skeleton. If a section was added or removed, revert that change.

# Output

- Write `README.md` (patched in place).
- Write `README_zh.md` (patched in place, bilingual mirror).
- Do **not** write any other files.
- Do **not** write to `docs/generated/candidates/`.

# Constraints

- Use only repository-relative paths in any new content. Never use absolute paths.
- Ground all claims in `reference_context.json` or source. Do not invent CLI flags, config keys, or file paths.
- Mark uncertain claims with `<!-- TODO: verify -->`.
- Do NOT apply `docgen-tutorial-update` or `docgen-overview-writer` logic here. Those skills are for book chapters.

# Failure Modes

- Adding a new `## ...` section because the change "deserves" one — this breaks skeleton integrity.
- Rewriting existing sections "for clarity" when only a small fact changed — this pollutes history and confuses review.
- Patching `README.md` but forgetting `README_zh.md` — bilingual contract violation.
- Translating section headings into Chinese — breaks section identity across files.
- Copying a full tutorial narrative into a README section — README is an entry point, not a tutorial.

# Dependencies

- Upstream: `docgen-change-impact` (scope decision) and `change_facts.json`.
- Downstream: `docgen-fact-check` (verify paths/links), `docgen-reviewer` (README-specific review).
- Authority: `docs/documentation-collaboration-style.md` for prose style within sections.
