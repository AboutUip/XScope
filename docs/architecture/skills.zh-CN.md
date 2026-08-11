# Skills 模块

[English](skills.md)

## 职责

`skills` 管理 XScope（及后续 MCP / AI 工具）的**文件型 skill**。  
Skill 就是磁盘上的普通文件——本模块负责索引、加载、安装与删除，**不**发明二进制 skill 包格式。

## 布局

默认根目录（经 `Workspace`）：

```
data_root/
  skills/
    <skill_id>/
      SKILL.md          # 必需入口（YAML frontmatter + markdown 正文）
      ...               # 可选附属文件
    optional-loose.md   # 也会被索引为单文件 skill
```

`SkillStore` 也可打开客户端提供的任意目录（不限于 `data_root/skills`）。

## SKILL.md 约定

```markdown
---
name: my-skill
description: 一行或折叠描述
---

# 正文

Markdown 说明 / 提示词 / 清单…
```

Frontmatter 解析刻意保持最小（`name`、`description`，含简单的 `>-` 折叠块）。

## API 表面

| API | 用途 |
|-----|------|
| `open(skills_root)` / `reload()` | 挂载并重新扫描 |
| `list()` / `find()` / `load()` | 索引 + 读取 |
| `install(source)` | 复制目录（需含 `SKILL.md`）或 `.md` 文件 |
| `save(id, raw)` | 写入/覆盖 `<id>/SKILL.md` |
| `remove(id)` | 删除 skill 文件 |
| `catalog_xaiop()` | 面向 UI 的目录流（按 skill id 键控） |

## 边界

- `skills` 内不做网络 I/O（下载用 `network`，再 `install`）
- 不依赖 SQLite（文件系统即权威）
- 进入客户端 UI 的目录列表走 XAIOP（与项目历史同一规则）
