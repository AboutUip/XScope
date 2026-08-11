# 流式与 XAIOP

[English](streaming-xaiop.md)

## 规则（XScope 规范）

同时满足以下两点时，采用 **XAIOP**：

1. 路径涉及**网络**（含 localhost / 回环，以及跨进程或跨主机的传输），**并且**
2. 载荷用于**客户端 UI 渲染与信息交换**（进度、结果、事件、UI 必须消费的结构化状态）。

目的：**流式消费**——数据到达即可 Snapshot / Diff，而不必等待一次性拼完的整棵 JSON。

任一条件不成立时，本规则**不强制**使用 XAIOP。

## 适用

| 示例 | 原因 |
|------|------|
| SDK / worker → Windows UI（localhost pipe/TCP/WS） | 网络 + UI 流 |
| 第二个 `exe` → 首个窗口的项目更新 | 回环/网络 + UI 同步 |
| 拖出窗口接收实时调研相位 | UI 信息交换；若同逻辑流也暴露到跨进程，线上形态必须是 XAIOP，以共用一套模型 |

\*同进程内可为延迟使用内存回调；一旦同一逻辑流跨进程上线，载荷必须是 XAIOP。

## 不适用（就本规则而言）

| 示例 | 原因 |
|------|------|
| 调用第三方搜索 HTTP API | 外部协议；边界适配；结果喂给 UI 时用 XAIOP |

**AI 例外（产品规则）：** 凡 SDK 面向客户端消费的 AI 结果（`catalog` / 可用模型 / 对话流阶段 / 错误）**必须**以 **XAIOP 线文本**发出，即使 UI 尚未接线。厂商 SSE/JSON 止于 AI 适配器边界。
| 加密 SQLite 读写 | 本地存储，不是 UI 网络流 |
| 大二进制下载到 `files/` | 字节/文件 I/O；UI 用 XAIOP 传元数据/进度 |
| 纯 SDK 内部模块调用 | 无网络 + 无 UI 线 |

## 分层

```
UI (WPF)  ←── XAIOP 流（Snapshot / Diff / 相位）──→  SDK / workers
                传输：pipe | TCP | WS | …
持久化仍是加密 SQLite + 项目文件（持久状态的权威来源）。
```

- **传输层**承载 UTF-8 XAIOP 文本（先拼成完整行再解析）。
- **XAIOP** 是面向 UI 的网络应用数据平面。
- **数据库**在丢事件或重启后仍是权威（见 [并发](concurrency.zh-CN.md)）。

可变 UI 行优先用**键控 / 具名路径**模型（task id、evidence id），不要依赖匿名数组下标。参见 XAIOP 实践文档 keyed state modeling（本地参考树在 `dev/XAIOP`，若存在）。

## 调研运行相位（`meta.kind = research_run`）

同进程通过 `xscope_research_poll_xaiop` 轮询，返回 XAIOP `wire` 与解码后的 `doc`。

**当前产品阶段：** 需求确定后立刻深度调研（`requirements_locked` → `next_step` → 搜索/知识图/`clarify` → `synthesize`/`final`）。

详见 [调研编排器](research.zh-CN.md)。

## 协议引用

该平面采用已封存的 XAIOP 线语义 **v0.6.0**。实现可用官方 Go/Node/… SDK 或等价符合规范的解析器；可观察的流式行为应对齐 Snapshot + Diff 消费。
