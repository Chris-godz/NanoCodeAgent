---
name: docgen-reviewer
description: Final review pass for teaching quality and readability. Does not rewrite content.
allowed-tools:
  - Bash
  - Read
---

# Purpose

Perform a final editorial review focused on teaching quality, logical flow, and reader experience. This skill does NOT rewrite content — it identifies problems and produces actionable feedback for the author or upstream skill to address.

# When to Use

- After `docgen-fact-check` has verified factual accuracy.
- As the last step before a documentation change is considered complete.

# Inputs

- The document under review (post fact-check corrections).
- Fact-check report artifact from the current docgen run.
- Repository glossary (from repo understanding summary).
- If the document includes Mermaid diagrams, read `docs/documentation-collaboration-style.md` and `docs/templates/diagram-gallery.md` so the review can distinguish teaching diagrams from anti-patterns. Also read the diagram spec artifact and rendered screenshot artifacts before judging diagram quality.
- When diagrams are reviewed, cite the exact repository-relative spec path and rendered artifact path(s) you used so the E2E evidence chain can prove review consumed them.
- Do not use editorial review to rediscover Mermaid parse failures. If rendering is broken, that belongs to blocking verify.

# Review Checklist

1. **Teaching logic**: Does the document build concepts in a logical order? Can a newcomer follow along?
2. **Prerequisites**: Are all required setup steps, dependencies, and prior knowledge stated upfront?
3. **Terminology consistency**: Does the document use the same terms as the rest of the documentation and the codebase? Flag any drift.
4. **Example quality**: Do examples clearly show input and expected output? Are they minimal and focused?
5. **Hallucination check**: Are there any confident-sounding statements without traceable evidence?
6. **Audience fit**: Is the language appropriate for the target audience? Not too advanced, not too patronizing.
7. **Completeness**: Are there obvious gaps — missing steps, unexplained concepts, or dead-end sections?
8. **Link integrity**: Do all cross-references point to valid, relevant targets?
9. **Diagram teaching value**: If the doc uses Mermaid, does each figure help the reader understand structure, flow, boundary, or coverage instead of just decorating the page?
10. **Split-vs-single judgment**: If a figure feels overloaded, can you state clearly whether it should remain one diagram or be split into two, and why?
11. **Overview-as-map check**: If the chapter opens with an overview figure, does it still behave like a system map rather than an implementation dump?
12. **Visual review from screenshot**: Does the rendered figure look publishable as a formal technical-doc diagram, rather than merely renderable Mermaid source?
13. **Rubric discipline**: Did you explicitly answer the five diagram review questions: one main question, detail downshift, short scannable titles, noisy center, and caption responsibility?

# Output Format

Write the review to the review report artifact for the current docgen run with:

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
- For diagram issues, say whether the issue is **semantic quality** or **visual quality**, then say whether the fix is to change diagram type, split the figure, regroup the figure, shorten labels, move explanation out of the figure into the caption, or rewrite the caption. Avoid vague “make the diagram clearer” feedback.

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
