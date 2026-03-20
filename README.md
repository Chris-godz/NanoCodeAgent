<div align="center">

# NanoCodeAgent

[English](README.md) | [简体中文](README_zh.md)

![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)
![CMake 3.20+](https://img.shields.io/badge/CMake-3.20%2B-064F8C?style=flat-square&logo=cmake&logoColor=white)
![libcurl](https://img.shields.io/badge/libcurl-networking-073551?style=flat-square)
![nlohmann/json](https://img.shields.io/badge/nlohmann-json-586069?style=flat-square)
![GoogleTest](https://img.shields.io/badge/GoogleTest-unit%20tests-34A853?style=flat-square)
![mdBook](https://img.shields.io/badge/mdBook-docs-000000?style=flat-square)

</div>

NanoCodeAgent is a teaching-oriented C++ code agent runtime focused on deterministic execution, workspace safety, and incremental capability building.

## Why This Repo Exists

This repository exists to make agent behavior inspectable instead of magical. The runtime keeps task execution local and bounded, while the newer documentation automation path applies the same discipline to README and mdBook updates: collect facts first, decide scope explicitly, then verify what changed.

## Big Picture

The repository now has two complementary tracks. The runtime track turns a prompt into a bounded local execution loop with explicit tool policy, workspace limits, and fail-fast behavior. The documentation track turns a code diff into change facts, scope decisions, grounded doc updates, blocking verification, and review evidence for `README.md`, `README_zh.md`, and `book/src/`.

## Main Flows

For coding work, a task enters through CLI and config setup, flows through the agent loop and LLM bridge, and only reaches tool executors after policy and workspace checks. For documentation work, `scripts/docgen/change_facts.py` captures what changed, AI scope decision limits what may be edited, `scripts/docgen/reference_context.py` gathers evidence, the writer updates approved docs in place, and the verify/review loop checks paths, links, diagram specs, Mermaid rendering, and review feedback before a summary is rendered.

## Current Status

`Phase 0` is complete. The current baseline includes:

- CLI and runtime configuration
- Agent loop and tool-calling basics
- HTTP / LLM integration and SSE streaming
- Safety-bounded `read`, `write`, and `bash` tools
- Test infrastructure and deterministic mdBook validation
- Change-aware documentation automation for README and book chapters, including scope decisions, reference context, blocking verify, and review/rework evidence

## Documentation Automation

Documentation automation follows the same local-first, bounded pattern as the runtime. The writer stage only edits approved homepage and book targets, while generated JSON, diagram specs, and rendered diagram artifacts stay under `docs/generated/` as handoff and review evidence.

### What You Usually Do

Run `bash scripts/docgen/setup.sh` first to confirm `python3`, `git`, `npm`, and Python venv support are present. Use `bash scripts/docgen/run_change_impact.sh` when you only need an impact report. Use `bash scripts/docgen/run_docgen_e2e_closed.sh` when you want the full change-facts -> scope -> reference-context -> doc-update -> verify/review loop. That closed loop also requires the Codex CLI in `PATH`. In GitHub Actions, `.github/workflows/docs-validate.yml` reuses the same setup and verify path for PRs, while `.github/workflows/core-ci.yml` exposes the same closed-loop entrypoint (job `full-doc-automation`) behind `workflow_dispatch` after build/test, with `OPENAI_API_KEY`.

### Boundaries And Pitfalls

The target docs remain narrow: `README.md`, `README_zh.md`, and Markdown under `book/src/`. `docs/generated/` contains scope decisions, verify reports, diagram specs, rendered SVG/PNG artifacts, and run evidence, but those are not destination docs. Blocking verification currently covers paths, links, diagram specs, and Mermaid render correctness; command checks still act as supporting signals rather than merge blockers. If you add or reorder Mermaid blocks in the book, update `tests/fixtures/ci_diagram_specs/` so PR verification still mirrors the expected spec layout.

### Dive Deeper

- [book/src/01-overview.md](book/src/01-overview.md)
- [book/src/06-documentation-automation.md](book/src/06-documentation-automation.md)
- [scripts/docgen/README.md](scripts/docgen/README.md)
- [scripts/docgen/tasks/README.md](scripts/docgen/tasks/README.md)
- [docs/docgen_e2e_flow.md](docs/docgen_e2e_flow.md)

## Quick Start

```bash
git submodule update --init --recursive
./build.sh
./build.sh test
```

## Roadmap

### Phase 0

Completed runtime baseline:

- [x] repository scaffold, CMake build, and CLI entrypoint
- [x] config precedence and workspace sandbox
- [x] HTTP client and basic LLM request plumbing
- [x] SSE parser and streaming response handling
- [x] tool registry and `tool_call` aggregation
- [x] secure workspace `write` tool
- [x] secure workspace `read` tool
- [x] bounded `bash` execution with timeout and process control
- [x] multi-turn agent loop with loop limits
- [x] mock/live-path test hardening and streaming robustness

### Phase 1

Goal: complete a stronger coding workflow loop inside a repository.

- [x] add `apply_patch` as a first-class mutation primitive
- [x] add `git_status` with structured branch, ahead/behind, and per-file change listing
- [x] add `git_diff` and `git_show` for read-only diff and commit history inspection
- [x] add `rg_search` and bounded `list_files_bounded` for repository search
- [x] introduce `ToolRegistry` typed dispatch with `ToolCategory` and `requires_approval` metadata
- [x] enforce mutation vs read-only tool policy and approval gate at runtime
- [x] add patch validation and rejection flow before writeback
- [x] support bounded build/test execution loops for CMake and `ctest`
- [ ] improve failure recovery and retry guidance from tool results
- [ ] add explicit commit packaging flow with `git_add` and `git_commit`

### Phase 2

Goal: decouple reusable workflows into Skills.

- [ ] define the skill directory convention and metadata/frontmatter shape
- [ ] implement a skill loader for workspace and shared skill locations
- [ ] inject selected skills into prompt/session assembly
- [ ] add built-in skills for C++ conventions, testing workflow, and git workflow
- [ ] expose skill enablement through CLI and config
- [ ] add skill execution traces and debugging output
- [ ] dogfood one real repository maintenance workflow through skills

### Docs Agent Milestone

This track follows a local validation first -> change-aware updates -> stronger verification and review -> CI readiness progression.

**Current focus:** CI-ready + GitHub Actions — reproduce the same docgen entrypoints in Actions as locally (full closed loop is manual `workflow_dispatch`, not a second-class pipeline).

- [x] Milestone 1: build the local-first docgen scaffold with rules, repo-scoped skills, deterministic scripts, generated outputs, and basic verification
- [x] Milestone 2: map code changes to documentation impact and classify whether updates are required, optional, or unnecessary
- [x] Milestone 3: connect change-impact analysis to incremental updates for README, tutorials, and getting-started documentation
- [x] Book rewrite wave 1 (mdBook teaching pass): treated complete for current planning; not the active backlog item
- [ ] Milestone 4: strengthen verification for commands, paths, configs, environment references, and other doc-to-repo consistency checks — **deferred / low priority** (do not block CI-ready)
- [x] Milestone 5: add a review stage focused on clarity, teaching quality, structure, and completeness before accepting generated docs
- [x] Milestone 6: CI-ready: `core-ci` + docs validation + manual full `run_docgen_e2e_closed.sh` on GitHub Actions (see `.github/workflows/docs-validate.yml` and `.github/workflows/core-ci.yml` `workflow_dispatch`)

## Future Directions

Longer-term directions stay centered on the runtime itself:

- MCP integration
- Telegram or other remote interfaces
- finer-grained sandbox and permission models
- stronger autonomous coding workflows
