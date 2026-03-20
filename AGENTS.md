# Repository Guidelines

## Working Scope

- Treat this file as the repository-wide source of truth for always-on engineering guidance.
- Use nested guidance only when a deeper directory introduces a more specific rule.
- Keep reusable task workflows in `.agents/skills/`; keep file-type specific rules in `.github/instructions/`.

## Build And Test

- Use `./build.sh` for all build and test work. Do not replace it with ad hoc CMake command sequences.
- Run `./build.sh test` after behavior changes unless the task explicitly prevents it.
- Keep AddressSanitizer-compatible behavior intact; do not introduce leaks or sanitizer regressions.

## C++ Rules

- Use C++23 and preserve the existing project style.
- Add or update GoogleTest coverage before or alongside implementation changes.
- Put tests under `tests/`, mirroring the production source layout.
- Use `spdlog` through the wrappers in `include/logger.hpp`; avoid `std::cout` and `printf` for business logic.
- Keep third-party dependencies under `3rd-party/` and wire them through existing submodule and CMake patterns.

## Change Discipline

- Prefer small, focused patches over broad refactors.
- Fix the root cause when practical, but avoid unrelated cleanup.
- Preserve existing user changes unless the task explicitly asks to replace them.

## Commit Expectations

- Follow the repository commit format when preparing commit messages.
- Use a top-level conventional summary line without a scope, formatted as `<type>: <subject summary>`.
- Keep the subject summary imperative, concise, and lowercase.
- Follow the summary with a blank line and bullet items for the concrete changes.
- Prefix each bullet item as `- <type>[(<scope>)]: <detail>`.
- Break significant modifications into separate bullet items, and use bullet scopes when they clarify the touched subsystem.

## Documentation Automation Rules

### Goals

- Keep docs aligned with the current repository state.
- Prefer factual correctness over polished but unsupported claims.
- Optimize for teaching clarity, not just changelog completeness.

### Must Do Before Editing Docs

- Read relevant docs, examples, and affected source files.
- Run repository understanding flow when the task is large or cross-cutting.
- Run fact-check scripts before finalizing.

### Do Not

- Do not infer CLI flags, config keys, or file paths without verifying them in the repo.
- Do not describe behavior that is not present in the repo.
- Do not rewrite unrelated sections unless necessary for coherence.
- Do not make definitive statements without code or script evidence.

### Output Quality Bar

- Explain what changed and why it matters.
- Keep terminology consistent with existing docs unless the old term is wrong.
- Include pitfalls and prerequisites when relevant.
- Prefer concrete examples over vague summaries.

### Fact Source Priority

1. Current repository source code.
2. Current repository configuration and CLI help output.
3. Current repository tests and examples.
4. Current repository existing documentation.
5. Current change diff and commit messages.
6. Explicitly specified external official documentation.

If source code conflicts with old documentation, source code wins.

### Validation

- Run `scripts/docgen/verify_*.py` scripts before considering a doc task complete.
- If validation fails, fix the doc before marking the task done.

### Rule Reading Convention

- When any docgen skill or script needs the documentation automation rules from this file, read **only** the targeted section using: `sed -n '/## Documentation Automation Rules/,/## AI Customization Layout/p' AGENTS.md`
- Do NOT use broad line-range reads like `sed -n '1,260p' AGENTS.md` or `head -n 300 AGENTS.md` — these pull in unrelated C++ and build rules and may miss the doc automation section entirely.
- If the `## Documentation Automation Rules` heading is not found, **fail explicitly** with a clear error message. Do not silently fall back to reading generic engineering rules.
- Generated documentation artifacts must use **repository-relative paths** (e.g., `src/main.cpp`, `include/agent_loop.hpp`). Never emit absolute paths like `/home/...` in any output under `docs/generated/`.

### Writing style and clarity

- Writing and review must follow the **documentation collaboration style** defined in [docs/documentation-collaboration-style.md](docs/documentation-collaboration-style.md).
- When **writing** target docs (README, book): use the expression order and priorities from the overview-writer layer (Why → Big Picture → Main Flow → Module Roles → What you usually do → Boundaries → Dive deeper). Do not lead with commands or script names; build the reader’s mental model first.
- When **reviewing**: besides factual and structural checks, assess **understanding effect** per that document (map before details, narrative over inventory, design intent explained, skimmable with dive-deeper entry). Use the doc-clarity-reviewer criteria; do not reduce review to format or spelling only.

### Responsibility Split

- **Scripts** (`scripts/docgen/`): deterministic work — scan directories, check links, verify paths, extract CLI help.
- **Skills** (`.agents/skills/docgen-*/`, `doc-clarity-reviewer`): workflow orchestration and writing style — when to run repo understanding, how to assess change impact, how to structure a tutorial update, how to review; **docgen-overview-writer** for expression order and priorities, **doc-clarity-reviewer** for clarity and understanding-effect review.
- **Model**: understanding, synthesis, organization, and writing.

## AI Customization Layout

- `AGENTS.md`: always-on repository guidance for Codex and other compatible agents.
- `.github/copilot-instructions.md`: Copilot workspace-wide guidance.
- `.github/instructions/*.instructions.md`: file-pattern specific Copilot deltas that should not duplicate `AGENTS.md`.
- `.agents/skills/<skill-name>/SKILL.md`: optional reusable workflows only. Do not store always-on coding standards here.
- Active repository skills: `docgen-repo-understanding`, `docgen-change-impact`, `docgen-tutorial-update`, `docgen-fact-check`, `docgen-reviewer`, `docgen-overview-writer`, `doc-clarity-reviewer`.
