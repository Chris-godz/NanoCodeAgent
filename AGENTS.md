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

## AI Customization Layout

- `AGENTS.md`: always-on repository guidance for Codex and other compatible agents.
- `.github/copilot-instructions.md`: Copilot workspace-wide guidance.
- `.github/instructions/*.instructions.md`: file-pattern specific Copilot deltas that should not duplicate `AGENTS.md`.
- `.agents/skills/<skill-name>/SKILL.md`: optional reusable workflows only. Do not store always-on coding standards here.
- There are currently no active repository skills. Add one only when a multi-step workflow repeats enough to justify it.
