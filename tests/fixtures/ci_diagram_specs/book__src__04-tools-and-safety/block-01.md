# Diagram Spec

## Target

- Target doc: `book/src/04-tools-and-safety.md`
- Diagram block: `block-01`
- Diagram type: `flowchart`

## Single Question

- 这张图只回答什么问题：工具调用从注册校验、策略审批、受限执行到 fail-fast 的控制链如何收紧。

## Reader Outcome

- 目标读者看完后应该获得什么心智模型：先认工具、再判策略、再执行、再按结果决定继续或停机。

## Out Of Scope

- 这张图明确不展示什么：工具分类细目、executor 内部实现、具体 approval 配置。

## Planned Shape

- 预计主块 / 参与者：`Assistant Tool Call`、`Registered`、`Approved By Policy`、`Bounded Executor`、`Result`、`Fail-Fast Stop`、`Next Turn`
- 预计阅读方向：`LR`

## Split Decision

- 保留单图还是拆图：保留单图

## Visual Guardrails

- 哪些视觉噪音必须避免：避免长句标签；决策节点保持短词。
