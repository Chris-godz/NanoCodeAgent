# 大模型网络与流式解析 (HTTP, LLM & Streaming)

## 1. 为什么需要这个功能？ (Why)
大模型代理在执行任务时，会返回大量结构化的文本以及密集的工具调用（Tool Calls）。如果使用同步阻塞的方式等待模型吐出完整长文本，会导致用户体验极度卡顿；如果不使用 SSE (Server-Sent Events) 流式解析，大模型的响应中途如果被截断将丢失所有上文。我们需要一个纯原生的非阻塞事件流框架来处理大块数据的到达。

## 2. 系统做了什么？ (What changed)
当前 `main` 分支在网络通信与协议解析上实现了如下基础：

- **C++ Native HTTP (`src/http.*`)**: 一个轻量级的基于系统 Socket 的网络客户端，用于直接构造发往大模型网关的报文头，不依赖庞大的三方网络库如 libcurl 即可提供原生支持。
- **SSE Parser (`src/sse_parser.*`)**: 针对 `text/event-stream` 格式的解析器。能够安全地将网络断断续续发来的残块拼装成合法的 JSON chunk，并在遇到特殊结束符时安全退出。
- **Tool-call 增量聚合 (`src/tool_call_assembler.*`)**: 当模型分十几次返回某个函数调用的 arguments 字符串时，该模块能够将其缓存、聚合，并在结束时抛出一个完整、合法的 JSON 给宿主解析使用。
- **主交互网关 (`src/llm.*`)**: 将配置模块与网络模块桥接，对外提供单入口函数进行提问和响应拦截。

## 3. 对开发者的影响 / 接口 (Developer Impact)
可以通过实现类似如下流程来注册流式监听：
```cpp
void on_chunk(const std::string& diff) {
    // 实时打字机输出结果
}
```

## 4. 相关文件映射 (Relevant Files)
- [src/http.cpp](../src/http.cpp)
- [src/sse_parser.cpp](../src/sse_parser.cpp)
- [src/tool_call_assembler.cpp](../src/tool_call_assembler.cpp)
- [src/llm.cpp](../src/llm.cpp)
