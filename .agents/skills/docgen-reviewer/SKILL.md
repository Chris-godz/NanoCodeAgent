---
name: docgen-reviewer
description: Final review pass for teaching quality and readability. Does not rewrite content.
allowed-tools: Bash, Read
---

# Purpose

Perform a final editorial review focused on teaching quality, logical flow, and reader experience. This skill does NOT rewrite content — it identifies problems and produces actionable feedback for the author or upstream skill to address.

# When to Use

- After `docgen-fact-check` has verified factual accuracy.
- As the last step before a documentation change is considered complete.

# Inputs

- The document under review (post fact-check corrections).
- Fact-check report (`docs/generated/fact_check_report.md`).
- Repository glossary (from repo understanding summary).

# Review Checklist

1. **Teaching logic**: Does the document build concepts in a logical order? Can a newcomer follow along?
2. **Prerequisites**: Are all required setup steps, dependencies, and prior knowledge stated upfront?
3. **Terminology consistency**: Does the document use the same terms as the rest of the documentation and the codebase? Flag any drift.
4. **Example quality**: Do examples clearly show input and expected output? Are they minimal and focused?
5. **Hallucination check**: Are there any confident-sounding statements without traceable evidence?
6. **Audience fit**: Is the language appropriate for the target audience? Not too advanced, not too patronizing.
7. **Completeness**: Are there obvious gaps — missing steps, unexplained concepts, or dead-end sections?
8. **Link integrity**: Do all cross-references point to valid, relevant targets?

# Output Format

Write the review to `docs/generated/reviewer_report.md` with:

- **Overall Assessment**: brief summary (1-2 sentences).
- **Must Fix**: issues that block publication.
- **Should Fix**: issues that significantly improve quality.
- **Nice to Have**: minor polish suggestions.
- **Teaching Flow Notes**: observations about the logical structure.

# Constraints

- Do NOT rewrite the document. Only identify issues and suggest fixes.
- Do NOT introduce new content or expand scope.
- Keep feedback specific and actionable — cite the exact section or line.
- Limit the report to problems, not praise.

# Failure Modes

- Rubber-stamping a document without thorough reading.
- Suggesting rewrites instead of targeted fixes (scope creep).
- Missing terminology drift because of reviewing in isolation.
- Ignoring the newcomer perspective.

# Validation Handoff

- All "Must Fix" items must be resolved before the document is finalized.
- The author or `docgen-tutorial-update` skill addresses feedback and loops back through `docgen-fact-check` if factual changes were made.

# Dependencies

- Upstream: `docgen-fact-check`.
- This is a terminal skill — no downstream skill follows.
- **Codex App/IDE note**: review is a read-only operation. Works identically in interactive and `codex exec` modes.
