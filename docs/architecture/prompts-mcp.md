# Prompts & MCP (search modules)

[中文](prompts-mcp.zh-CN.md)

## Accuracy policy

XScope prioritizes **research precision** over token savings.

- Usable module lists include **complete** module metadata and **complete** `SKILL.md` text.
- Prompt injection and MCP tool results **must not** summarize, truncate, or omit skill content.
- Secrets values are never injected (only `secret_id` / `secret_configured` flags).

## Flow

```
list_usable_search_modules()
        │  full module + full skill raw
        ▼
prompts.render_chat_system()  ── mandatory {{search_modules}} injection
        │
        ▼
AI chat (future)
        │
        ├─ MCP list_search_modules      → full list JSON (skills embedded)
        ├─ MCP get_search_module_skill  → one module + full skill
        ├─ MCP github_oauth_*           → Device Flow / PAT / status
        ├─ MCP run_search               → GitHub REST / Bocha web|ai-search (full body)
        └─ MCP github_rest_get          → authenticated deep GET (full body)
```

## Modules

### `registry` helpers

- `list_usable_search_modules` — enabled + valid skill; optional secret gate
- `format_usable_modules_for_prompt` — lossless text block
- `usable_modules_to_json` — lossless JSON

### `prompts`

- Root: `data_root/prompts/`
- Default template: `chat_system.md` (seeded if missing)
- **Requires** `{{search_modules}}` placeholder for chat system render
- Client extras via `PromptContext::extras` (`{{key}}`)

### `mcp`

Tools:

| Name | Input | Output |
|------|-------|--------|
| `list_search_modules` | `{}` | Full usable modules JSON (each includes complete skill raw/body/frontmatter) |
| `get_search_module_skill` | `{ "module_id": "..." }` | One usable module JSON with complete skill |
| `github_oauth_start` | `{ "scope"?, "open_browser"? }` | `device_code`, `user_code`, `verification_uri`, `interval`, … |
| `github_oauth_poll` | `{ "device_code" }` | pending / authorized / errors; stores token on success |
| `github_oauth_status` | `{}` | connected + login/scope (never access_token) |
| `github_oauth_disconnect` | `{}` | clears `github.oauth` |
| `github_oauth_set_pat` | `{ "token", "scope"? }` | PAT fallback into the same secret id |
| `run_search` | `{ "module_id":"github"|"bocha", "endpoint", "q", ... }` | Complete search response JSON |
| `github_rest_get` | `{ "path", "query"? }` | Alias of `github_rest` GET |
| `github_rest` | `{ "path", "method"?, "query"?, "accept"?, "body"?, "decode_content"? }` | Complete REST body |
| `github_rest_paginate` | `{ "path", "query"?, "per_page"?, "max_pages"? }` | All pages kept; arrays combined into `items` |
| `github_resource` | `{ "resource", "owner"?, "repo"?, ..., "paginate"?, "decode_content"? }` | Named research reads |
| `github_rest_catalog` | `{}` | Catalog of named resources |
| `knowledge_graph_get` | `{ "project_id" }` | Full project knowledge graph (nodes + edges) |
| `knowledge_graph_add` | `{ "project_id", "title", "content"?, "valid"?, ... }` | Add node; skips when `valid=false` |
| `knowledge_graph_update` | `{ "project_id", "id", ... }` | Update node |
| `knowledge_graph_delete` | `{ "project_id", "node_id"? , "edge_id"? }` | Delete node (and incident edges) or edge |
| `knowledge_graph_link` | `{ "project_id", "from_id", "to_id", "relation"? }` | Create association edge |
| `knowledge_graph_catalog` | `{ "project_id" }` | Knowledge table **directory** (no bodies) |
| `memory_catalog` | `{ "project_id" }` | Radiating-tree memory **directory** (no bodies) |
| `memory_get` | `{ "project_id", "id" }` | One memory entry with body |
| `memory_chain` | `{ "project_id", "id" }` | Full chain root→tip for a tip id |
| `memory_add` | `{ "project_id", "title", "body"?, "branch_id"?, ... }` | Append stage memory |
| `memory_branch_create` | `{ "project_id", "title", "parent_branch_id"? }` | Create side-path branch |
| `memory_branch_list` | `{ "project_id" }` | List branches |

`Workspace::search_tools()` binds registry + skills + secrets + HTTP + GitHub OAuth + project DB opener.

See also [OAuth & GitHub REST](oauth-github.md).

## Workspace helpers

- `list_usable_search_modules(require_secret_present)`
- `render_chat_system_prompt(require_secret_present)`
- `search_tools(require_secret_present)`
