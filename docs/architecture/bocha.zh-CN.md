# 博查搜索供应商

[English](bocha.md)

内置搜索模块 `id=bocha`（skill `bocha-search`）。

## 鉴权

- 类型：Bearer API Key
- 秘密 id：`bocha.default`
- 请求头：`Authorization: Bearer <key>`
- 取钥：[open.bocha.cn](https://open.bocha.cn)

C API：`xscope_search_module_set_api_key(ws, "bocha", key)`（同样适用于其它 Bearer/API-key 模块）。

## 端点（经 `run_search`）

| endpoint | 方法 | URL |
|----------|------|-----|
| `web-search` | POST | `https://api.bocha.cn/v1/web-search` |
| `ai-search` | POST | `https://api.bocha.cn/v1/ai-search` |

完整 HTTP 状态与 JSON body 原样返回（调研保真，不截断）。
