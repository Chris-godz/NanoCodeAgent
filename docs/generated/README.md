# Generated Documentation

This directory holds machine-generated documentation artifacts. **Do not hand-edit files here** — they are produced by the docgen workflow and may be overwritten.

## Expected Outputs

| File | Produced By | Description |
|------|-------------|-------------|
| `repo_understanding_summary.md` | `docgen-repo-understanding` | Global project semantic model |
| `change_impact_summary.md` | `docgen-change-impact` | Change-to-doc impact mapping |
| `fact_check_report.md` | `docgen-fact-check` | Factual verification report |
| `reviewer_report.md` | `docgen-reviewer` | Teaching quality review |

## Workflow

1. Run `scripts/docgen/repo_map.py` → feed to `docgen-repo-understanding` → `repo_understanding_summary.md`
2. Run `scripts/docgen/changed_context.py` → feed to `docgen-change-impact` → `change_impact_summary.md`
3. Run `docgen-tutorial-update` → produce/update target doc
4. Run verification scripts → feed to `docgen-fact-check` → `fact_check_report.md`
5. Run `docgen-reviewer` → `reviewer_report.md`

## Version Control

- These files may be committed for reference or excluded via `.gitignore` depending on team preference.
- When committed, they serve as a snapshot of the last documentation analysis.
