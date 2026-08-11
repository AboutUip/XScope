# Utils 模块

[English](utils.md)

## 职责

`utils` 是 SDK 的**内部通用工具箱**。其他模块（`storage`、`skills`、`network` 等）应复用它，而不是各自复制一份助手函数。

它刻意保持小而无聊：仅字符串 / 路径 / 时间原语。**业务逻辑不属于这里。**

## 分包

| 头文件 | 内容 |
|--------|------|
| `xscope/utils/string.hpp` | `trim_copy`、`strip_quotes`、`json_escape`、`sanitize_id`、`split_lines`、`starts_with`、`ends_with` |
| `xscope/utils/path.hpp` | `path_to_utf8`、`ensure_directory`、`read_file_utf8`、`write_file_utf8` |
| `xscope/utils/time.hpp` | `now_unix_seconds`、`file_mtime_unix_seconds` |
| `xscope/utils/json.hpp` | 最小 JSON 解析/序列化（`null`/`bool`/`number`/`string`/`array`/`object`） |
| `xscope/utils/utils.hpp` | 汇总包含 |

## 规则

1. 助手尽量纯、少副作用（I/O 助手除外，且保持很薄）。
2. `utils` 不依赖 SQLite、WinHTTP、XAIOP 或 crypto。
3. 优先扩展 `utils`，避免在功能模块里再复制同一段 10 行代码。
4. 优先新增聚焦头文件，而不是堆一个巨大的 `misc.cpp`。
