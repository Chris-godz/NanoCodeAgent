# 第 1 章：项目概览

## 什么是 NanoCodeAgent？

NanoCodeAgent 是一个**教学型 AI Code Agent**，使用 C++ 实现，展示了如何构建一个
能够理解自然语言指令、调用工具、操作文件系统的 AI 编码助手的核心机制。

## 项目特性

- 🤖 **LLM 集成**：通过 HTTP 调用 OpenAI 兼容的 LLM API
- 🔧 **工具调用**：实现 Function Calling / Tool Use 协议
- 📁 **文件操作**：读写工作区文件，支持相对路径安全校验
- 💻 **Shell 工具**：在隔离环境中执行 Bash 命令
- 📝 **SSE 解析**：支持流式输出（Server-Sent Events）

## 目录结构

```
NanoCodeAgent/
├── src/                # 源代码
│   ├── main.cpp        # 程序入口
│   ├── llm.cpp         # LLM API 调用
│   ├── http.cpp        # HTTP 客户端
│   ├── bash_tool.cpp   # Shell 工具
│   ├── read_file.cpp   # 文件读取工具
│   ├── write_file.cpp  # 文件写入工具
│   ├── config.cpp      # 配置解析
│   ├── logger.cpp      # 日志系统
│   └── ...
├── include/            # 头文件
├── tests/              # 测试
├── book/               # 本书（mdBook）
├── tools/docgen/       # AI 文档生成脚本
└── CMakeLists.txt      # 构建配置
```

## 快速开始

请参阅[第 2 章：环境搭建与构建](./02-setup.md)。

<!-- TODO: 补充版本历史和贡献指南 -->
