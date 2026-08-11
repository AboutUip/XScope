# Storage & project layout

[中文](storage.zh-CN.md)

## Responsibilities

| Actor | Owns |
|-------|------|
| Client | Chooses a platform-safe `data_root` |
| SDK | Layout under that root; SQLite; secret encryption; migrations; safe APIs |

The SDK does **not** pick OS special folders.

## Workspace layout

```
data_root/
  global/
    global.db           # project index, encrypted secrets, meta
    master.key          # DPAPI-protected 32-byte AES key (Windows)
  projects/
    <project_id>/
      project.db        # per-project DB (minimal schema; grows later)
      files/            # attachments / snapshots / exports
  skills/
    <skill_id>/SKILL.md # file-based skills (see Skills module)
  registry/
    search_modules.json # search module registry (see Registry)
  prompts/
    chat_system.md      # prompt templates (see Prompts & MCP)
```

Rules:

1. One research project = one directory + one project database.
2. Project files stay under that directory.
3. Global API keys live in `global.db` (ciphertext), not in a single project.
4. Schema starts minimal (`user_version` + v1 tables) and grows via migrations.

## SQLite module

`xscope::storage::Database`:

- Parameterized SQL only
- WAL + busy timeout
- Recursive mutex; short transactions
- UTF-16 path open on Windows (non-ASCII `data_root` safe)

`xscope::storage::Workspace`:

- Opens global DB, creates projects, opens project DBs
- `put_secret` / `get_secret` (AES-256-GCM; key from DPAPI master key)
- `projects_history_xaiop()` — **UI-bound** project history as XAIOP wire (keyed by project id)

## Encryption (pragmatic v1)

| Asset | Protection |
|-------|------------|
| Master key file | Windows DPAPI (`CryptProtectData`) |
| API keys / secrets columns | AES-256-GCM under master key |
| DB files location | Client private `data_root` |

Full SQLCipher-style page encryption can be added later behind the same `Database` boundary without changing callers.

## UI + XAIOP

Project history and similar index reads that will cross a network/loopback boundary into the client UI **MUST** be exposed as XAIOP streams (see [Streaming & XAIOP](streaming-xaiop.md)). The workspace helper already encodes the project index that way for the Windows client to consume incrementally.
