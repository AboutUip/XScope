# Concurrency & multi-window

[中文](concurrency.zh-CN.md)

## Supported ways to work

Users may:

- Run **multiple tasks inside one process**
- **Detach a task into its own window**
- Start **multiple `exe` instances** and research in parallel

The same project may be opened by more than one process/window.

## Decision: window vs process

| Action | Default behavior | Rationale |
|--------|------------------|-----------|
| Drag/detach to an independent window | **Same process, new window** | Simpler sync, shared in-memory services, still feels like a separate workspace |
| Launch another `exe` / second instance | **New process** | True isolation for heavy jobs; uses DB locks + IPC |

UI-first: detaching a window should feel instant; forcing a new process for every detach is heavier and less stable.

## Same project, multiple writers

Policy: **any participant may write**; writers **queue** on the database lock.

- Prefer **short transactions**
- Accept that multi-process writes can be slower than single-process
- Do not design for high-frequency cross-process write storms; research workloads are bursty, not trading-system hot

SQLite (with a single writer at a time) is the source of truth for durable state.

## UI linkage (required)

When multiple windows/processes open the **same project**, they must stay in sync for user-visible state (open/save, task progress, structural changes).

Approach:

1. **Durable state** → encrypted SQLite (and files under the project directory)
2. **Live notifications** → local IPC event bus keyed by `project_id` (and workspace identity)

When that bus crosses a network/loopback boundary **and** drives UI rendering / information exchange, the payload **MUST** be **XAIOP** (streaming Snapshot/Diff). See [Streaming & XAIOP](streaming-xaiop.md).

Events are hints to refresh or apply small updates; the DB remains authoritative after conflicts or missed messages.

IPC mechanism is platform-specific behind a SDK/client portability layer (e.g. named pipes / local sockets on desktop). Exact transport is an implementation choice; the contract is “project-scoped XAIOP (when UI-bound) + reload from storage when unsure”.

## Practical rules

- Opening a project registers the process/window on that project’s bus
- Closing unregisters cleanly
- Workers should avoid long write transactions that stall other windows
- File writes under `files/` should use atomic replace patterns where practical
