# Diagram Spec

## Target

- Target doc: `book/src/05-testing.md`
- Diagram block: `block-02`
- Diagram type: `flowchart`

## Single Question

- 这张图只回答什么问题：主要风险面如何映射到必须守住的行为合同，再映射到代表测试簇。

## Reader Outcome

- 目标读者看完后应该获得什么心智模型：测试存在的逻辑是风险→合同→测试簇，而非文件索引。

## Out Of Scope

- 这张图明确不展示什么：每个测试源文件的完整列表。

## Planned Shape

- 预计主块 / 参与者：五组 `Risk`→`Contract`→`Tests` 链（路径逃逸、流式碎片、危险工具、失控执行、repo 探测）
- 预计阅读方向：`LR`

## Split Decision

- 保留单图还是拆图：保留单图

## Visual Guardrails

- 哪些视觉噪音必须避免：节点文案保持短词组。
