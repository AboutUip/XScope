# 存储与项目布局

[English](storage.md)

## 职责划分

| 角色 | 职责 |
|------|------|
| 客户端 | 选择平台安全的 `data_root` |
| SDK | 在该根下布局；SQLite；秘密加密；迁移；安全 API |

SDK **不**自行选择系统特殊文件夹。

## 工作区布局

```
data_root/
  global/
    global.db           # 项目索引、加密秘密、meta
    master.key          # DPAPI 保护的 32 字节 AES 密钥（Windows）
  projects/
    <project_id>/
      project.db        # 每项目库（最小 schema，随后扩展）
      files/            # 附件 / 快照 / 导出
  skills/
    <skill_id>/SKILL.md # 文件型 skills（见 Skills 模块）
  registry/
    search_modules.json # 搜索模块注册表（见 Registry）
  prompts/
    chat_system.md      # 提示词模板（见 提示词与 MCP）
```

规则：

1. 一次调研项目 = 一个目录 + 一个项目数据库。
2. 项目文件都放在该目录下。
3. 全局 API Key 在 `global.db`（密文），不绑死在某一个项目。
4. Schema 从最小集起步（`user_version` + v1 表），经迁移扩展。

## SQLite 模块

`xscope::storage::Database`：

- 只走参数化 SQL
- WAL + busy timeout
- 递归锁；短事务
- Windows 上 UTF-16 打开路径（支持非 ASCII `data_root`）

`xscope::storage::Workspace`：

- 打开全局库、创建项目、打开项目库
- `put_secret` / `get_secret`（AES-256-GCM；主密钥来自 DPAPI）
- `projects_history_xaiop()` — 面向 **UI** 的项目历史，输出为 XAIOP 线文本（按 project id 键控）

## 加密（务实第一版）

| 资产 | 保护 |
|------|------|
| 主密钥文件 | Windows DPAPI（`CryptProtectData`） |
| API Key / 秘密列 | 主密钥下的 AES-256-GCM |
| 库文件位置 | 客户端私有 `data_root` |

完整 SQLCipher 级页加密可在同一 `Database` 边界后补上，无需改调用方。

## UI + XAIOP

项目历史及类似索引读取，若将跨网络/回环进入客户端 UI，**必须**以 XAIOP 流暴露（见 [流式与 XAIOP](streaming-xaiop.zh-CN.md)）。Workspace 已把项目索引按此方式编码，供 Windows 客户端增量消费。
