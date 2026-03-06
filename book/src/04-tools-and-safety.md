# 工具层、安全边界与循环 (Tools & Agent Loop)

## 1. 为什么需要这个功能？ (Why)
Agent 通过工具（Tools）感知环境和产生影响。让不可靠的 LLM 直接拥有原生系统的全部操作权限是极其危险的（例如全盘擦除、进入死循环）。我们必须依靠 C++ 提供严格的物理壁垒和控制流中断机制，保证读写操作不越狱、终端指令不越权、调用循环不死锁。

## 2. 系统做了什么？ (What changed)
目前 `main` 仓库包含了如下健壮的工具与执行环机制：

- **读写安全工具 (`src/read_file.*` & `src/write_file.*`)**: 代理对文件的修改操作。在执行前会受到 `workspace` 沙箱的仲裁，拦截符号链接攻击（symlink traversal）与 TOCTOU 等漏洞。
- **安全的内嵌终端 (`src/bash_tool.*`)**: 利用 Unix 的 `fork` 与 `execvp` 启动完全无残留的隔离子系统执行命令。它会切断原系统的 ENV 并利用 `poll` 非阻塞轮询和管道机制（Pipes）防止读写死锁。对输出进行 size 截断，对运行时间进行 timeout 扼杀。
- **工具分发中心 (`src/agent_tools.*`)**: 通过规范的模板对外输出所有被允许执行的 Tool JSON Schema，并为上层提供统一的分发接口去调用物理工具。
- **受控封闭循环 (`src/agent_loop.*`)**: 一个负责轮询 LLM → 调用特定 Tool → 返回结果 → 继续请求 LLM 的引擎。它具备明确的多轮拦截器（Brake limits Bounds），遇到恶意或无意义的循环会主动抛出 `AGENT LIMIT EXCEEDED` 终止死锁。

## 3. 对开发者的影响 / 接口 (Developer Impact)
可以通过查阅 `get_agent_tools_schema()` 查看当前提供给模型的所有挂载工具。并在 `agent_run` 当中修改相应的限制值来调整防线阈值。

## 4. 相关文件映射 (Relevant Files)
- [src/read_file.cpp](../src/read_file.cpp)
- [src/write_file.cpp](../src/write_file.cpp)
- [src/bash_tool.cpp](../src/bash_tool.cpp)
- [src/agent_tools.cpp](../src/agent_tools.cpp)
- [src/agent_loop.cpp](../src/agent_loop.cpp)
