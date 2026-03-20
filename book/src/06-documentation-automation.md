# 文档自动化 (Documentation Automation)

这章讲的是 NanoCodeAgent 如何把“文档要不要改、该改哪里、改完怎么证明没写偏”拆成一条可验证的流程，而不是把整件事一次性交给模型自由发挥。

## 1. 为什么需要这条流程？ (Why)
文档更新真正困难的部分，通常不是措辞，而是范围控制。一次代码变更出现后，系统必须先回答几个问题：这是不是用户可见变化、应该落到 `README.md` 还是 `book/src/`、只是补充说明还是需要调整章节结构。

如果这些判断没有先固定下来，模型就容易把“找证据、定范围、写内容、做校验”混成一步，最后写出一份看起来完整、但和仓库事实对不上的文档。文档自动化存在的意义，就是先把事实、职责和出口分开，再让写作发生。

## 2. 整体图景 (Big Picture)
现在这条能力更适合看成三层协作，而不是一条单脚本流水线。最前面是**客观事实层**：`scripts/docgen/change_facts.py` 只提取 changed files、change type、public surface signals 之类的事实，不提前替 writer 猜目标文档。中间是**范围与证据层**：AI scope decision 负责回答 README / book 是否在范围内、批准哪些目标，而 `scripts/docgen/reference_context.py` 只补充 examples、tests、templates、source paths 与现有文档摘录，供写作阶段引用。最后才是**起草与闭环层**：writer 只改获批文档，verify / review 再判断这些修改是否真的站得住，而且会明确说明“哪些批准目标真的被检查了，哪些还没有”。

如果你只想知道这次代码变更是否值得更新文档，change-impact 路径仍然存在，但它现在更像前置筛查工具。它会从 `scripts/docgen/changed_context.py` 生成面向人的 change context，再由 `scripts/docgen/generate_impact_report.py` 写出 `docs/generated/change_impact_report.md`。这条路径停在“值不值得继续”。

如果你已经决定要让仓库里的真实文档落地更新，主入口则是 `scripts/docgen/run_docgen_e2e_closed.sh`。它会先清理旧产物，再产出 `docs/generated/change_facts.json`、`docs/generated/doc_scope_decision.json` 与 `docs/generated/reference_context.json`，随后调用 writer 更新 `README.md`、`README_zh.md`、`book/src/` 下的获批章节，以及在需要时更新 `book/src/SUMMARY.md`。真正的重点不是“又多了几个 JSON”，而是每一段 AI 写作之前都先有事实和边界，后面还有 verify、review 和 evidence summary 接手；verify 不再只是笼统地说“过了或没过”，而是要把覆盖范围和缺口一起暴露出来。

## 3. 主流程 (Main Flow)
正常情况下，流程从一次代码 diff 开始，但现在要先分清你是在做“影响判断”还是“真实文档更新”。

如果目标只是判断影响范围，入口是 `scripts/docgen/run_change_impact.sh`。它读取规则与 skill 后，调用 `scripts/docgen/changed_context.py` 生成 `docs/generated/change_context_output.md`，再由 `scripts/docgen/generate_impact_report.py` 写出 `docs/generated/change_impact_report.md`，最后对这份报告运行路径与链接校验。你得到的是一份供人或下游阶段参考的报告，而不是已修改的 README / book。

如果目标是真正改文档，入口是 `scripts/docgen/run_docgen_e2e_closed.sh`。这条闭环先做 Step 0 清理，避免旧的 `docs/generated/change_facts.json`、`docs/generated/doc_scope_decision.json`、`docs/generated/reference_context.json`、diagram artifacts 相关生成目录和旧状态文件污染当前 run。然后 `scripts/docgen/change_facts.py` 产出客观事实，AI scope decision 决定 README 和哪些书籍章节在范围内，`scripts/docgen/reference_context.py` 再收集参考证据。writer 只能编辑 scope 批准的真实目标文档，而不是去改 `docs/generated/` 下的中间产物。

草稿写完后，流程不会直接宣布完成。`scripts/docgen/missing_diagram_specs.py` 会先检查在范围内的 Mermaid 文档是否缺少 diagram spec 产物；如果缺了，会先触发专门的 backfill 任务。随后 `scripts/docgen/run_docgen_e2e_loop.py` 进入 verify -> verify_repair -> review -> review_rework 的受限循环。验证阶段通过 `scripts/docgen/run_verify_report.py` 统一运行路径、链接、diagram spec、Mermaid render 与命令检查；其中路径、链接、diagram spec 与 Mermaid render 是 blocking，命令检查不是。新的重点是：verify 现在同时回答两件事，一是文档内容本身有没有坏路径、坏链接或坏 Mermaid，二是 scope decision 批准的目标里有没有文件根本没被 verify 覆盖。后者会在 `docs/generated/verify_report.json` 里通过 `approved_targets`、`target_docs_checked`、`not_checked` 与 `verify_coverage_note` 显式留下痕迹，而不是让人误以为“批准了就一定检查到了”。若存在 Mermaid，`scripts/docgen/verify_mermaid.py` 还会写出 SVG、PNG 与 artifact report，供后续 visual review 使用。

只有当 blocking verify 通过、review 的 must-fix 也被清空，闭环才会通过 `scripts/docgen/render_docgen_e2e_summary.py` 和 `docs/generated/e2e_run_evidence.json` 给出这次 run 的最终摘要。如果修复次数达到上限，流程会明确写出 TerminalFail，而不是继续无限重试。

## 4. 模块职责 (Module Roles)
- **客观事实层**：`scripts/docgen/change_facts.py` 负责从 diff 中抽取结构化事实，避免一开始就把“事实提取”和“写作判断”混在一起；`scripts/docgen/changed_context.py` 继续服务 change-impact 报告，它提供的是上下文提示，不是 scope 决策。
- **影响判断层**：`scripts/docgen/run_change_impact.sh` 与 `scripts/docgen/generate_impact_report.py` 负责回答“这次变化是否值得更新文档”，并把答案落到 `docs/generated/change_impact_report.md`。
- **范围与证据层**：AI scope decision 决定是否更新 README、是否更新 book、批准哪些目标，以及是否需要调整 `book/src/SUMMARY.md`；`scripts/docgen/reference_context.py` 只补充写作证据，不扩展编辑范围。
- **真实文档写入层**：writer 阶段只允许修改获批的 `README.md`、`README_zh.md` 和 `book/src/` 下文档；如果书籍结构要变，`book/src/SUMMARY.md` 也会跟着更新。
- **Diagram contract 层**：`scripts/docgen/missing_diagram_specs.py` 与 `scripts/docgen/verify_diagram_specs.py` 负责确保 Mermaid 图不只是“能画”，还要先有 diagram spec 说明合同。
- **验证与审稿层**：`scripts/docgen/run_verify_report.py` 汇总 blocking / non-blocking 检查，并把批准范围与实际 verify 覆盖面一起写进 `docs/generated/verify_report.json`；`scripts/docgen/verify_paths.py` 也支持对生成型证据目录做前缀级忽略，避免把 `docs/generated/` 这类中间产物误判成必须存在的正式文档路径；`scripts/docgen/verify_mermaid.py` 负责真实渲染并产出截图证据；review 和 rework 则消费这些证据，而不是只看 Markdown 源码。
- **闭环与审计层**：`scripts/docgen/run_docgen_e2e_loop.py` 管理 retry 上限、失败类型、状态文件，以及 `docs/generated/e2e_run_evidence.json` 这份 run 级别的证据清单。
- **兼容与旧流程脚本**：`scripts/docgen/run_docgen_e2e.sh`、`scripts/docgen/generate_candidates.py`、`scripts/docgen/run_validation_report.py`、`scripts/docgen/run_review_report.py` 仍保留在仓库中，但更偏向旧版 candidate-based 流程或独立分析工具；E2E summary 现由 `render_docgen_e2e_summary.py` 在主路径中产出。

## 5. 你通常会怎么用？ (What You Usually Do)
进入 AI 阶段之前，先把前置条件按责任拆开理解。`scripts/docgen/setup.sh` 会检查 `python3`、`git`、`npm`、Python venv 支持，以及当前目录是否位于 git worktree 中；它的职责是确认主机具备运行 docgen 与后续 Mermaid bootstrap 的基础条件，而不是替你检查闭环专属工具。

真正进入闭环时，`scripts/docgen/run_docgen_e2e_closed.sh` 才会继续要求 `codex` 在 `PATH` 里。`scripts/docgen/setup.sh` 也不会提前安装 Mermaid 相关依赖；只有当 in-scope 文档里真的包含 Mermaid 时，`scripts/docgen/verify_mermaid.py` 才会按需准备 `.venv/`、安装 Python `playwright` 与 Playwright `chromium`，并通过 `npm` 在 `tmp/mermaid-tools/` 下安装 Mermaid bundle。

你只想判断影响范围时，通常走 `scripts/docgen/run_change_impact.sh`。这条路径适合在改动刚落地、你还不确定是否需要动 README 或书籍时使用；它的输出是 `docs/generated/change_impact_report.md`，重点是给出“值不值得继续”的结论。

你已经确定要更新真实文档时，通常走 `scripts/docgen/run_docgen_e2e_closed.sh`。这条路径默认以 `HEAD~1` 作为 diff 基准；如果仓库历史不足、是浅克隆，或你想拿别的基线比较，需要显式设置 `REF=main` 或其他可解析的 ref。它会按 change facts -> scope decision -> reference context -> doc update -> verify / review / summary 的顺序推进。对日常使用者来说，最重要的判断不是记住每个中间产物名字，而是理解这条闭环会先定边界，再写正文，最后验证“写得对不对、查得全不全”。

你只是想复用旧版 candidate-based 流程，或者暂时只想看 candidates、validation report 和 heuristic review，也还能运行 `scripts/docgen/run_docgen_e2e.sh`。但这条路径不会更新真实 README / book，也不会驱动新的闭环修复逻辑。

## 6. 边界与易错点 (Boundaries / Pitfalls)
真实编辑目标仍然只有 `README.md`、`README_zh.md` 与 `book/src/` 下的 Markdown 文档。`docs/generated/` 下的 JSON、报告、状态文件、diagram specs 和 diagram artifacts 都是中间交接物或审计证据，不是最终用户文档。即使 `book/src/SUMMARY.md` 被纳入批准目标，它通常也只在章节结构真的变化时才需要修改。

验证阶段也不是所有检查都同等严格。按 `scripts/docgen/run_verify_report.py` 当前实现，路径检查、链接检查、diagram spec 检查与 Mermaid render 都是 blocking，命令检查是 non-blocking。这意味着坏路径、坏链接、缺 diagram spec 或 Mermaid render fail 都会挡住主流程，而命令问题更像额外信号。与此同时，路径检查不再是“见到 repo-relative 路径就一律必须存在”这么死板：`verify_paths.py` 现在支持 `--ignore-repo-prefix`，而 `run_verify_report.py` 会把这层能力以 `--verify-paths-ignore-repo-prefix` 继续向上暴露，允许调用方对 `docs/generated/` 这类生成型证据目录做前缀级豁免。这个豁免只影响路径存在性校验，不会改变链接检查、diagram spec 或 Mermaid render 的 blocking 合同。

另一个常见误解，是把 verify 看成“只要对 in-scope 文档跑过一次脚本，就默认覆盖完整”。现在不该再这样理解了。scope decision 批准的是“理论上应该被检查的集合”，而 verify 报告负责告诉你“实际检查到了哪几个目标”。如果某个批准目标因为文件不存在或当前 verify 还不支持而没有进入检查集合，报告会把这个缺口单独列出来，提醒你这是 pipeline 覆盖问题，不是文案已经没问题的证据。

再一个常见误解，是把 `scripts/docgen/run_change_impact.sh`、`scripts/docgen/run_docgen_e2e.sh` 与 `scripts/docgen/run_docgen_e2e_closed.sh` 当成同一条线上的不同名字。实际上它们分属三种责任：change-impact 只做影响判断；`scripts/docgen/run_docgen_e2e.sh` 属于旧版 candidate-based 流程；`scripts/docgen/run_docgen_e2e_closed.sh` 才是当前把真实文档写入、verify、review、repair 和 evidence summary 串起来的闭环入口。

CI 里的责任也要分开看。`.github/workflows/core-ci.yml` 只在 `workflow_dispatch` 下运行完整闭环，而且前提是 build/test 先通过；`.github/workflows/docs-validate.yml` 仍然负责 PR 与主分支上的文档校验，并会在聚合 verify 前恢复 CI 用的 diagram specs，然后调用 `scripts/docgen/run_verify_report.py` 对固定 scope 做检查。这个校验入口也会传入 `docs/generated/` 前缀忽略，避免把生成证据目录误当成必须随仓库提交的正式路径。

最后，闭环不是“修到过为止”。当 verify repair 达到 2 次、review rework 达到 2 次，或总修复动作达到 4 次时，`scripts/docgen/run_docgen_e2e_loop.py` 会停止并写出 TerminalFail 相关产物，要求人来接手。无论成功还是失败，本次 run 的 diagram spec / artifact / screenshot 证据都会集中记录到 `docs/generated/e2e_run_evidence.json`，避免和旧 run 的残留产物混在一起靠人猜。

## 7. 继续深入 (Dive Deeper)
- [scripts/docgen/README.md](../../scripts/docgen/README.md)
- [docs/docgen_e2e_flow.md](../../docs/docgen_e2e_flow.md)
- [scripts/docgen/tasks/README.md](../../scripts/docgen/tasks/README.md)
- [tests/test_docgen_change_impact.py](../../tests/test_docgen_change_impact.py)
- [tests/test_docgen_e2e.py](../../tests/test_docgen_e2e.py)
- [tests/test_docgen_e2e_loop.py](../../tests/test_docgen_e2e_loop.py)
