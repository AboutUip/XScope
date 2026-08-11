# XScope

面向 Windows 的本地优先**智能网络调研**桌面工具。

[English](README.md)

## 是什么

XScope 把一个调研问题变成可引导的工作流：

1. **需求发现** — 澄清需求（可选搜索 + 选项卡）
2. **深度调研** — 路由模块、按深度层挖掘、收集证据
3. **报告** — 客户端展示 Markdown 综合报告（右侧实时动态）

**C++ SDK** 负责引擎（编排、MCP 工具、存储、GitHub/Bocha、AI）。  
**Windows WPF 客户端** 是产品界面（项目库、设置、调研壳层）。

| 区域 | 路径 | 职责 |
|------|------|------|
| 核心 SDK | [`sdk/`](sdk/) | 调研引擎、C API（`xscope_capi`）、内嵌 XAIOP |
| Windows 客户端 | [`clients/windows/`](clients/windows/) | WPF UI / UX |
| 文档 | [`docs/`](docs/) | 架构与搭建说明 |

## 功能概览

- **两阶段调研** — 锁定需求后进入深度调研（[说明](docs/architecture/research.zh-CN.md)）
- **精度档位** — 快 / 普 / 深 / 最大（限制单方向深度层数）
- **搜索模块** — GitHub（设备码登录 / PAT + REST）与 Bocha 网页搜索
- **AI 提供商** — OpenAI 兼容对话（如 DeepSeek、Kimi），在设置中配置
- **项目库** — 本地项目，支持置顶 / 重命名 / 删除
- **知识关联图 + 阶段记忆树** — 向模型提供目录，正文按需读取；客户端三维洞察侧栏
- **XAIOP 流式阶段** — 思考、关键词、证据、报告实时推送
- **外观** — 自动 / 浅色 / 深色（X 风格深色铬）
- **报告导出** — 完成后支持 Markdown / PDF / Word（客户端转换）

运行时数据在 `%LocalAppData%\XScope\data`（密钥加密存放；不进 git）。

## UI 技术栈

原生 **WPF** + **Material Design**（MaterialDesignThemes）。报告 Markdown 在应用内渲染（MdXaml）。主界面不使用 WebView。

## 环境要求

- Windows 10 / 11（x64）
- [.NET 9 SDK](https://dotnet.microsoft.com/download)
- Visual Studio 2022+（含 **Desktop C++** / MSVC、**CMake**、**Ninja**）
- [CMake](https://cmake.org/) **3.20+**
- [Go](https://go.dev/) 在 `PATH` 中（配置 SDK 时编译内嵌 `xaiop_native`）

## 快速开始

请先编译 **SDK**，再编译客户端（WPF 工程会把 `xscope_capi.dll` / `xaiop_native.dll` 复制到输出目录）。

### 1. SDK（`x64-debug`）

```powershell
cd sdk
cmake --preset x64-debug
cmake --build out/build/x64-debug --target xscope_capi
```

预设见 [`sdk/CMakePresets.json`](sdk/CMakePresets.json)。  
也可：Visual Studio → 打开 `sdk/` 文件夹 → 选择预设 **x64 Debug**。

### 2. Windows 客户端

```powershell
cd clients/windows
dotnet restore
dotnet build -c Debug -p:Platform=x64
dotnet run -c Debug -p:Platform=x64
```

或用 Visual Studio 打开 [`clients/windows/XScope.sln`](clients/windows/XScope.sln)（平台选 **x64**）。

### 首次使用（应用内）

1. **设置 → AI** — 填写提供商 API Key 与首选模型  
2. **设置 → GitHub** — 设备码登录或粘贴 PAT（GitHub 调研需要）  
3. **设置 → 搜索模块** — 启用 Bocha 并配置 Key（若使用网页搜索）  

## 仓库结构

```text
XScope/
├── clients/windows/     # WPF 应用（net9.0-windows，x64）
├── packaging/windows/   # MSI 构建（WiX + 发布脚本）
├── sdk/                 # C++20 引擎 + C API + 依赖（sqlite、xaiop_native）
├── docs/                # 架构与客户端说明（中英）
├── README.md
├── README.zh-CN.md
└── LICENSE
```

## 文档

从 [`docs/README.zh-CN.md`](docs/README.zh-CN.md) 开始（English: [`docs/README.md`](docs/README.md)）。

| 主题 | 文档 |
|------|------|
| 架构概览 | [overview](docs/architecture/overview.zh-CN.md) |
| 调研编排 | [research](docs/architecture/research.zh-CN.md) |
| 存储 | [storage](docs/architecture/storage.zh-CN.md) |
| 流式与 XAIOP | [streaming-xaiop](docs/architecture/streaming-xaiop.zh-CN.md) |
| 网络 | [network](docs/architecture/network.zh-CN.md) |
| OAuth 与 GitHub REST | [oauth-github](docs/architecture/oauth-github.zh-CN.md) |
| Bocha | [bocha](docs/architecture/bocha.zh-CN.md) |
| AI 模块 | [ai](docs/architecture/ai.zh-CN.md) |
| Prompts & MCP | [prompts-mcp](docs/architecture/prompts-mcp.zh-CN.md) |
| Skills / Registry / Utils | [skills](docs/architecture/skills.zh-CN.md) · [registry](docs/architecture/registry.zh-CN.md) · [utils](docs/architecture/utils.zh-CN.md) |
| 并发 / 安全 | [concurrency](docs/architecture/concurrency.zh-CN.md) · [security](docs/architecture/security.zh-CN.md) |
| Windows 客户端 | [client/windows](docs/client/windows.zh-CN.md) |
| Windows MSI 打包 | [packaging/windows](docs/packaging/windows.zh-CN.md) |
| SDK | [sdk/overview](docs/sdk/overview.zh-CN.md) |

## 许可

[MIT](LICENSE) © 2026 小萱baibai
