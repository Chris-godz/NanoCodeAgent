# CLI、配置加载与工作区 (CLI, Config & Workspace)

## 1. 为什么需要这个功能？ (Why)
Agent 在运行时必定需要访问大模型 API，同时也需要在本地文件系统内留存工作成果。如果不加约束，Agent 可能无意中把配置泄漏、甚至改写主机的敏感系统文件。
为此，引入 `Config Precedence`（配置优先级）机制是为了让开发者方便地在局部项目覆盖默认行为；而引入 `Workspace Sandbox` 则是为大模型的物理读写设立围墙。

## 2. 系统做了什么？ (What changed)
在当前的 `main` 分支中，我们提供了以下机制：

- **CLI 骨架 (`src/cli.*`)**: 实现了多参启动解析（如 `--workspace`，`--api-key`，`--config`），能够与下游组件通讯。
- **配置覆盖 (`src/config.*`)**: 支持四级优先级控制机制（Defaults < File (*.ini*) < Env (`NCA_` prefix) < CLI Flags）。保证了运行时的 API_KEY 和参数不仅能通过 CLI 提供，也能被 `.ini` 或环境注入以适应容器化需求。
- **工作区封锁 (`src/workspace.*`)**: 初始化后强制记录并锁定绝对路径，拦截任何含有 `..` 回退或者偏离 Base 路径的访问请求。

## 3. 对开发者的影响 / 接口 (Developer Impact)
可以通过以下方式启动代理并提供配置：
```bash
NCA_API_KEY="sk-..." ./build/agent --workspace ./sandbox --config ./my_config.ini
```
若尝试跨越 `/sandbox` 读取 `/etc/passwd` 等行为，会在内部立刻被 Workspace Validator 拦截抛出异常。

## 4. 相关文件映射 (Relevant Files)
- [src/cli.cpp](../src/cli.cpp)
- [src/config.cpp](../src/config.cpp)
- [src/workspace.cpp](../src/workspace.cpp)
