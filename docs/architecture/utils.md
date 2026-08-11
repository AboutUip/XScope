# Utils module

[中文](utils.zh-CN.md)

## Role

`utils` is the SDK’s **internal general-purpose** toolbox. Other modules (`storage`, `skills`, `network`, …) should reuse it instead of copying helpers.

It is intentionally small and boring: string / path / time primitives only. Business logic does **not** belong here.

## Packages

| Header | Contents |
|--------|----------|
| `xscope/utils/string.hpp` | `trim_copy`, `strip_quotes`, `json_escape`, `sanitize_id`, `split_lines`, `starts_with`, `ends_with` |
| `xscope/utils/path.hpp` | `path_to_utf8`, `ensure_directory`, `read_file_utf8`, `write_file_utf8` |
| `xscope/utils/time.hpp` | `now_unix_seconds`, `file_mtime_unix_seconds` |
| `xscope/utils/json.hpp` | Minimal JSON parse/dump (`null`/`bool`/`number`/`string`/`array`/`object`) |
| `xscope/utils/utils.hpp` | Umbrella include |

## Rules

1. Keep helpers pure and side-effect free where possible (I/O helpers are the exception and stay thin).
2. Do not depend on SQLite, WinHTTP, XAIOP, or crypto from `utils`.
3. Prefer extending `utils` over duplicating the same 10-line helper in a feature module.
4. Prefer new focused headers over a single giant “misc.cpp”.
