---
description: "Use when writing git commit messages or reviewing commit history. Covers conventional commit types, summary line format, bullet point structure, and scope conventions for this repository."
---
# Git Commit Message Rules

When writing commit messages for this repository as an AI coding assistant, you must strictly adhere to the project's commit style:

## 1. Commit Message Structure
Commits must include a top-level summary line, a blank line, and a bulleted list of specific changes.

**Format Pattern:**
```
<type>: <subject summary>

- <type>[(<scope>)]: <detailed change 1>
- <type>[(<scope>)]: <detailed change 2>
...
```

## 2. Component Rules
- **Type**: Standard conventional commit types (`feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`).
  - **CRITICAL**: Do NOT include a `(<scope>)` in the top-level summary line. It must strictly be `<type>: <subject summary>`.
- **Subject Summary**: Written in the imperative mood, concise, lowercase, and summarizes the core contribution of the commit.
- **Bullet Points**: Every significant modification must be broken down as a separate list item.
  - Starts with `- `
  - Includes type and an optional but recommended `(<scope>)` (e.g., `feat(http):`, `test(sse):`).
  - Action-oriented descriptions (e.g., `introduce workspace_init...`, `add unit test...`).

## 3. Example Reference
```
feat: implement config precedence and workspace security sandbox

- feat(config): load configs honoring priority (Defaults < File < Env < CLI)
- feat(config): replace global AGENT_ env prefix with NCA_ and support --config ini flag
- feat(workspace): introduce workspace_init protecting agent runtime and enforcing absolute paths
- test(config): add unit tests validating config overriding priorities
- test(workspace): add unit tests securing absolute access, dot dots logic
```
