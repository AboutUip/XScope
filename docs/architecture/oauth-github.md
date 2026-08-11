# OAuth & GitHub REST

[中文](oauth-github.zh-CN.md)

## Goal

Connect **GitHub** as a first-class search module using **OAuth Device Flow** (PAT fallback), then call **GitHub REST** with full-fidelity responses (no truncation for token savings).

XScope still has **no product account** — only third-party provider connections stored as encrypted secrets under the client `data_root`.

## Operator setup

1. Create a GitHub **OAuth App** (or GitHub App).
2. Enable **Device Flow** in the app settings (required).
3. Provide the client id to XScope:
   - Env: `XSCOPE_GITHUB_OAUTH_CLIENT_ID`
   - Optional: `XSCOPE_GITHUB_OAUTH_CLIENT_SECRET`, `XSCOPE_GITHUB_OAUTH_SCOPE`
   - Or file: `data_root/global/github_oauth.json`

```json
{
  "client_id": "Ov23li...",
  "client_secret": "",
  "scope": "read:user",
  "secret_id": "github.oauth"
}
```

Default scope is `read:user`. Request broader scopes (e.g. `repo`) when private repositories are needed. **Code search always requires a connected token.**

## User flow

```
github_oauth_start  →  browser + user_code
        │
        ▼
github_oauth_poll (loop)  →  encrypted secret github.oauth
        │
        ▼
run_search / github_resource / github_rest(_paginate)  →  complete bodies
```

PAT fallback: `github_oauth_set_pat`. Disconnect: `github_oauth_disconnect`.

## REST surface

| Capability | How |
|------------|-----|
| Search | `run_search` (`repositories/code/issues/…`) |
| Named reads | `github_resource` (`repo`, `contents`, `raw`, `git_tree`, `issue`, `pull_files`, …) |
| Arbitrary path | `github_rest` / `github_rest_get` (`method`, `path`, `accept`, `body`) |
| Pagination | `github_rest_paginate` or `github_resource` + `paginate=true` (keeps every page) |
| Catalog | `github_rest_catalog` |
| Base64 files | `decode_content` → `decoded_content` (full text) |

Accept overrides supported (e.g. `application/vnd.github.raw`, diff/patch media types).

## SDK pieces

| Piece | Role |
|-------|------|
| `auth::DeviceFlowClient` | RFC 8628 over HTTP |
| `auth::GithubOAuth` | GitHub device flow + secret persistence + `/user` login hint |
| `auth::TokenSet` | JSON payload in encrypted secrets (access/refresh/expiry/scope) |
| `providers::github::RestClient` | Search, named resources, Link pagination, Accept overrides; full bodies |
| `providers::github::ensure_github_search_module` | Seeds skill `github-search` + registry id `github` (`auth.type=oauth`) |
| MCP tools | See [Prompts & MCP](prompts-mcp.md) |

## Registry module

```json
{
  "id": "github",
  "skill_id": "github-search",
  "requires_api_key": true,
  "auth": {
    "type": "oauth",
    "secret_id": "github.oauth",
    "param_name": "Authorization"
  },
  "tags": ["github", "code", "oauth"]
}
```

Seeded automatically on `Workspace::open`.

## Accuracy policy

- Search and REST tools return **complete** response bodies.
- Skill text for `github-search` is injected and listed in full.
- Rate-limit / `Link` / `incomplete_results` are preserved for the researcher.
- Access tokens never appear in MCP status payloads.
