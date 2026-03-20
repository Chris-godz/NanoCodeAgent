## Target

- Target doc: `book/src/06-documentation-automation.md`
- Diagram block: `block-01`
- Diagram type: `overview`

## Single Question

- 这张图只回答什么问题：当前文档自动化能力由哪几个阶段组成，以及事实、范围、写作、验证和审稿之间如何交接。

## Reader Outcome

- 目标读者看完后应该获得什么心智模型：先把 docgen 看成“事实与边界先行，再进入真实文档写作与闭环验证”的流程，而不是一条模型自由发挥的单脚本链。

## Out Of Scope

- 这张图明确不展示什么：每个 verify checker 的实现细节、闭环重试上限、具体 task 文件内容、diagram artifact 文件名。
- 哪些细节应下沉到后续章节图：verify/review/rework 的限制和产物进入本章后续正文说明。

## Planned Shape

- 预计主块 / 参与者：`Code Change`、`Change Facts`、`Scope Decision`、`Reference Context`、`README + Book Draft`、`Verify`、`Review / Rework`、`Evidence Summary`
- 预计阅读方向：`LR`
- 预计 caption 主句：这张图展示文档自动化的主阶段与交接边界。

## Split Decision

- 保留单图还是拆图：保留单图
- 为什么：这里先只讲高层流程，不展开重试分支；单图能稳定回答“文档自动化整体怎么走”这个问题。
- 若拆图，拆成哪两张：不拆

## Visual Guardrails

- 哪些视觉噪音必须避免：不要把 `verify_paths.py`、`verify_links.py`、`verify_mermaid.py` 等脚本名全部塞进节点。
- 哪些节点文案必须保持短、名词化、可扫读：`Change Facts`、`Scope Decision`、`Reference Context`、`Verify`、`Review / Rework`
