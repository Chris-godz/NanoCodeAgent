# 第 2 章：环境搭建与构建

## 依赖要求

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| C++ 编译器 | GCC 11+ 或 Clang 14+ | 需要 C++23 支持 |
| CMake | 3.20+ | 构建系统 |
| libcurl | 7.x+ | HTTP 客户端 |
| nlohmann/json | 3.x | JSON 解析（已内置为子模块） |

## 克隆仓库

```bash
git clone --recurse-submodules https://github.com/Chris-godz/NanoCodeAgent.git
cd NanoCodeAgent
```

## 本地构建

使用项目提供的构建脚本：

```bash
./build.sh
```

或手动使用 CMake：

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## 运行

```bash
./build/agent --config my_config.ini
```

配置文件示例请参阅[第 8 章：配置与日志系统](./08-config-logging.md)。

## 构建在线文档（本书）

```bash
# 安装 mdBook
cargo install mdbook

# 本地预览
mdbook serve book -p 3000
# 浏览器打开 http://localhost:3000
```

<!-- TODO: 补充 Docker 一键启动说明 -->
