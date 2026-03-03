# 第 4 章：工具调用机制

## 什么是工具调用（Tool Calling）？

工具调用（Function Calling / Tool Use）是现代 LLM 的核心能力之一。
模型在推理时可以"决定"调用外部工具，并返回结构化的调用请求，
由应用层执行后将结果返回给模型，继续对话。

## NanoCodeAgent 支持的工具

| 工具名 | 功能 | 对应文件 |
|--------|------|----------|
| `bash` | 执行 Shell 命令 | `bash_tool.cpp` |
| `read_file` | 读取文件内容 | `read_file.cpp` |
| `write_file` | 写入/创建文件 | `write_file.cpp` |

## 工具描述格式（OpenAI 兼容）

```json
{
  "type": "function",
  "function": {
    "name": "bash",
    "description": "在工作区执行 Bash 命令",
    "parameters": {
      "type": "object",
      "properties": {
        "command": {
          "type": "string",
          "description": "要执行的 shell 命令"
        }
      },
      "required": ["command"]
    }
  }
}
```

## 调用流程

```
模型输出 (SSE 片段)
  → SSE Parser 解析 delta
  → Tool Call Assembler 收集完整调用
  → 解析 tool_calls[].function.{name, arguments}
  → 路由到对应工具函数
  → 执行并获取结果字符串
  → 构造 role=tool 消息追加到 messages
  → 再次调用 LLM
```

## 安全注意事项

- `bash` 工具：命令在限定的工作区目录下执行，详见[第 7 章](./07-bash-tool.md)
- `write_file` 工具：路径必须是相对路径，且在规范化后不能跳出工作区目录（允许包含会被规范化到工作区内的 `..` 段），详见[第 6 章](./06-file-tools.md)

<!-- TODO: 补充工具调用失败的重试逻辑说明 -->
