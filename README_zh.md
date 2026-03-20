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

NanoCodeAgent 是一个教学型 C++ code agent 运行时，重点放在确定性执行、workspace 安全，以及逐步演进的能力闭环。

## Why This Repo Exists

这个仓库的目标，是让 agent 行为变得可检查，而不是依赖黑盒魔法。runtime 路径把任务执行保持在本地且受限；新增的文档自动化路径则把同样的纪律应用到 README 和 mdBook 更新上：先收集事实，再明确 scope，最后验证改动是否站得住。

## Big Picture

仓库现在有两条互补主线。runtime 主线把一个 prompt 变成本地、受限的执行循环，强调 tool policy、workspace 边界和 fail-fast。documentation 主线把代码 diff 变成 change facts、scope decision、带证据的文档更新，以及面向 `README.md`、`README_zh.md` 和 `book/src/` 的阻塞校验与 review evidence。

## Main Flows

对 coding 工作来说，任务先经过 CLI 和配置装配，再进入 agent loop 与 LLM bridge，只有通过策略和 workspace 检查后才会触达具体 executor。对文档工作来说，`scripts/docgen/change_facts.py` 先抽取变化事实，AI scope decision 限定允许编辑的目标，`scripts/docgen/reference_context.py` 收集写作证据，writer 原地更新获批文档，随后 verify/review 闭环检查路径、链接、diagram spec、Mermaid 渲染，以及审稿反馈。

## Current Status

`Phase 0` 已完成，当前基线包括：

- CLI 与运行时配置
- agent loop 与基础 tool-calling
- HTTP / LLM 集成与 SSE 流式解析
- 安全受限的 `read`、`write`、`bash` 工具
- 测试基础设施与确定性的 mdBook 校验
- 面向 README 和书籍章节的 change-aware 文档自动化，包括 scope decision、reference context、blocking verify，以及 review/rework evidence

## Documentation Automation

文档自动化沿用了与 runtime 相同的 local-first、bounded 思路。writer 阶段只允许修改获批的主页和书籍目标文档；生成出来的 JSON、diagram spec、渲染图与 run evidence 都留在 `docs/generated/` 下，作为阶段交接和 review 证据，而不是最终用户文档。

### What You Usually Do

先运行 `bash scripts/docgen/setup.sh`，确认 `python3`、`git`、`npm` 和 Python venv 支持都可用。只想判断是否需要更新文档时，用 `bash scripts/docgen/run_change_impact.sh`。想跑完整的 change-facts -> scope -> reference-context -> doc-update -> verify/review 闭环时，用 `bash scripts/docgen/run_docgen_e2e_closed.sh`。这条闭环还要求 `codex` CLI 已在 `PATH` 中。在 GitHub Actions 里，`.github/workflows/docs-validate.yml` 会在 PR 上复用同样的 setup 与 verify 路径，而 `.github/workflows/core-ci.yml` 则在 build/test 之后，通过 `workflow_dispatch`（job `full-doc-automation`）和 `OPENAI_API_KEY` 暴露同一条 closed-loop 入口。

### Boundaries And Pitfalls

真实文档目标仍然很窄：`README.md`、`README_zh.md`，以及 `book/src/` 下的 Markdown。`docs/generated/` 保存的是 scope decision、verify report、diagram spec、渲染出来的 SVG/PNG，以及 run evidence，它们不是最终文档。当前 blocking verify 覆盖路径、链接、diagram spec 和 Mermaid 渲染正确性；命令检查仍然更像辅助信号，而不是合并闸门。如果你在书中新增或调整 Mermaid 块，还要同步更新 `tests/fixtures/ci_diagram_specs/`，否则 PR 校验所用的 spec 镜像会和正文脱节。

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

当前已完成的运行时基线：

- [x] 仓库脚手架、CMake 构建和 CLI 入口
- [x] 配置优先级与 workspace sandbox
- [x] HTTP client 与基础 LLM 请求链路
- [x] SSE parser 与流式响应处理
- [x] tool registry 与 `tool_call` 聚合
- [x] 安全的 workspace `write` 工具
- [x] 安全的 workspace `read` 工具
- [x] 带超时和进程控制的受限 `bash` 执行
- [x] 带限制器的多轮 agent loop
- [x] mock/live 路径测试加固与流式鲁棒性完善

### Phase 1

目标：补齐一个更完整的仓库内 coding workflow 闭环。

- [x] 把 `apply_patch` 做成一等变更原语
- [x] 增加 `git_status`，返回结构化的分支、ahead/behind 与逐文件变更状态
- [x] 增加 `git_diff` 和 `git_show`，用于只读 diff 与历史查看
- [x] 增加仓库搜索工具 `rg_search` 与受限目录枚举 `list_files_bounded`
- [x] 引入 `ToolRegistry` 类型化分发，并附带 `ToolCategory` 与 `requires_approval` 元数据
- [x] 在运行时真正落地只读工具与修改型工具的权限/确认边界
- [x] 增加 patch 校验与拒绝回退流程
- [x] 支持围绕 CMake 与 `ctest` 的受限 build/test 循环
- [ ] 强化失败恢复与基于 tool result 的重试提示
- [ ] 增加显式的提交封装流程，如 `git_add` 与 `git_commit`

### Phase 2

目标：把可复用工作流解耦为 Skills。

- [ ] 定义 skill 目录约定与 metadata/frontmatter 结构
- [ ] 实现 workspace 与共享位置的 skill loader
- [ ] 把选中的 skill 注入 prompt / session 装配流程
- [ ] 内置 C++ 规范、测试流程与 git 工作流等基础 skills
- [ ] 通过 CLI 与 config 暴露 skill 开关
- [ ] 增加 skill 执行 trace 与调试输出
- [ ] 用 skills 真正跑通一个仓库维护型 dogfood workflow

### Docs Agent Milestone

这条路线遵循：local validation first -> change-aware updates -> stronger verification and review -> CI readiness progression。

**当前重心：** CI-ready + GitHub Actions — 在 Actions 上复现与本地相同的 docgen 入口（完整 closed-loop 用 `workflow_dispatch` 手动跑，不是降级版流程）。

- [x] Milestone 1：完成本地优先的 docgen scaffold，包括规则、仓库内 skills、确定性脚本、生成物与基础校验
- [x] Milestone 2：把代码变更映射到文档影响，并判断更新是必需、可选还是不需要
- [x] Milestone 3：把 change-impact analysis 接到 README、教程与 getting-started 文档的增量更新流程里
- [x] Book rewrite wave 1（mdBook 教学向改写）：当前规划下视为已完成，不再作为主战场
- [ ] Milestone 4：增强 commands、paths、configs、environment references 等 doc-to-repo 一致性校验 — **延后 / 低优先级**（不与 CI-ready 抢优先级）
- [x] Milestone 5：增加聚焦清晰度、教学质量、结构与完整性的 review 阶段，再接受生成文档
- [x] Milestone 6：CI-ready：`core-ci`、文档校验、以及在 GitHub Actions 上手动跑完整 `run_docgen_e2e_closed.sh`（见 `.github/workflows/docs-validate.yml` 与 `.github/workflows/core-ci.yml` 的 `workflow_dispatch`）

## Future Directions

更远期方向仍然围绕 runtime 本身展开：

- MCP 集成
- Telegram 或其他远程入口
- 更细粒度的 sandbox / permission 模型
- 更强的自主 coding workflow
