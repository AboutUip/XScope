# SDK overview

[中文](overview.zh-CN.md)

Path: `sdk/`  
Language: C++20 (modular) + embedded Go XAIOP  
Build: CMake + CMake Presets (Ninja / MSVC on Windows)

## Role

The SDK is the **core** of XScope:

- Modular capabilities with low coupling
- Encrypted secret storage + SQLite persistence (paths injected by the client)
- Embedded **XAIOP** (Go SDK via `xaiop_native`) for UI-bound networked streams
- Generic **network** module (HTTP + optional XAIOP) for MCP / AI / research adapters
- Research orchestration (grows over time)

## Layout

```
sdk/
  include/xscope/     Public headers
  src/                storage / crypto / network / xaiop / smoke main
  deps/sqlite/        SQLite amalgamation
  deps/xaiop_native/  cgo bridge → Go XAIOP (build → xaiop_native.dll)
```

## Modules (current)

| Module | Role |
|--------|------|
| `storage` | SQLite wrapper, migrations, `Workspace` (`data_root`, global + project DBs) |
| `crypto` | DPAPI-wrapped master key + AES-256-GCM for API keys / secrets |
| `network` | Generic WinHTTP client, cancel, streaming; optional `enable_xaiop` session |
| `skills` | File-based skill store (`SKILL.md`); install/load/remove; `catalog_xaiop()` |
| `utils` | Internal string / path / time / JSON / URL helpers shared by other modules |
| `registry` | Search module JSON registry; usable list with full skill text |
| `prompts` | Template engine; mandatory full `{{search_modules}}` injection |
| `auth` | OAuth Device Flow + token secret persistence (`GithubOAuth`) |
| `providers/github` | GitHub REST search/GET + builtin skill/registry seed |
| `mcp` | Search list/skill + GitHub OAuth + `run_search` / `github_rest_get` |
| `ai` | Provider/model registry + OpenAI-compatible runtime; **all AI returns are XAIOP** |
| `capi` | Shared `xscope_capi.dll` C ABI for WPF P/Invoke (workspace + GitHub OAuth) |
| `xaiop` | Loads embedded Go XAIOP; encode/parse/LiveParser for UI streams |

## XAIOP embed

- Source dependency: clone [XAIOP](https://github.com/AboutUip/XAIOP) to `dev/XAIOP` (gitignored), or set `XSCOPE_XAIOP_GO_SRC`
- CMake target `xaiop_native` builds `deps/xaiop_native` with `go build -buildmode=c-shared`
- Runtime: `xaiop_native.dll` is copied next to `XScope.exe`

UI-facing history (and similar index queries) is emitted as **XAIOP wire** via `Workspace::projects_history_xaiop()` — see [Streaming & XAIOP](../architecture/streaming-xaiop.md).

## Build (Windows)

Requires: Visual Studio 2022 C++ tools, CMake, Ninja, Go 1.22+ with CGO (`gcc` on PATH, e.g. Strawberry).

```powershell
cd sdk
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\XScope.exe
```

## Boundary with clients

- Client supplies `data_root` and owns UI
- SDK owns durable data formats, secret sealing, and XAIOP encode for UI-bound streams
- Clients never open SQLite directly
