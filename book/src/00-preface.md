# 前言

欢迎阅读 **NanoCodeAgent 教学手册**。

本书面向希望深入理解 AI 驱动的代码智能体（Code Agent）工程实现的开发者。
NanoCodeAgent 是一个教学型项目，用 C++ 实现了一个轻量级的 AI 编码助手，
涵盖 LLM API 调用、工具调用协议（Tool Calling）、文件系统操作和 Shell 沙盒等核心功能。

## 本书结构

| 章节 | 主题 |
|------|------|
| 第 1 章 | 项目整体功能与目录结构 |
| 第 2 章 | 本地环境搭建与编译运行 |
| 第 3 章 | 核心架构与模块依赖关系 |
| 第 4 章 | 工具调用协议实现 |
| 第 5 章 | HTTP 客户端与 LLM 推理集成 |
| 第 6 章 | 读写文件工具实现 |
| 第 7 章 | Bash 工具与安全隔离 |
| 第 8 章 | 配置文件与日志系统 |

## 如何使用本书

- 在线阅读：访问项目 GitHub Pages（在仓库 Settings -> Pages 启用）
- 本地构建：安装 [mdBook](https://rust-lang.github.io/mdBook/)，然后执行 `mdbook serve book -p 3000`

> **提示**：本书文档由 AI 自动维护（`tools/docgen/docgen.py`），
> 每次代码变更后会自动更新相关章节并通过 PR 提交。
