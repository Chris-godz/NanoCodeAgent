# Diagram Spec

## Target

- Target doc: `book/src/01-overview.md`
- Diagram block: `block-02`
- Diagram type: `sequence`

## Single Question

- 这张图只回答什么问题：一个典型任务是如何从入口装配进入 agent loop，并在有无 `tool_calls` 两种情况下完成或继续下一轮。

## Reader Outcome

- 目标读者看完后应该获得什么心智模型：理解主流程的时序骨架，知道入口、agent loop、LLM、registry 和 executor 分别在什么时候介入，而不是把它们只当作模块清单。

## Out Of Scope

- 这张图明确不展示什么：不展示具体配置优先级细节、workspace 路径规则、某个工具 executor 的内部实现、测试覆盖图。
- 哪些细节应下沉到后续章节图：配置与 workspace 细节下沉到 `book/src/02-cli-config-workspace.md`，流式拼装细节下沉到 `book/src/03-http-llm-streaming.md`，审批与边界细节下沉到 `book/src/04-tools-and-safety.md`。

## Planned Shape

- 预计主块 / 参与者：`User`、`MainEntry`、`AgentLoop`、`LlmBridge`、`ToolRegistry`、`ToolExecutor`
- 预计阅读方向：`sequence top-down`
- 预计 caption 主句：这张图只展示典型任务从入口到回答或工具调用的时序主线，不展开子系统内部实现。

## Split Decision

- 保留单图还是拆图：保留单图
- 为什么：当前图只表达任务主时序，虽然含有 `alt` 和 `loop`，但仍然围绕单一主问题，阅读方向稳定。
- 若拆图，拆成哪两张：若后续引入更多异常路径，可拆成“无工具回答路径”和“tool call 执行路径”。

## Visual Guardrails

- 哪些视觉噪音必须避免：不要在线条上挂长句解释，不要把失败分支或实现备注塞进 participant 名称。
- 哪些节点文案必须保持短、名词化、可扫读：`User`、`MainEntry`、`AgentLoop`、`LlmBridge`、`ToolRegistry`、`ToolExecutor`
