# Copilot Workspace Instructions

- Use [AGENTS.md](../AGENTS.md) as the primary source of repository-wide engineering guidance.
- Use `.github/instructions/*.instructions.md` only for file-type or path-specific deltas that are not already stated in `AGENTS.md`.
- Use `.agents/skills/` only for reusable multi-step workflows. Do not treat development conventions as skills.
- Keep responses and changes aligned with the existing repository structure, `build.sh` workflow, GoogleTest usage, and commit message conventions.
- Treat the commit message format in `AGENTS.md` as canonical; Copilot-targeted instruction files should only refine task targeting or scope hints, not redefine the format.
- Active repository skills for documentation automation: `docgen-repo-understanding`, `docgen-change-impact`, `docgen-tutorial-update`, `docgen-fact-check`, `docgen-reviewer`. See `.agents/skills/` for details.
