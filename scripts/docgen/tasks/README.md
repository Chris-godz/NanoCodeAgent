# Docgen pipeline tasks

These files are the **task prompts** passed to the AI (e.g. `codex exec --full-auto "$(cat tasks/ai_doc_scope_decision.md)"`). Each task references the corresponding skill under `.agents/skills/` for role knowledge.

## Used by E2E (`run_docgen_e2e.sh`)

| Task | Step | Output |
|------|------|--------|
| `ai_doc_scope_decision.md` | 4 | `doc_scope_decision.json` |
| `ai_doc_restructure_or_update.md` | 6 | Edits to README.md and book/src |
| `ai_review.md` | 8 | `docgen_review_report.md` |
| *(Step 9)* | 9 | `docgen_e2e_summary.md` — produced by `render_docgen_e2e_summary.py` (deterministic, no task file). |

Target docs are **README.md** and **book/src/**\*.md only.
