# Skills module

[中文](skills.zh-CN.md)

## Role

`skills` manages **file-based skills** for XScope (and future MCP / AI tooling).  
Skills are ordinary files on disk — the module indexes, loads, installs, and removes them. It does **not** invent a binary skill package format.

## Layout

Default root (via `Workspace`):

```
data_root/
  skills/
    <skill_id>/
      SKILL.md          # required entry file (YAML frontmatter + markdown body)
      ...               # optional companion files
    optional-loose.md   # also indexed as a single-file skill
```

`SkillStore` can also open any client-provided directory (not only `data_root/skills`).

## SKILL.md convention

```markdown
---
name: my-skill
description: One-line or folded description
---

# Body

Markdown instructions / prompts / checklists…
```

Frontmatter parsing is intentionally minimal (`name`, `description`, including simple `>-` folded blocks).

## API surface

| API | Purpose |
|-----|---------|
| `open(skills_root)` / `reload()` | Mount and rescan |
| `list()` / `find()` / `load()` | Index + read |
| `install(source)` | Copy a directory (`SKILL.md` required) or `.md` file |
| `save(id, raw)` | Write/replace `<id>/SKILL.md` |
| `remove(id)` | Delete skill files |
| `catalog_xaiop()` | UI-bound catalog stream (keyed by skill id) |

## Boundaries

- No network I/O inside `skills` (use `network` to download, then `install`)
- No SQLite dependency (filesystem is source of truth)
- UI catalogs use XAIOP when crossing into the client (same rule as project history)
