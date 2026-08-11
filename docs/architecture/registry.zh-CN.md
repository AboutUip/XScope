# 搜索模块注册表

[English](registry.md)

## 职责

**搜索模块注册表**描述有哪些搜索提供方、是否启用、依赖哪个 **skill 文件**，以及如何鉴权（API Key 等）。

客户端（或当前开发树）可附带一份**静态 JSON**。用户/客户端后续也可**导入 skill** 来扩展注册表。

## JSON 文件

默认路径（经 `Workspace`）：

```
data_root/registry/search_modules.json
```

Schema（v1）：

```json
{
  "schema": 1,
  "modules": [
    {
      "id": "serpapi",
      "name": "SerpAPI",
      "description": "经 SerpAPI 获取结果",
      "enabled": true,
      "skill_id": "serpapi-search",
      "requires_api_key": true,
      "auth": {
        "type": "api_key",
        "secret_id": "serp.default",
        "param_name": "api_key"
      },
      "tags": ["web"]
    }
  ]
}
```

### 鉴权类型

| `auth.type` | 含义 |
|-------------|------|
| `none` | 无秘密 |
| `api_key` | API Key |
| `bearer` | Bearer Token |
| `basic` | Basic 鉴权材料 |
| `custom` | 调用方自定义 |
| `oauth` | OAuth 管理的 token（如 GitHub Device Flow → `github.oauth`） |

`secret_id` 指向加密秘密库（`Workspace::put_secret` / `get_secret`）。

## 有效性规则

模块有效需满足：

1. `id`、`name`、`skill_id` 非空
2. `skill_id` 在 `SkillStore` 中存在（磁盘上有对应 skill 文件）
3. 若 `requires_api_key` → `auth.type != none` 且设置了 `auth.secret_id`
4. 可选（严格模式）：秘密库中确实存在该 secret

## API

| API | 用途 |
|-----|------|
| `open` / `reload` / `save` | JSON 生命周期 |
| `merge_file` | 导入客户端静态 JSON（同 id 覆盖） |
| `list` / `list_enabled` / `find` | 查询 |
| `upsert` / `remove` / `set_enabled` | 变更 |
| `validate` / `validate_all` | 校验 skill + 鉴权 |
| `import_skill_module` | `SkillStore::install` + 注册模块 |
| `catalog_xaiop` | 面向 UI 的注册表流 |

## 扩展流程

1. 客户端附带 `search_modules.json` + skill 目录，或调用 `merge_file`
2. 用户导入 skill 包 → `import_skill_module(skills, path, draft)`
3. UI 通过 `catalog_xaiop()` / `list_enabled()` 列出模块
4. 运行时适配器读取 `skill_id`，解析 `secret_id`，再调用 `network`
