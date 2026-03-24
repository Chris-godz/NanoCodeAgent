---
name: docgen-overview-writer
description: Guide how to write documentation for quick project understanding; expression order and priorities, not scope or target selection.
allowed-tools:
  - Bash
  - Read
  - Write
  - Edit
---

# Purpose

This skill defines **how to write** documentation so readers can quickly understand the project. It does **not** decide which docs to update or what scope to apply — that is handled by docgen-tutorial-update and scope decision. It governs **expression order and priorities** so that each document builds a mental model before dumping details.

# When to Use

- Whenever you are writing or updating README sections or book chapters as part of the docgen flow.
- Use **together with** docgen-tutorial-update: tutorial-update decides *what* to change and *where*; overview-writer dictates *how* to structure and order content inside the document.

# Authority

Read and follow **docs/documentation-collaboration-style.md** for the full specification. This skill summarizes the writing rules and layers.

If you add or revise Mermaid diagrams, also consult **docs/templates/diagram-gallery.md** for reusable good/anti-pattern examples and **docs/templates/diagram-spec-template.md** for the pre-Mermaid spec stage.

# Expression Order (Overview Layer First)

Write in this order. Do not lead with commands or script names.

1. **One-line summary** — What this document covers and what problem it addresses.
2. **Why** — Why this capability/flow/design exists; why the reader should care.
3. **Big Picture** — Overall shape: main phases, main components, data/control flow. Use a short paragraph or a diagram (e.g. Mermaid); avoid long lists of scripts here.
4. **Main Flow** — From the reader’s perspective: what they do first, then next, then what they get. Narrative, not a bullet list of commands.
5. **Module Roles** — Who is responsible for which decision or output. Organize by responsibility, not by script filename.
6. **What You Usually Do** — The most common entry points and a simple decision guide (e.g. “only check impact → run X; update docs → run Y; verify → run Z”). State **prerequisites** (Codex, python3, git, etc.) before or in this section.
7. **Boundaries / Pitfalls** — What is in/out of scope, blocking vs non-blocking, common mistakes.
8. **Dive Deeper** — Links to script READMEs, flow docs, tests, source. Do not overwhelm the opening of the document; keep “continue here” at the end.

# Overview vs Detail Layer

- **Overview layer**: Summary, Why, Big Picture, Main Flow, Module Roles, What You Usually Do. Goal: reader can answer “what is this and how do I use it in the normal case?”
- **Detail layer**: Boundaries/Pitfalls, Dive Deeper, and any necessary command/path/artifact references. Goal: reader who needs precision can find it without drowning in it at the start.

Default to serving “understand first”; support “look up later” via clear sections and links.

# Do Not

- Do not open with a list of commands or scripts.
- Do not make script names or artifact paths the main subject of the prose.
- Do not use a long product/artifact list as the backbone of the document; use narrative and flow first, then reference specifics.
- Do not assume the reader already has a mental model; build it in the first half of the document.

# Diagram Writing Checklist

Apply this when the document includes Mermaid diagrams:

1. **Write or refresh a diagram spec before reviewable Mermaid work** — save it under `docs/generated/diagram_specs/` using a slugged doc-path directory, for example `docs/generated/diagram_specs/book__src__01-overview/block-01.md`. The spec must state the single question, reader outcome, explicit omissions, planned blocks/participants, and why the figure stays single or gets split. If an in-scope chapter keeps an existing key figure, refresh its spec so review still has an auditable contract.
2. **Use a diagram only when it adds understanding** — structure, main flow, sequence, boundary, or risk coverage. Do not add a diagram for a short list or a simple fact.
3. **Choose one primary diagram type**:
   - Architecture / overview: overall structure and major module relations
   - Sequence / streaming: temporal order and message flow
   - Boundary / approval: allow-block-execute-stop gates
   - Coverage / testing: risk surface to contract to representative test cluster
   - For chapter-opening overview figures, bias toward map semantics: layers, major responsibilities, and reading order before implementation internals.
4. **Keep one diagram, one idea** — if the figure needs to explain both module structure and timing, split it.
5. **Do not treat node count as a hard cap** — more nodes are acceptable if the figure still has one meaning, one stable reading direction, low crossing, clear grouping, and short consistent labels.
6. **Split when clarity breaks** — if the figure starts mixing structure with timing, main flow with edge cases, or needs a caption that explains “how to read the sub-parts,” split it instead of compressing harder.
7. **Use explicit split strategies when needed**:
   - Overview too heavy -> structure diagram + task-flow diagram
   - Streaming too heavy -> transport/parse diagram + tool-call assembly diagram
   - Tools/safety too heavy -> control-chain diagram + risk-layer diagram
   - Testing too heavy -> risk-map diagram + contract/test-cluster diagram
8. **Prefer reader-facing node names** — responsibilities and stages before internal symbol names; keep titles short, noun-like, and scannable.
9. **Keep a single reading direction** — default to `LR` or `TD`; avoid back-and-forth arrows unless the sequence itself requires it.
10. **Prefer grouping over dense wiring** — use subgraphs or staged lanes before adding more arrows.
11. **Move explanation into the caption** — do not hang long explanatory sentences on connectors or pack them into node labels.
12. **Write a caption after every diagram** — explain what the figure shows, what it intentionally omits, and how the reader should interpret it.

# Common Diagram Failure Modes

- Mixing architecture, sequence, and boundary semantics in one flowchart.
- Keeping one figure after it already needs to be explained as “read this half first, then the other half.”
- Letting the first overview figure absorb implementation details that belong in later chapter diagrams.
- Packing the center of the figure with noise nodes, remarks, or implementation-heavy labels so the map loses its visual anchor.
- Mechanically splitting a diagram that is already single-purpose and visually stable.
- Turning a testing figure into a raw list of test filenames.
- Using long implementation-heavy node labels that bury the structure.
- Relying on color instead of structure or caption to convey meaning.
- Repeating the paragraph in diagram form without adding a clearer mental model.

# Constraints

- All claims and paths must still be grounded in reference context and repo state (per Documentation Automation Rules in AGENTS.md).
- Use only repository-relative paths in output.
- This skill does not replace docgen-tutorial-update; it constrains **how** content is ordered and emphasized when that skill applies scope and writes README/book.

# Dependencies

- **Upstream**: docgen-tutorial-update (for scope and target docs); reference_context.json and doc_scope_decision.json.
- **Authority**: docs/documentation-collaboration-style.md.
