---
name: doc-clarity-reviewer
description: Review documentation for human understanding effect; not format or fact-check only.
allowed-tools:
  - Bash
  - Read
---

# Purpose

Review documentation for **whether readers can quickly understand the project**. This skill does **not** replace fact-check or path/link verification (those are script and fact-check responsibilities). It focuses on: big picture before details, narrative over inventory, design intent explained, and skimmable structure with clear “dive deeper” entry points.

# When to Use

- As part of the docgen review step (e.g. `ai_review` task). The review report should include a **Clarity / 理解效果** dimension that applies this skill’s criteria.
- Use **together with** docgen-reviewer: docgen-reviewer covers teaching logic, prerequisites, terminology, completeness; doc-clarity-reviewer adds the “overview-first, map-before-details” and “not a command/script list” checks.

# Authority

Read **docs/documentation-collaboration-style.md**, especially the section on **审稿判断标准（Clarity Reviewer）**. This skill operationalizes those criteria.

If the target doc contains Mermaid diagrams, also apply the **Diagram Reviewer SOP** and compare against **docs/templates/diagram-gallery.md** for expected diagram shape and anti-patterns. Read the diagram spec artifact and the rendered screenshot artifacts before judging the figure, and cite those repository-relative evidence paths in the review output.

Render correctness is **not** a readability review responsibility. Mermaid syntax errors, fence errors, and render failures belong to blocking verify. Semantic quality and visual quality are readability review responsibilities once render passes.

# Clarity Checklist

When reviewing, explicitly assess:

1. **Big picture first** — Can the reader quickly grasp the system/flow’s goal and overall shape from the first part of the document?
2. **Map before details** — Is there a clear summary, Why, Big Picture, and Main Flow before commands, paths, or artifact lists appear?
3. **Commands not before understanding** — If commands or script names appear before the reader knows what the system does, flag it. Prerequisites should be stated before the main “how to run” section.
4. **Narrative over inventory** — Does it read like a “help me understand” narrative, or like a maintainer memo / script list? If the latter, suggest strengthening motivation and flow.
5. **Module roles clear** — Can the reader answer “who is responsible for which decision or output”?
6. **Design intent explained** — Are key boundaries (e.g. why only README + book, why Agent does scope decision) briefly explained?
7. **Dive deeper entry** — Is there an explicit “further reading” or “Dive Deeper” section with links?
8. **Information that doesn’t help understanding** — Is any passage factually correct but neither building a mental model nor explaining motivation? If so, suggest rewriting to establish context before detail.

# Diagram Review Checklist

When a book chapter includes Mermaid diagrams, explicitly assess:

1. **Right diagram type** — Is the figure acting as architecture, sequence, boundary, or coverage? If the diagram type is wrong for the teaching goal, flag it.
2. **One diagram, one idea** — Does the figure explain one primary meaning, or does it mix timing, structure, and boundaries in one flowchart?
3. **Readable main path** — Can a reader follow the primary direction without tracing crossing arrows?
4. **Reader-facing labels** — Are node names short and responsibility-oriented instead of implementation-heavy?
5. **Caption quality** — Does the caption say what the figure shows, what it omits, and how the reader should read it?
6. **Diagram-text consistency** — Does the surrounding prose describe the same boundary, flow, or coverage claim shown in the diagram?
7. **Understanding gain** — Does the diagram materially improve understanding, or is it just a paragraph redrawn as boxes?
8. **Should it be split?** — Does the figure now combine multiple primary questions, unstable reading order, obvious crossing, or caption-level multi-part explanation such that two diagrams would be clearer?
9. **Visual quality from screenshot** — Does the rendered figure look like a formal document figure: stable visual center, short scannable labels, low clutter, and no explanation-heavy lines?
10. **Rubric answers must be explicit** — The review should directly answer: one main question? detail downshifted? short scannable titles? noisy visual center? caption doing explanatory work?

# Diagram Issue Classification

- **Pipeline / blocking verify**:
  - Mermaid syntax error
  - Mermaid fence error
  - Diagram cannot be rendered by the Mermaid verifier
- **Must fix**:
  - Wrong diagram type causing semantic confusion
  - Severe semantic mixing in one diagram
  - Diagram and prose contradict each other
  - Diagram is misleading enough that removing it would be safer than keeping it
  - The figure clearly should be split, but keeping it as one diagram now harms the main teaching point
  - An overview figure has stopped acting like a map and now front-loads implementation detail that should live in later chapter diagrams
  - Screenshot shows severe clutter, unstable visual focus, or explanation-heavy labels/edges that block quick scanning
- **Should fix**:
  - Node count is acceptable in theory, but current clarity is degrading
  - Weak grouping or cluttered wiring
  - Labels are too implementation-specific
  - Caption missing or too vague to define the boundary
  - The figure likely needs splitting into two diagrams, even if the current version is still barely readable
  - Screenshot shows moderate crowding, too many containers, or labels that are just a bit too long

# Do Not

- Do not reduce this review to format, spelling, or link checks; those are handled by verify scripts and fact-check.
- Do not only check factual accuracy; clarity review is about **effect on human understanding**.
- Do not rewrite the document; only identify issues and suggest fixes (same as docgen-reviewer).

# Output

- Findings should be included in the same review report as the rest of docgen (e.g. `docs/generated/docgen_review_report.md`).
- The report must include a **Clarity / 理解效果** (or equivalent) section that addresses the checklist above: e.g. “Does the doc give a map first?”, “Is it command-list heavy?”, “Is design intent explained?”, “Is it skimmable with a clear dive-deeper?”.
- If diagrams are reviewed, the clarity section should explicitly note whether each key diagram is the right type, whether it adds understanding, whether it should stay as one figure or be split, whether the rendered screenshot passes a visual-quality check, how the five rubric questions were answered, and whether each finding is **semantic quality** or **visual quality** plus **must_fix** or **should_fix** under the Diagram Reviewer SOP.
- Cite specific sections or lines when flagging issues.

# Dependencies

- **Upstream**: Updated target docs (README, book/src); optionally verify_report.json and doc_scope_decision.json for context.
- **Authority**: docs/documentation-collaboration-style.md (reviewer criteria section).
