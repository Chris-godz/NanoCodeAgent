# Task: Diagram Spec Backfill

You are the docgen agent responsible for filling in **missing diagram spec artifacts only** after the main writer stage. Do not rewrite README or book chapters here. Your job is to inspect the already in-scope Mermaid-bearing docs and create the missing `docs/generated/diagram_specs/...` files that the next verify step expects.

## Step 0 — Rules

1. Read the Documentation Automation Rules from AGENTS.md (section `## Documentation Automation Rules` to `## AI Customization Layout`).
2. Read `docs/documentation-collaboration-style.md`, `docs/templates/diagram-gallery.md`, and `docs/templates/diagram-spec-template.md`.
3. Read `.agents/skills/docgen-tutorial-update/SKILL.md` and `.agents/skills/docgen-overview-writer/SKILL.md`.

## Input

- Read `docs/generated/doc_scope_decision.json` to determine the approved target docs.
- Read `docs/generated/reference_context.json` for supporting evidence.
- Read `docs/generated/missing_diagram_specs.json` for the exact missing spec paths and block indices.
- Read the corresponding in-scope chapter(s) so the spec reflects the current figure and nearby prose.

## Output

- For each entry in `docs/generated/missing_diagram_specs.json`, create exactly the missing spec file under `docs/generated/diagram_specs/...`.
- Each spec must at least cover:
  - target doc
  - diagram block index
  - diagram type
  - single question
  - reader outcome
  - out-of-scope details
  - planned shape / participants
  - split decision
  - visual guardrails

## Constraints

- Do not modify README, README_zh.md, or book chapters in this task.
- Do not create extra files outside the missing `docs/generated/diagram_specs/...` targets listed in `docs/generated/missing_diagram_specs.json`.
- Keep the spec aligned to the figure that already exists in the chapter. If the figure itself is wrong, that belongs to later review/rework; this task is only to supply the missing review contract.
