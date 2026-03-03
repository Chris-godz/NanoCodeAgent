# 第 7 章：Shell 工具与安全边界

## Bash 工具（`bash_tool.cpp`）

Bash 工具允许 LLM 在受控环境中执行 Shell 命令，是 Code Agent 最强大也最危险的能力之一。

## 安全设计

### 工作区隔离

- 所有命令在**工作区目录**（workspace）下执行
- 通过 `chdir()` 将进程工作目录切换到工作区
- 防止直接访问工作区外的文件系统

### 超时控制

- 每条命令设置执行超时（可配置）
- 超时后强制终止子进程，防止无限循环

### 竞态条件防护

最近的安全加固（`fix(bash): tighten bash tool isolation boundaries and race conditions`）
解决了多个并发执行时的竞态条件问题。

## 已知限制

| 限制 | 说明 |
|------|------|
| 网络访问 | 不限制（依赖系统防火墙） |
| 文件系统 | 仅软隔离（工作目录），非容器 |
| 资源限制 | 依赖 OS 配置，无内置 cgroup |

## 最佳实践

1. **最小权限原则**：以普通用户身份运行 Agent，不要用 root
2. **只读挂载**：重要目录使用只读权限
3. **沙盒容器**：生产环境建议在 Docker/Podman 容器内运行

## 与 `workspace.cpp` 的关系

`workspace.cpp` 维护当前工作区路径，Bash 工具和文件工具都通过 `Workspace` 类
获取安全的绝对路径，确保一致的边界校验。

<!-- TODO: 补充 seccomp/AppArmor 集成方案 -->
