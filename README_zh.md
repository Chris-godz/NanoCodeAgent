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

NanoCodeAgent 是一个面向工程实践的教学型 C++ Code Agent 运行时，重点放在确定性执行、安全边界，以及逐步演进的 agent 能力闭环。

它当前被刻意设计为一个本地、受控、偏 C/C++ 风格的 runtime，而不是一个黑盒自动化平台。这个仓库的主线目标，是先把 agent loop、工具安全边界和后续可自举的 coding / docs workflow 打扎实。

## 当前状态

`Phase 0` 已完成，当前基线能力包括：

- CLI 与运行时配置
- agent loop 与基础 tool-calling
- HTTP / LLM 集成与 SSE 流式解析
- 安全受限的 `read`、`write`、`bash` 工具
- 测试基础设施与确定性的 mdBook 校验

文档方面，当前主线只保留确定性的 mdBook 构建与基础结构检查。AI 自动生成文档暂时不作为仓库主流程的一部分。

## 快速开始

```bash
git submodule update --init --recursive
./build.sh        # 以 Debug 模式构建
./build.sh test   # 构建并运行全部测试
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
- [x] mock / live 路径测试加固与流式鲁棒性完善

### Phase 1

目标：补齐一个更完整的仓库内 coding workflow 闭环。

- [x] 把 `apply_patch` 做成一等变更原语
- [x] 增加 `git_status`，返回结构化的分支、ahead/behind 与逐文件变更状态
- [x] 增加 `git_diff` 和 `git_show` 用于只读 diff 与历史查看
- [x] 增加仓库搜索工具 `rg_search` 与受限目录枚举 `list_files_bounded`
- [x] 引入 `ToolRegistry` 类型化分发，附带 `ToolCategory` 与 `requires_approval` 元数据
- [x] 在运行时真正落地只读工具与修改型工具的权限/确认边界
<<<<<<< HEAD
- [ ] 增加 patch 校验与拒绝回退流程
- [x] 支持围绕 CMake 与 `ctest` 的受限 build/test 循环
- [ ] 强化失败恢复与基于 tool result 的重试提示
- [ ] 增加显式的提交封装流程，如 `git_add` 与 `git_commit`

### Phase 2

目标：把可复用工作流解耦为 Skills。

- [ ] 定义 skill 目录约定与 metadata/frontmatter 结构
- [ ] 实现 workspace 与共享位置的 skill loader
- [ ] 把选中的 skill 注入 prompt / session 装配流程
- [ ] 内置 C++ 规范、测试流程、git 流程等基础 skills
- [ ] 通过 CLI 与 config 暴露 skill 开关
- [ ] 增加 skill 执行 trace 与调试输出
- [ ] 用 skills 真正跑通一个仓库维护型 dogfood workflow

=======
- [ ] 增加 patch 校验与拒绝回退流程
- [ ] 支持围绕 CMake 与 `ctest` 的受限 build/test 循环
- [ ] 强化失败恢复与基于 tool result 的重试提示
- [ ] 增加显式的提交封装流程，如 `git_add` 与 `git_commit`

### Phase 2

目标：把可复用工作流解耦为 Skills。

- [ ] 定义 skill 目录约定与 metadata/frontmatter 结构
- [ ] 实现 workspace 与共享位置的 skill loader
- [ ] 把选中的 skill 注入 prompt / session 装配流程
- [ ] 内置 C++ 规范、测试流程、git 流程等基础 skills
- [ ] 通过 CLI 与 config 暴露 skill 开关
- [ ] 增加 skill 执行 trace 与调试输出
- [ ] 用 skills 真正跑通一个仓库维护型 dogfood workflow

>>>>>>> origin/main
### Docs Agent 里程碑

这条路线遵循：local validation first -> change-aware updates -> stronger verification and review -> CI readiness。

- [x] Milestone 1：完成本地优先的 docgen scaffold，包括规则、仓库内 skills、确定性脚本、生成物与基础校验
- [ ] Milestone 2：把代码变更映射到文档影响，并判断更新是必需、可选还是不需要
- [ ] Milestone 3：把 change-impact analysis 接到 README、教程与 getting-started 文档的增量更新流程里
- [ ] Milestone 4：增强 commands、paths、configs、environment references 等 doc-to-repo 一致性校验
- [ ] Milestone 5：增加聚焦清晰度、教学质量、结构与完整性的 review 阶段，再接受生成文档
- [ ] Milestone 6：把稳定的本地工作流打包，并为未来接入 CI / GitHub Actions 做准备
<<<<<<< HEAD

## 未来方向

更远期方向仍然围绕 runtime 本身展开：

- MCP 集成
- Telegram 或其他远程入口
- 更细粒度的 sandbox / permission 模型
- 更强的自主 coding workflow
=======

## 未来方向

更远期方向仍然围绕 runtime 本身展开：

- MCP 集成
- Telegram 或其他远程入口
- 更细粒度的 sandbox / permission 模型
- 更强的自主 coding workflow
>>>>>>> origin/main
