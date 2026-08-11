# Search module registry

[中文](registry.zh-CN.md)

## Role

The **search module registry** describes which search providers exist, whether they are enabled, which **skill file** they require, and how they authenticate (API key, etc.).

Clients (or the current development tree) can ship a **static JSON** file. Users/clients can later **import skills** to extend the registry.

## JSON file

Default path (via `Workspace`):

```
data_root/registry/search_modules.json
```

Schema (v1):

```json
{
  "schema": 1,
  "modules": [
    {
      "id": "serpapi",
      "name": "SerpAPI",
      "description": "Google results via SerpAPI",
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

### Auth types

| `auth.type` | Meaning |
|-------------|---------|
| `none` | No secret |
| `api_key` | API key style secret |
| `bearer` | Bearer token |
| `basic` | Basic auth material |
| `custom` | Caller-defined |
| `oauth` | OAuth-managed token in secrets (e.g. GitHub Device Flow → `github.oauth`) |

`secret_id` points at the encrypted secrets store (`Workspace::put_secret` / `get_secret`).

## Validity rules

A module is valid when:

1. `id`, `name`, `skill_id` are non-empty
2. `skill_id` exists in `SkillStore` (file-based skill present)
3. If `requires_api_key` → `auth.type != none` and `auth.secret_id` set
4. Optionally (strict mode): the secret actually exists in the store

## APIs

| API | Purpose |
|-----|---------|
| `open` / `reload` / `save` | JSON lifecycle |
| `merge_file` | Import static client JSON (id overwrite) |
| `list` / `list_enabled` / `find` | Query |
| `upsert` / `remove` / `set_enabled` | Mutate |
| `validate` / `validate_all` | Check skill + auth |
| `import_skill_module` | `SkillStore::install` + register module |
| `catalog_xaiop` | UI-bound registry stream |

## Extension flow

1. Client ships `search_modules.json` + skill folders, or calls `merge_file`
2. User imports a skill package → `import_skill_module(skills, path, draft)`
3. UI lists modules via `catalog_xaiop()` / `list_enabled()`
4. At runtime, adapters read `skill_id` + resolve `secret_id` before calling `network`
