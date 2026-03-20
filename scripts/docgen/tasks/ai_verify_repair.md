# Task: Verify Repair (fix blocking issues only)

You are the docgen agent that **repairs** documentation to fix **blocking** verification failures. You change only what is necessary to resolve the issues listed in the verify report. Do not rewrite the document, change style, or expand scope.

## Step 0 — Rules

1. Read the Documentation Automation Rules from AGENTS.md (section `## Documentation Automation Rules` to `## AI Customization Layout`).
2. Use only **repository-relative paths** in any content. Never use absolute paths.

## Input

- Read `docs/generated/doc_scope_decision.json`: `update_readme`, `update_book`, `book_dirs_or_chapters` — you may edit **only** these in-scope files, except that when a blocking issue is `missing_diagram_spec`, you may also create or update the corresponding artifact under `docs/generated/diagram_specs/`.
- Read `docs/generated/verify_report.json`: `blocking_issues` (and optionally `per_file` / `blocking_passed`). Each blocking issue has `type` (e.g. invalid_path, invalid_link, invalid_command), `file`, `detail`, `suggested_fix`.
- Read the **actual in-scope files** (README.md and/or the listed book/src files) to apply fixes.

## Output

1. **Edit only the in-scope files** that have blocking issues. Fix each blocking issue (wrong path → correct repo-relative path; broken link → correct link; invalid command → correct command or removal). For `missing_diagram_spec`, create the required spec artifact file under `docs/generated/diagram_specs/` for the already in-scope Mermaid document. Do **not** rewrite entire sections or optimize prose.
2. Write exactly one file: `docs/generated/verify_repair_report.json` with this structure:

```json
{
  "fixed": [
    { "file": "<repo-relative path>", "issue_type": "<string>", "detail": "<short description>", "action_taken": "<what you changed>" }
  ],
  "not_fixed": [
    { "file": "...", "issue_type": "...", "detail": "...", "reason": "..." }
  ],
  "reason_not_fixed": "<optional string if any issues were left unfixed>"
}
```

- Populate `fixed` for each blocking issue you successfully addressed; `not_fixed` for any you could not fix (with reason). If all fixed, `not_fixed` may be [] and `reason_not_fixed` may be empty.

## Constraints

- Fix **only** issues listed in `blocking_issues`. Do not fix non_blocking_issues in this task.
- Do not add new sections, change narrative flow, or expand to files outside the scope.
- Do not alter writing style or clarity; this task is technical correction only.
- For `missing_diagram_spec`, write the minimal spec artifact needed to describe the existing diagram’s single question, reader outcome, out-of-scope details, planned shape, and split decision. Do not use this task to redesign the whole chapter.
- After your edits, the next verify run should see fewer or zero blocking issues for the same files.
