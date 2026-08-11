# 调研编排器

[English](research.md)

由引擎负责的调研循环，含两阶段：**需求确定** → **深度调研**。

## 边界

| 层 | 职责 |
|----|------|
| **SDK** | 发现→锁定→深度调研（方向深度层、知识关联图、模块选型）、MCP、证据库、XAIOP |
| **客户端** | 提交查询，轮询/取消/继续，中间区选择题与报告，右侧实时流 |

## 阶段 1 — 需求确定

1. 用户提交初始查询。
2. 引擎可用 Bocha / GitHub **搜索以理解需求**（不是回答）。
3. 模型发出 `thinking`，然后 `search` / `ask_user` / `confirm`。
4. 锁定后 **立刻**进入阶段 2。

## 阶段 2 — 深度调研

1. **分析并选型**可用模块（GitHub 需求优先 REST/code）。
2. **广度** = 调查方向数；**深度** = 某一方向上的层数。
3. 精度限制的是**每方向最大深度层**（不是广度）：
   - 快速 ≤3，普通 ≤5，深度 ≤10，最大 = 不限（须详尽；GitHub 须深入代码细节）。
4. AI 搭建**项目知识关联图**；仅 `valid=true` 的知识入库。
5. **阶段记忆**为散发树（分支旁路，便于后续追问）；引擎**不**向模型倾倒记忆正文。
6. 深度调研每一回合**必须先获取**：`memory_catalog` + `knowledge_graph_catalog`（目录）；是否打开正文由模型决定，并受精度约束：
   - **快速**：仅当前任务或当前分支浅层关联记忆
   - **普通**：相关依赖记忆，或当前分支全链路
   - **深度**：当前链路全记忆 + 其余旁路目录按需读正文
   - **最大**：无读取约束，推进读全相关记忆，频繁使用知识关联图
7. MCP：知识图 get/add/update/delete/link/**catalog**；记忆 **catalog/get/chain/add/branch_***。
8. 调研中允许询问/选择题；以 `synthesize` + `final` 结束。

## XAIOP

见英文版相位表：`requirements_locked` → `next_step(research)` → … → `synthesize` / `final`。

## 相关文档

- [流式与 XAIOP](streaming-xaiop.zh-CN.md)
- [提示词与 MCP](prompts-mcp.zh-CN.md)
- [Bocha](bocha.zh-CN.md)
