# Diagram Spec

## Target

- Target doc: `book/src/01-overview.md`
- Diagram block: `block-01`
- Diagram type: `overview`

## Single Question

- 这张图只回答什么问题：NanoCodeAgent 的系统地图由哪几层组成，以及任务在这些层之间如何大致衔接。

## Reader Outcome

- 目标读者看完后应该获得什么心智模型：先把入口层、runtime、执行边界和测试保障层区分开，知道总览图不是实现细节图，而是一张阅读后续章节的地图。

## Out Of Scope

- 这张图明确不展示什么：不展示 `LLM bridge`、`Tool Call Assembler`、approval 细节、具体工具分类、失败恢复分支。
- 哪些细节应下沉到后续章节图：流式解析细节下沉到 `book/src/03-http-llm-streaming.md`，边界与审批下沉到 `book/src/04-tools-and-safety.md`，测试覆盖关系下沉到 `book/src/05-testing.md`。

## Planned Shape

- 预计主块 / 参与者：`User Task`、`Entry Layer`、`Runtime Loop`、`Execution Guardrails`、`Assurance`
- 预计阅读方向：`LR`
- 预计 caption 主句：这张图只展示系统层级与主衔接顺序，不展开实现细节和审批链。

## Split Decision

- 保留单图还是拆图：保留单图
- 为什么：这张图只承担系统地图语义，节点和连线仍然稳定地服务于单一问题，没有混入时序或异常分支。
- 若拆图，拆成哪两张：若后续需要扩展，可拆成“系统结构图”和“主任务推进图”。

## Visual Guardrails

- 哪些视觉噪音必须避免：不要把实现名词、异常说明或长解释句放进节点和连线中心区域。
- 哪些节点文案必须保持短、名词化、可扫读：`User Task`、`Entry Layer`、`Runtime Loop`、`Execution Guardrails`、`Assurance`
