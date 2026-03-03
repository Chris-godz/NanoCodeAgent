# 第 5 章：HTTP 与 LLM 集成

## HTTP 客户端（`http.cpp`）

NanoCodeAgent 使用 **libcurl** 作为底层 HTTP 客户端，封装在 `http.cpp` 中。

### 主要功能

- 发送 POST 请求到 LLM API 端点
- 支持自定义请求头（Authorization、Content-Type 等）
- 流式响应处理（通过 CURLOPT_WRITEFUNCTION 回调）

## LLM 集成（`llm.cpp`）

### API 兼容性

NanoCodeAgent 兼容 **OpenAI Chat Completions API** 格式，可对接：
- OpenAI（GPT-4、GPT-4o 等）
- Azure OpenAI
- GitHub Models（`https://models.inference.ai.azure.com`）
- 本地模型（Ollama 等）

### 请求结构

```json
{
  "model": "gpt-4o-mini",
  "messages": [...],
  "tools": [...],
  "stream": true
}
```

### 流式解析（`sse_parser.cpp`）

Server-Sent Events（SSE）格式：

```
data: {"choices":[{"delta":{"content":"Hello"},"finish_reason":null}]}

data: [DONE]
```

解析器逐行读取，提取 `data:` 后的 JSON，累积 `delta` 内容。

## GitHub Models 配置示例

```ini
base_url = https://models.inference.ai.azure.com/chat/completions
model = openai/gpt-4o-mini
api_key = <your-github-token>
```

<!-- TODO: 补充错误处理和重试机制 -->
