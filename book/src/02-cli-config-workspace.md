# CLI、配置与工作区 (CLI, Config & Workspace)

这章讲的是代理真正开始运行之前，系统如何先把三件事固定下来：入口参数、配置来源，以及“哪些路径算工作区内”。

## 1. 为什么需要这一层？ (Why)
NanoCodeAgent 不是单纯把一段 prompt 交给模型就结束的程序。它要决定用哪个模型、从哪里读取密钥、这次运行采用哪组配置，以及哪些文件路径允许被访问。

如果这些事情没有固定顺序，运行结果就会变得不可预测。更严重的是，模型一旦拿到模糊的路径边界，后面的读写工具就很容易把“当前项目里的文件”和“主机上的任意路径”混在一起。CLI、配置和工作区这一层的职责，就是先把运行条件说清楚，再把后面的能力放进受控范围里。

## 2. 整体图景 (Big Picture)
这一层的主线可以概括成三个连续动作。程序先建立一份默认配置，再从配置文件和环境变量里补齐它，最后让 CLI 参数做最后覆盖。等这些值固定后，`src/main.cpp` 才会创建或规范化工作区路径，并把这份配置交给后面的 LLM、工具层和 agent loop。

这里最重要的不是“参数很多”，而是“覆盖关系明确”。同一个字段如果同时出现在默认值、配置文件、环境变量和命令行里，最终以命令行为准；而“该读哪个配置文件”这件事本身，也先看 `--config`，找不到才回退到 `NCA_CONFIG`。

## 3. 主流程 (Main Flow)
实际启动顺序在 `src/main.cpp` 很清楚。程序先调用 `config_init(argc, argv)` 建立配置初值，再调用 `cli_parse(argc, argv, config)` 让命令行参数覆盖已有配置。只有 CLI 通过后，程序才初始化日志、创建或规范化工作区目录，并检查真实网络模式下是否已经提供 API key。

配置加载本身也分层进行。`src/config.cpp` 先设置默认值，然后扫描 `--config` 或 `--config=...`，如果命令行没有指定，再看 `NCA_CONFIG`；找到配置文件后，只按简单的 `key=value` 形式读取内容，忽略空行以及 `#`、`;` 注释。之后环境变量用 `NCA_` 前缀再次覆盖，最后 CLI 在 `src/cli.cpp` 里做最后一层覆盖。

工作区边界是在配置稳定之后才建立的。`src/main.cpp` 会把 `config.workspace` 规范化成绝对路径，后续文件类工具再通过 `workspace_resolve()` 检查相对路径是否仍位于这个基准目录之内。也就是说，工作区不是“随时变化的当前目录”，而是后续工具共享的一条固定边界。

## 4. 模块职责 (Module Roles)
- `src/main.cpp`：负责编排启动顺序，决定什么时候配置完成、什么时候工作区被固定、什么时候正式进入 agent loop。
- `src/config.cpp`：负责默认值、配置文件与环境变量这三层输入；它解决的是“同一个字段从哪里来”。
- `src/cli.cpp`：负责命令行解析和最终覆盖；它解决的是“这次运行明确要什么”，并要求必须提供 `--execute` 这一必填任务参数。
- `src/workspace.cpp`：负责把相对路径解释到工作区内，并拒绝绝对路径、空路径和越界路径。
- `src/read_file.cpp`、`src/write_file.cpp`、`src/repo_tools.cpp`：在工作区解析之上继续做更强的文件系统保护，例如拒绝符号链接穿越、设备文件和超出边界的访问。

## 5. 你通常会怎么用？ (What You Usually Do)
最常见的入口，是先指定工作区和任务，再按需要补充配置来源：

```bash
NCA_API_KEY="sk-..." ./build/agent \
  --workspace ./sandbox \
  --config ./agent.conf \
  -e "summarize the repository"
```

如果你只是想临时覆盖某个值，CLI 是最后一层，所以直接在命令行上传参最明确。比如 `--workspace`、`--model`、`--mode`、`--allow-mutating-tools` 和 `--allow-execution-tools` 都会覆盖前面的配置来源。

如果你希望把一组默认运行参数留在仓库外部文件里，可以使用配置文件，但要把它理解成简单的 `key=value` 清单，而不是完整的通用 INI 系统。当前实现支持的键以 `src/config.cpp` 中显式处理的字段为准。

## 6. 边界与易错点 (Boundaries / Pitfalls)
最容易误解的一点是：“工作区边界”并不等于“整个进程被操作系统级沙箱包住”。当前仓库里，强边界主要体现在文件和仓库工具上: 它们会把路径解析到工作区内，再拒绝符号链接穿越、特殊文件和越界访问。

这条边界不能被过度外推到所有执行面。比如 `bash_execute_safe()` 会在工作区下启动命令、清空子进程环境并施加超时与输出上限，但它不是容器，也不是 `chroot` 或 seccomp 级别的隔离。因此，这一章更准确的理解方式是：CLI 和配置负责固定运行条件，workspace 负责给文件类工具定义路径边界，而不是“整个系统已经被完全物理隔离”。

另一个常见误区是把旧示例命令直接照抄。当前 CLI 明确要求提供 `--execute` 对应的任务内容；如果缺少这一项，程序会直接报错退出，而不是进入默认交互。

## 7. 接下来往哪里看？ (Dive Deeper)
- [概览](01-overview.md)
- [HTTP 与 LLM 流式解析](03-http-llm-streaming.md)
- [工具与安全边界](04-tools-and-safety.md)
- [src/main.cpp](../../src/main.cpp)
- [src/config.cpp](../../src/config.cpp)
- [src/cli.cpp](../../src/cli.cpp)
- [src/workspace.cpp](../../src/workspace.cpp)
