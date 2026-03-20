# Diagram Spec

## Target

- Target doc: `book/src/03-http-llm-streaming.md`
- Diagram block: `block-01`
- Diagram type: `sequence`

## Single Question

- 这张图只回答什么问题：流式响应从 HTTP 传输到 SSE 解析、LLM bridge、tool-call 组装，最终如何形成完整 assistant message。

## Reader Outcome

- 目标读者看完后应该获得什么心智模型：理解 content 增量与 tool_call 碎片如何在同一流里交错到达，以及各层职责分界。

## Out Of Scope

- 这张图明确不展示什么：HTTP 限额细节、agent_loop 审批与工具执行、具体 JSON 字段全集。

## Planned Shape

- 预计主块 / 参与者：`LLM Service`、`HTTP Transport`、`SSE Parser`、`LLM Bridge`、`Tool Call Assembler`、`Agent Loop`
- 预计阅读方向：`sequence top-down`

## Split Decision

- 保留单图还是拆图：保留单图

## Visual Guardrails

- 哪些视觉噪音必须避免：不要把实现细节堆在 participant 名称里。
