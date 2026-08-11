# SDK 概览

[English](overview.md)

路径：`sdk/`  
语言：C++20（模块化）+ 内嵌 Go XAIOP  
构建：CMake + CMake Presets（Windows 上 Ninja / MSVC）

## 职责

SDK 是 XScope 的**核心**：

- 低耦合的模块化能力
- 加密秘密存储 + SQLite 持久化（路径由客户端注入）
- 内嵌 **XAIOP**（Go SDK，经 `xaiop_native`）服务面向 UI 的网络流
- 通用 **network** 模块（HTTP + 可选 XAIOP），供 MCP / AI / 调研适配依赖
- 调研编排（随时间扩展）

## 布局

```
sdk/
  include/xscope/     公共头文件
  src/                storage / crypto / network / xaiop / 冒烟入口
  deps/sqlite/        SQLite amalgamation
  deps/xaiop_native/  cgo 桥 → Go XAIOP（构建产出 xaiop_native.dll）
```

## 模块（当前）

| 模块 | 职责 |
|------|------|
| `storage` | SQLite 封装、迁移、`Workspace`（`data_root`、全局库 + 项目库） |
| `crypto` | DPAPI 包装的主密钥 + AES-256-GCM（API Key / 秘密） |
| `network` | 通用 WinHTTP 客户端、取消、流式读；可选 `enable_xaiop` 会话 |
| `skills` | 文件型 skill 仓库（`SKILL.md`）；安装/加载/删除；`catalog_xaiop()` |
| `utils` | 内部字符串 / 路径 / 时间 / JSON / URL 助手，供其他模块复用 |
| `auth` | OAuth Device Flow + token 秘密持久化（`GithubOAuth`） |
| `providers/github` | GitHub REST 搜索/GET + 内置 skill/注册表种子 |
| `registry` | 搜索模块 JSON 注册表；可用列表含完整 skill 文本 |
| `prompts` | 提示词引擎；强制完整注入 `{{search_modules}}` |
| `mcp` | 搜索 list/skill + GitHub OAuth + `run_search` / `github_rest_get` |
| `ai` | 提供商/模型注册表 + OpenAI 兼容运行时；**AI 返回一律为 XAIOP** |
| `capi` | 共享 `xscope_capi.dll` C ABI（供 WPF P/Invoke：工作区 + GitHub OAuth） |
| `xaiop` | 加载内嵌 Go XAIOP；encode/parse/LiveParser 服务 UI 流 |

## XAIOP 内嵌

- 源码依赖：将 [XAIOP](https://github.com/AboutUip/XAIOP) 克隆到 `dev/XAIOP`（已 gitignore），或设置 `XSCOPE_XAIOP_GO_SRC`
- CMake 目标 `xaiop_native` 对 `deps/xaiop_native` 执行 `go build -buildmode=c-shared`
- 运行时：`xaiop_native.dll` 复制到 `XScope.exe` 旁

面向 UI 的历史（及类似索引查询）通过 `Workspace::projects_history_xaiop()` 输出为 **XAIOP 线文本** — 见 [流式与 XAIOP](../architecture/streaming-xaiop.zh-CN.md)。

## 构建（Windows）

需要：Visual Studio 2022 C++ 工具、CMake、Ninja、Go 1.22+ 且开启 CGO（PATH 上有 `gcc`，如 Strawberry）。

```powershell
cd sdk
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\XScope.exe
```

## 与客户端的边界

- 客户端提供 `data_root` 并拥有 UI
- SDK 拥有持久化格式、秘密封装，以及面向 UI 流的 XAIOP 编码
- 客户端不直连 SQLite
