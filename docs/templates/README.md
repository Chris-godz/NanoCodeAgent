# Documentation Templates

This directory contains standardized templates for documentation generation. The `docgen-tutorial-update` skill selects the appropriate template based on the type of documentation being produced.

## Available Templates

| Template | Use Case |
|----------|----------|
| `docs/templates/tutorial_template.md` | Step-by-step learning guides for newcomers |
| `docs/templates/feature_update_template.md` | Documenting feature changes for existing users |
| `docs/templates/release_note_template.md` | Structured release notes |
| `docs/templates/troubleshooting_template.md` | Problem/solution pairs for common issues |
| `docs/templates/diagram-gallery.md` | Mermaid diagram reference gallery with good/anti-pattern examples |
| `docs/templates/diagram-spec-template.md` | Diagram intent/spec template before writing Mermaid |

## Usage

1. The `docgen-tutorial-update` skill reads the relevant template.
2. When a book chapter includes Mermaid diagrams, first draft a diagram spec from `docs/templates/diagram-spec-template.md` and save it under the generated diagram-spec artifacts before writing Mermaid.
3. Then use `docs/templates/diagram-gallery.md` together with `docs/documentation-collaboration-style.md` to choose diagram type, judge whether one figure should be split into two, avoid anti-patterns, and write captions.
4. The model fills in each section, grounding every claim in repository evidence.
5. Mermaid-bearing docs should pass `scripts/docgen/verify_mermaid.py`, which now emits rendered SVG/PNG review artifacts plus an artifact report, before editorial review.
6. Editorial review should read both the diagram spec and the rendered screenshot artifacts before deciding whether a diagram needs rework.
7. The output passes through `docgen-fact-check` and `docgen-reviewer` before finalization.

## Guidelines

- **Do not remove template section markers** — they provide stable structure for human review and future automation.
- **Fact source priority** is embedded in each template header as a reminder.
- Templates are versioned; update the version footer when making structural changes.
- Add new templates here when a new documentation category is needed.
