# 提示词与 MCP（搜索模块）

[English](prompts-mcp.md)

## 精准度策略

XScope **优先调研精准度**，不为省 token 做有损压缩。

- 可用模块列表包含**完整**模块元数据与**完整** `SKILL.md` 文本
- 提示词注入与 MCP 工具结果**不得**摘要、截断或省略 skill 内容
- 永不注入秘密明文（仅 `secret_id` / `secret_configured`）

## 流程

```
list_usable_search_modules()
        │  完整模块 + 完整 skill raw
        ▼
prompts.render_chat_system()  ── 强制注入 {{search_modules}}
        │
        ▼
AI 对话（后续）
        │
        ├─ MCP list_search_modules      → 完整列表 JSON（内嵌 skill）
        ├─ MCP get_search_module_skill  → 单模块 + 完整 skill
        ├─ MCP github_oauth_*           → Device Flow / PAT / 状态
        ├─ MCP run_search               → GitHub REST / 博查 web|ai-search（完整 body）
        └─ MCP github_rest_get          → 已鉴权深挖 GET（完整 body）
```

## 模块

### `registry` 助手

- `list_usable_search_modules` — 已启用且 skill 有效；可选密钥门禁
- `format_usable_modules_for_prompt` — 无损文本块
- `usable_modules_to_json` — 无损 JSON

### `prompts`

- 根目录：`data_root/prompts/`
- 默认模板：`chat_system.md`（不存在则种子生成）
- 聊天系统提示词**必须**含 `{{search_modules}}`
- 客户端附加占位符：`PromptContext::extras`（`{{key}}`）

### `mcp`

工具：

| 名称 | 输入 | 输出 |
|------|------|------|
| `list_search_modules` | `{}` | 完整可用模块 JSON（含完整 skill raw/body/frontmatter） |
| `get_search_module_skill` | `{ "module_id": "..." }` | 单个可用模块 JSON（含完整 skill） |
| `github_oauth_start` | `{ "scope"?, "open_browser"? }` | `device_code`、`user_code`、`verification_uri`、`interval`… |
| `github_oauth_poll` | `{ "device_code" }` | pending / authorized / 错误；成功则写入 token |
| `github_oauth_status` | `{}` | connected + login/scope（永不返回 access_token） |
| `github_oauth_disconnect` | `{}` | 清除 `github.oauth` |
| `github_oauth_set_pat` | `{ "token", "scope"? }` | PAT 回退写入同一 secret id |
| `run_search` | `{ "module_id":"github"|"bocha", "endpoint", "q", ... }` | 完整搜索响应 JSON |
| `github_rest_get` | `{ "path", "query"? }` | `github_rest` GET 别名 |
| `github_rest` | `{ "path", "method"?, "query"?, "accept"?, "body"?, "decode_content"? }` | 完整 REST body |
| `github_rest_paginate` | `{ "path", "query"?, "per_page"?, "max_pages"? }` | 保留每一页；数组合并为 `items` |
| `github_resource` | `{ "resource", "owner"?, "repo"?, ..., "paginate"?, "decode_content"? }` | 命名调研读取 |
| `github_rest_catalog` | `{}` | 命名资源目录 |

`Workspace::search_tools()` 绑定 registry + skills + secrets + HTTP + GitHub OAuth。

另见 [OAuth 与 GitHub REST](oauth-github.zh-CN.md)。

## Workspace 助手

- `list_usable_search_modules(require_secret_present)`
- `render_chat_system_prompt(require_secret_present)`
- `search_tools(require_secret_present)`
