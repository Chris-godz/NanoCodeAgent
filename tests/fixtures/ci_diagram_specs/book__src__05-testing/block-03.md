# Diagram Spec

## Target

- Target doc: `book/src/05-testing.md`
- Diagram block: `block-03`
- Diagram type: `flowchart`

## Single Question

- 这张图只回答什么问题：相对路径输入经解析、安全打开、类型检查到读写的文件边界链，以及何处会拒绝不安全访问。

## Reader Outcome

- 目标读者看完后应该获得什么心智模型：边界判断依赖解析与安全打开，不是“路径看起来在 workspace 内”。

## Out Of Scope

- 这张图明确不展示什么：具体 errno、平台差异细节。

## Planned Shape

- 预计主块 / 参与者：`RelativePathInput`、`workspace_resolve`、`secureOpenNoFollow`、`fileTypeChecks`、`read_or_write`、`RejectUnsafeAccess`
- 预计阅读方向：`LR`

## Split Decision

- 保留单图还是拆图：保留单图

## Visual Guardrails

- 哪些视觉噪音必须避免：不要把测试断言文本塞进节点。
