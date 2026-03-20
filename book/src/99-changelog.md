# 更新日志 (Changelog)

此日志仅关注该库核心架构能力的演变，严禁出现未经合并的主观路线图推测。

- `**feat: integrate real LLM networking and unify mock streaming loops**`
  - 核心突破：全量整合真实的 LLM 网络通讯到流式解析循环中。
- `**feat(agent): implement agent loop closed-state and multi-tier brake limits bounds**`
  - 核心突破：加入了多轮拦截循环与超时/错误边界限制。
- `**feat: bootstrap teaching site with mdBook ...**`
  - 架构变更：建立规范化的文档站点并引入基于 `mdBook` 的工程脚手架。
- `**feat: implement bash_execute_safe with DOD resource limits and deadlock-free piping**`
  - 工具能力：引入完整的、防御死锁的终端沙箱 `bash_tool`。
- `**feat: implement secure workspace physical write/read tool...**`
  - 工具能力：实装严密的 `read_file` 和 `write_file` 安全工具边界防线。
- `**feat: add streaming tool_call aggregation ...**`
  - 数据解析：实现了增量的 JSON 函数调用拼接技术。
- `**feat: implement HTTP/LLM bridge ... & SSE streaming and incremental JSON parsing**`
  - 通讯协议：创建基础的网络 C++ 套接字模块与 SSE 增量解码通道。
- `**feat: implement config precedence and workspace security sandbox**`
  - 配置基建：构建四级加载的设定环境以及隔离非法路径的安全目录策略。
- `**commit (initial): CLI skeleton**`
  - 工程初始：引入 CLI 选项解析及基础日志接入。

