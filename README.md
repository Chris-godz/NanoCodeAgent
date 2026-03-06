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

It is intentionally positioned as a local, controlled runtime rather than a cloud-first black-box automation platform. The current repository focuses on building the core agent loop, tool safety boundaries, and a development workflow that can later dogfood stronger coding and documentation agents.

## Current Status

`Phase 0` is complete. The current baseline includes:

- CLI and runtime configuration
- Agent loop and tool-calling basics
- HTTP / LLM integration and SSE streaming
- Safety-bounded `read`, `write`, and `bash` tools
- Test infrastructure and deterministic mdBook validation

For documentation, the main branch now keeps only deterministic mdBook build and structure checks. AI-generated documentation is not part of the mainline workflow at this stage.

## Quick Start

```bash
git submodule update --init --recursive
./build.sh        # build in Debug mode
./build.sh test   # build and run all tests
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

- [ ] add `apply_patch` as a first-class mutation primitive
- [ ] add read-only git inspection tools such as `git_status`, `git_diff`, and `git_show`
- [ ] add repository search tools such as `rg_search` and bounded file listing
- [ ] define mutation vs read-only tool policy and approval boundaries
- [ ] add patch validation and rejection flow before writeback
- [ ] support bounded build/test execution loops for CMake and `ctest`
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

This starts after `Phase 2`, not before.

- [ ] manually run NanoCodeAgent over repo docs, code, and diff context in one workflow
- [ ] generate a documentation plan and patch as a manual agent task
- [ ] ship a `Docs Maintainer` skill instead of a CI-side doc generator
- [ ] reconsider automation only after repeated stable manual dogfooding

## Future Directions

Longer-term directions stay centered on the runtime itself:

- MCP integration
- Telegram or other remote interfaces
- finer-grained sandbox and permission models
- stronger autonomous coding workflows
