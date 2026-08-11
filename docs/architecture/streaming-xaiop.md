# Streaming & XAIOP

[中文](streaming-xaiop.zh-CN.md)

## Rule (normative for XScope)

Use **XAIOP** when **both** are true:

1. The path involves **networking** (including localhost / loopback IPC that crosses process or host boundaries), **and**
2. The payload is for **client UI rendering and information exchange** (progress, results, events, structured state the UI must consume).

Goal: **streaming consumption** — Snapshot / Diff as data arrives, without waiting for a finished one-shot JSON tree.

If either condition is false, XAIOP is **not required** by this rule.

## Applies

| Example | Why |
|---------|-----|
| SDK / worker → Windows UI over localhost pipe/TCP/WS | Network + UI stream |
| Second `exe` → first window project updates | Network/loopback + UI sync |
| Detached window receiving live research phases | UI exchange over an in-process or local channel that is treated as the UI data plane* |

\*Same-process UI updates may use in-memory callbacks for latency; when the same logical stream is also exposed across process boundaries, that on-the-wire form **MUST** be XAIOP so both paths share one model.

## Does not apply (by this rule)

| Example | Why |
|---------|-----|
| Calling third-party search HTTP APIs | External protocols; adapt at the boundary; use XAIOP when the result feeds UI |

**AI exception (product rule):** all SDK AI results intended for client consumption (`catalog` / usable models / chat stream phases / errors) **MUST** be emitted as **XAIOP wire**, even before a UI is wired. Vendor SSE/JSON stops at the AI adapter.
| Encrypted SQLite read/write | Local storage, not a UI network stream |
| Downloading a large binary blob to `files/` | Bytes/file I/O; UI gets XAIOP metadata/progress instead |
| Pure SDK-internal module calls | No network + no UI wire |

## Layering

```
UI (WPF)  ←── XAIOP stream (Snapshot / Diff / phases) ──→  SDK / workers
                transport: pipe | TCP | WS | …
Persistence remains encrypted SQLite + project files (authoritative for durable state).
```

- **Transport** carries UTF-8 XAIOP text (reassemble complete lines before parse).
- **XAIOP** is the application data plane for UI-bound networked exchange.
- **Database** remains source of truth after missed events or restart (see [Concurrency](concurrency.md)).

Prefer **keyed / named-path** models for mutable UI rows (task ids, evidence ids), not anonymous array indexes. See XAIOP practice: keyed state modeling (reference checkout under `dev/XAIOP` when present).

## Research run phases (`meta.kind = research_run`)

Same-process poll via `xscope_research_poll_xaiop` returns XAIOP `wire` plus decoded `doc`.

**Current product stages:** requirements discovery then deep research (`requirements_locked` → `next_step` → search/knowledge/`clarify` → `synthesize`/`final`).

See [Research orchestrator](research.md).

## Protocol reference

XScope adopts sealed XAIOP wire **v0.6.0** semantics for this plane. Implementation may use the official Go/Node/… SDKs or an equivalent conforming parser; observable streaming behavior should match Snapshot + Diff consumption.
