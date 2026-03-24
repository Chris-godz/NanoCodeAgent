---
name: docgen-fact-check
description: Verify generated or updated documentation against the actual repository state.
allowed-tools:
  - Bash
  - Read
  - Write
  - Edit
---

# Purpose

Validate that documentation content is factually consistent with the current repository. This skill acts as a quality gate between content generation and final review, catching hallucinations, stale references, and unsupported claims.

# When to Use

- After `docgen-tutorial-update` produces or modifies a document.
- Before merging any documentation change.
- As a periodic audit of existing documentation.

# Inputs

- The document to verify.
- Related source files, tests, examples, and configuration.
- Output of verification scripts:
  - `scripts/docgen/verify_paths.py`
  - `scripts/docgen/verify_links.py`
  - `scripts/docgen/verify_doc_consistency.py`
  - `scripts/docgen/verify_commands.py`

# Required Steps

1. Run `scripts/docgen/verify_paths.py` against the target document.
2. Run `scripts/docgen/verify_links.py` to check internal references.
3. Run `scripts/docgen/verify_doc_consistency.py` to check documented path and script references, and note any `NOT_IMPLEMENTED` categories it reports.
4. Run `scripts/docgen/verify_commands.py` for command syntax validation.
5. For each claim in the document, trace it to a source of truth using the fact source priority:
   1. Source code
   2. Configuration / CLI help
   3. Tests / examples
   4. Existing documentation
   5. Diff / commit messages
   6. Specified external docs
6. Flag any claim that cannot be traced to a source.
7. Produce a structured verification report.

# Output Format

Write the report to `docs/generated/fact_check_report.md` with:

- **Summary**: overall pass/fail and confidence level.
- **Errors**: factual inaccuracies that must be fixed (with evidence).
- **Warnings**: claims that could not be fully verified.
- **Required Fixes**: concrete list of corrections needed.
- **Optional Improvements**: suggestions for clarity or completeness.

# Failure Modes

- Passing documents that reference non-existent paths or commands.
- Accepting claims from old documentation without re-verifying against current code.
- Missing behavioral changes that make examples incorrect.
- Treating absence of contradicting evidence as confirmation.

# Validation Handoff

- All "Required Fixes" must be addressed before the document is considered complete.
- Pass the corrected document to `docgen-reviewer` for final teaching quality review.

# Dependencies

- Upstream: `docgen-tutorial-update` (the document under review).
- Downstream: `docgen-reviewer`.
- Requires Python 3 for running verification scripts.
- **Codex App/IDE note**: script execution is the same in interactive and `codex exec` modes. No IDE-specific features required.
