# Diagram Spec

## Target

- Target doc: `book/src/05-testing.md`
- Diagram block: `block-01`
- Diagram type: `flowchart`

## Single Question

- 这张图只回答什么问题：`./build.sh test` 如何把 Debug 构建、ctest、测试可执行文件与生产源码、runtime 合同串成一条链。

## Reader Outcome

- 目标读者看完后应该获得什么心智模型：测试可执行文件直接链接生产源码，验证的是合同而非外壳。

## Out Of Scope

- 这张图明确不展示什么：单个测试文件名枚举、CI 矩阵。

## Planned Shape

- 预计主块 / 参与者：`build.sh test`、`DebugBuildWithAsan`、`ctest`、`GTestExecutables`、`ProductionSources`、`RuntimeContracts`
- 预计阅读方向：`LR`

## Split Decision

- 保留单图还是拆图：保留单图

## Visual Guardrails

- 哪些视觉噪音必须避免：不要把章节内测试清单塞进节点。
