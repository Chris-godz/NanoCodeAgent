# 第 8 章：配置与日志系统

## 配置系统（`config.cpp`）

NanoCodeAgent 使用**扁平 `key=value` 格式**的配置文件（无 section），通过 `--config` 参数指定文件路径。

### 配置文件结构

```ini
# NanoCodeAgent 配置文件（flat key=value，注释用 # 或 ;）
model = openai/gpt-4o-mini
api_key = <your-api-key>
base_url = https://models.inference.ai.azure.com/chat/completions
workspace = ./my_workspace
debug = true
```

### 支持的配置键

| 键 | 默认值 | 说明 |
|----|--------|------|
| `model` | `gpt-4o` | 使用的模型名称 |
| `api_key` | — | API 密钥 |
| `base_url` | `https://api.openai.com/v1` | LLM API 端点 |
| `workspace` | `.` | 工作区目录（相对或绝对路径） |
| `debug` | `false` | 开启调试日志（`true` 或 `1`） |

### 环境变量覆盖

所有配置项均可通过环境变量覆盖（优先级高于配置文件）：

| 环境变量 | 对应配置键 |
|----------|-----------|
| `NCA_MODEL` | `model` |
| `NCA_API_KEY` | `api_key` |
| `NCA_BASE_URL` | `base_url` |
| `NCA_WORKSPACE` | `workspace` |
| `NCA_DEBUG` | `debug` |
| `NCA_CONFIG` | 配置文件路径 |

### 优先级

```
内置默认值 → 配置文件 → NCA_* 环境变量 → 命令行参数 (--model/--api-key/--base-url/--workspace/--debug 等，最高优先级)
```

## 日志系统（`logger.cpp`）

NanoCodeAgent 使用 **spdlog** 库实现日志。

### 日志级别

当前支持两个级别，通过 `debug` 配置项或 `NCA_DEBUG` 环境变量控制：

| 条件 | 日志级别 |
|------|----------|
| `debug=false`（默认） | `info`（及以上） |
| `debug=true` | `debug`（及以上） |

### 开启调试日志

```bash
# 方式 1：配置文件
debug = true

# 方式 2：环境变量
NCA_DEBUG=true ./build/agent --config config.ini
```

### 日志格式

```
[2025-01-01 12:00:00.123] [info] Sending request to model gpt-4o-mini
[2025-01-01 12:00:01.456] [debug] Tool call: bash { "command": "ls -la" }
```

日志输出到 **stdout**（spdlog 默认行为），可通过 shell 重定向分离：`./build/agent > log.txt`。

<!-- TODO: 未来计划支持日志文件输出和更细粒度的日志级别（warn/error） -->
