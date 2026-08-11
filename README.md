# XScope

Local-first **intelligent network research** desktop toolkit for Windows.

[中文](README.zh-CN.md)

## What it is

XScope turns a research question into a guided workflow:

1. **Requirements discovery** — clarify the need (optional search + choices)
2. **Deep research** — route modules, dig by depth layers, collect evidence
3. **Report** — Markdown synthesis shown in the client (with live right-rail feed)

The **C++ SDK** owns the engine (orchestration, MCP tools, storage, GitHub/Bocha, AI).  
The **Windows WPF client** is the product UI (projects, settings, research shell).

| Area | Path | Role |
|------|------|------|
| Core SDK | [`sdk/`](sdk/) | Research engine, C API (`xscope_capi`), embedded XAIOP |
| Windows client | [`clients/windows/`](clients/windows/) | WPF UI / UX |
| Docs | [`docs/`](docs/) | Architecture & setup |

## Features

- **Two-stage research** — lock requirements, then deep research ([details](docs/architecture/research.md))
- **Precision modes** — Quick / Normal / Deep / Maximum (depth-layer budgets)
- **Search modules** — GitHub (OAuth device flow / PAT + REST) and Bocha web search
- **AI providers** — OpenAI-compatible chat (e.g. DeepSeek, Kimi) via settings
- **Project library** — local projects, pin / rename / delete
- **Knowledge graph + stage memory tree** — catalogs to the model; bodies on demand; 3D insights rail in the client
- **XAIOP streaming** — live phases for thinking, keywords, evidence, report
- **Appearance** — Auto / Light / Dark (X-style dark chrome)
- **Report export** — Markdown / PDF / Word from the finished report (client-side)

Runtime data lives under `%LocalAppData%\XScope\data` (secrets encrypted; not in git).

## UI stack

Native **WPF** + **Material Design** (MaterialDesignThemes). Report Markdown is rendered in-app (MdXaml). Shell UI is not a WebView.

## Requirements

- Windows 10 / 11 (x64)
- [.NET 9 SDK](https://dotnet.microsoft.com/download)
- Visual Studio 2022+ with **Desktop C++** / MSVC, **CMake**, and **Ninja**
- [CMake](https://cmake.org/) **3.20+**
- [Go](https://go.dev/) on `PATH` (builds embedded `xaiop_native` during SDK configure)

## Quick start

Build the **SDK first**, then the client (the WPF project copies `xscope_capi.dll` / `xaiop_native.dll` next to the app).

### 1. SDK (`x64-debug`)

```powershell
cd sdk
cmake --preset x64-debug
cmake --build out/build/x64-debug --target xscope_capi
```

Presets: see [`sdk/CMakePresets.json`](sdk/CMakePresets.json).  
Also available: Visual Studio → Open Folder on `sdk/` → preset **x64 Debug**.

### 2. Windows client

```powershell
cd clients/windows
dotnet restore
dotnet build -c Debug -p:Platform=x64
dotnet run -c Debug -p:Platform=x64
```

Or open [`clients/windows/XScope.sln`](clients/windows/XScope.sln) in Visual Studio (platform **x64**).

### First-run setup (in app)

1. **Settings → AI** — set provider API key and preferred model  
2. **Settings → GitHub** — device login or paste a PAT (needed for GitHub research)  
3. **Settings → Search modules** — enable Bocha and set API key if you use web search  

## Repository layout

```text
XScope/
├── clients/windows/     # WPF app (net9.0-windows, x64)
├── packaging/windows/   # MSI build (WiX + publish script)
├── sdk/                 # C++20 engine + C API + deps (sqlite, xaiop_native)
├── docs/                # Architecture & client notes (EN + zh-CN)
├── README.md
├── README.zh-CN.md
└── LICENSE
```

## Documentation

Start at [`docs/README.md`](docs/README.md) (中文: [`docs/README.zh-CN.md`](docs/README.zh-CN.md)).

| Topic | Doc |
|-------|-----|
| Architecture overview | [overview](docs/architecture/overview.md) |
| Research orchestrator | [research](docs/architecture/research.md) |
| Storage | [storage](docs/architecture/storage.md) |
| Streaming & XAIOP | [streaming-xaiop](docs/architecture/streaming-xaiop.md) |
| Network | [network](docs/architecture/network.md) |
| OAuth & GitHub REST | [oauth-github](docs/architecture/oauth-github.md) |
| Bocha | [bocha](docs/architecture/bocha.md) |
| AI module | [ai](docs/architecture/ai.md) |
| Prompts & MCP | [prompts-mcp](docs/architecture/prompts-mcp.md) |
| Skills / Registry / Utils | [skills](docs/architecture/skills.md) · [registry](docs/architecture/registry.md) · [utils](docs/architecture/utils.md) |
| Concurrency / Security | [concurrency](docs/architecture/concurrency.md) · [security](docs/architecture/security.md) |
| Windows client | [client/windows](docs/client/windows.md) |
| Windows MSI packaging | [packaging/windows](docs/packaging/windows.md) |
| SDK | [sdk/overview](docs/sdk/overview.md) |

## License

[MIT](LICENSE) © 2026 小萱baibai
