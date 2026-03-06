# 测试与 TDD (Testing)

## 1. 为什么需要这个功能？ (Why)
作为一个要运行不受信任代码与随机返回负载环境的代理系统，运行时级别的内存安全与业务逻辑防线至关重要。纯 C++ 的开发极其依赖 GoogleTest (gtest) 与基于 AddressSanitizer (ASan) 的环境来排查潜在的文件描述符泄漏和越界访问风险。因此我们将测试置于项目的一等公民位置。

## 2. 系统做了什么？ (What changed)
目前 `main` 仓库包含了大量详尽的边界条件测试：

- **测试框架集成**: 完整引入了 gtest，并在 `tests/` 目录中镜像映射了源码结构，支持通过 `make test` 或自动化脚本驱动。
- **内存防漏监测**: 集成了强制的 ASan 参数（当使用 DEBUG 构建时），任何细微的内存泄漏及越界访问都会导致测试终止报错。
- **重点边界校验**: 
  - `test_bash_tool.cpp`: 测试了超量输出与超长超时的情况是否正确截断。
  - `test_agent_loop_limits.cpp`: 测试多轮死循环场景是否成功被引擎上限遏制。
  - `test_read_file.cpp` / `test_write_file.cpp`: 测试了非法字符、向上遍历 (`..`) 等等越权读取是否被正确捕获阻断。
  - `test_sse_parser.cpp`: 测试对残缺的 SSE 碎片拼接时的稳健性与容错能力。

## 3. 对开发者的影响 / 接口 (Developer Impact)
编写任何新功能都**必须**在对应的 `tests/` 目录下新增相应的文件。可以使用 `build.sh test` 在本地跑通所有测试靶机，这也是 CI 中强制的一环。

## 4. 相关文件映射 (Relevant Files)
- [tests](../tests) (测试目录整体)
