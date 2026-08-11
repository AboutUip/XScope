# Bocha search provider

[中文](bocha.zh-CN.md)

Builtin search module `id=bocha` (skill `bocha-search`).

## Auth

- Type: Bearer API key
- Secret id: `bocha.default`
- Header: `Authorization: Bearer <key>`
- Keys: [open.bocha.cn](https://open.bocha.cn)

C API: `xscope_search_module_set_api_key(ws, "bocha", key)` (also works for other Bearer/API-key modules).

## Endpoints (via `run_search`)

| endpoint | Method | URL |
|----------|--------|-----|
| `web-search` | POST | `https://api.bocha.cn/v1/web-search` |
| `ai-search` | POST | `https://api.bocha.cn/v1/ai-search` |

Example:

```json
{
  "module_id": "bocha",
  "endpoint": "web-search",
  "q": "阿里巴巴2024年的ESG报告",
  "freshness": "noLimit",
  "count": 10,
  "summary": true
}
```

Returns complete HTTP status + JSON body (research fidelity; no truncation).
