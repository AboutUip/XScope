# Architecture overview

[中文](overview.zh-CN.md)

## Product goal

XScope is a local **intelligent network research** toolkit. Users create research **projects**, run tasks, and keep evidence on disk. There is no account/login and no hosted network database.

## System layout

```
┌─────────────────────────────────────┐
│  clients/windows (WPF, UI-first)    │
└─────────────────┬───────────────────┘
                  │ thin interop boundary
┌─────────────────▼───────────────────┐
│  sdk/ (modular C++)                 │
│  orchestration → capabilities       │
│  → storage / crypto / platform      │
└─────────────────────────────────────┘
```

- **`sdk/`** — Core engine. Modular, low coupling. Owns research logic, encrypted storage usage, and future workers.
- **`clients/windows/`** — Primary product surface. UI feel comes first. Chooses OS-safe `data_root` and presents workflows.
- Other clients (e.g. Android later) may host the same SDK with a different UI shell.

## Design pillars

1. **Modular SDK** — capabilities evolve independently; no god-object core.
2. **UI-first client** — Windows WPF + Material Design 3; no WebView for shell UI.
3. **Local-only data** — client supplies `data_root`; one project = one directory tree + encrypted DB.
4. **Extensible workspace** — many projects under one root; schema starts minimal and grows.
5. **Multi-window / multi-process** — stable locking + IPC sync when the same project is open more than once.
6. **Pragmatic security** — encrypt DB and secrets with common platform practices; not extreme, not careless.
7. **XAIOP for UI-bound network streams** — when traffic is networked **and** feeds client UI rendering / information exchange, use XAIOP for streaming Snapshot/Diff (see [Streaming & XAIOP](streaming-xaiop.md)).
8. **Generic network module** — shared HTTP transport for MCP / AI / research; optional `enable_xaiop` plane (see [Network](network.md)).
9. **File-based skills** — manage `SKILL.md` trees under `data_root/skills` (see [Skills](skills.md)).
10. **Internal utils** — shared string/path/time/json helpers (see [Utils](utils.md)).
11. **Search module registry** — JSON catalog of search providers + skill + auth (see [Registry](registry.md)).
12. **Prompts + MCP** — full-fidelity usable modules injected into prompts; MCP tools return complete skills (see [Prompts & MCP](prompts-mcp.md)).
13. **OAuth + GitHub REST** — Device Flow (PAT fallback) and full-fidelity GitHub search/GET (see [OAuth & GitHub REST](oauth-github.md)).
14. **AI providers** — registry-driven OpenAI-compatible models (Kimi / DeepSeek); **AI returns are always XAIOP** (see [AI](ai.md)).

## Related docs

- [Storage](storage.md)
- [Concurrency](concurrency.md)
- [Security](security.md)
- [Streaming & XAIOP](streaming-xaiop.md)
- [Network](network.md)
- [Skills](skills.md)
- [Utils](utils.md)
- [Registry](registry.md)
- [Prompts & MCP](prompts-mcp.md)
- [OAuth & GitHub REST](oauth-github.md)
- [AI](ai.md)
- [SDK overview](../sdk/overview.md)
