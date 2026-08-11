# OAuth 与 GitHub REST

[English](oauth-github.md)

## 目标

将 **GitHub** 作为一等搜索模块接入：**OAuth Device Flow**（PAT 回退），并通过 **GitHub REST** 返回**完整**响应（不为省 token 截断）。

XScope **仍然没有产品账号**——只有第三方连接，凭证加密存在客户端 `data_root` 下。

## 运营侧配置

1. 创建 GitHub **OAuth App**（或 GitHub App）。
2. 在应用设置中启用 **Device Flow**（必须）。
3. 向 XScope 提供 client id：
   - 环境变量：`XSCOPE_GITHUB_OAUTH_CLIENT_ID`
   - 可选：`XSCOPE_GITHUB_OAUTH_CLIENT_SECRET`、`XSCOPE_GITHUB_OAUTH_SCOPE`
   - 或文件：`data_root/global/github_oauth.json`

```json
{
  "client_id": "Ov23li...",
  "client_secret": "",
  "scope": "read:user",
  "secret_id": "github.oauth"
}
```

默认 scope 为 `read:user`。需要私有仓库时再申请更广权限（如 `repo`）。**代码搜索始终要求已连接 token。**

## 用户流程

```
github_oauth_start  →  浏览器 + user_code
        │
        ▼
github_oauth_poll（轮询）→  加密 secret github.oauth
        │
        ▼
run_search / github_resource / github_rest(_paginate)  →  完整 body
```

PAT 回退：`github_oauth_set_pat`。断开：`github_oauth_disconnect`。

## REST 能力面

| 能力 | 方式 |
|------|------|
| 搜索 | `run_search`（`repositories/code/issues/…`） |
| 命名读取 | `github_resource`（`repo`、`contents`、`raw`、`git_tree`、`issue`、`pull_files`…） |
| 任意路径 | `github_rest` / `github_rest_get`（`method`、`path`、`accept`、`body`） |
| 分页 | `github_rest_paginate` 或 `github_resource` + `paginate=true`（保留每一页） |
| 目录 | `github_rest_catalog` |
| Base64 文件 | `decode_content` → `decoded_content`（全文） |

支持 Accept 覆盖（如 `application/vnd.github.raw`、diff/patch 媒体类型）。

## SDK 组件

| 组件 | 职责 |
|------|------|
| `auth::DeviceFlowClient` | HTTP 上的 RFC 8628 |
| `auth::GithubOAuth` | GitHub device flow + secret 持久化 + `/user` 登录名 |
| `auth::TokenSet` | 加密 secret 中的 JSON（access/refresh/expiry/scope） |
| `providers::github::RestClient` | 搜索、命名资源、Link 分页、Accept 覆盖；完整 body |
| `providers::github::ensure_github_search_module` | 种子 skill `github-search` + 注册表 id `github`（`auth.type=oauth`） |
| MCP tools | 见 [Prompts & MCP](prompts-mcp.zh-CN.md) |

## 注册表模块

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

在 `Workspace::open` 时自动种子化。

## 精准度策略

- 搜索与 REST 工具返回**完整**响应体。
- `github-search` 的 skill 全文注入与列表返回。
- 保留限流 / `Link` / `incomplete_results` 供调研使用。
- MCP 状态载荷中**永不**返回 access token。
