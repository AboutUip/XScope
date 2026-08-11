# 架构概览

[English](overview.md)

## 产品目标

XScope 是本地**智能网络调研**工具。用户创建调研**项目**、执行任务，并将证据保存在本地磁盘。无账号/登录，也无托管网络数据库。

## 系统布局

```
┌─────────────────────────────────────┐
│  clients/windows（WPF，UI 优先）     │
└─────────────────┬───────────────────┘
                  │ 薄互操作边界
┌─────────────────▼───────────────────┐
│  sdk/（模块化 C++）                   │
│  编排 → 能力模块                      │
│  → 存储 / 加密 / 平台                 │
└─────────────────────────────────────┘
```

- **`sdk/`** — 核心引擎。模块化、低耦合。负责调研逻辑、加密存储使用与未来的 worker。
- **`clients/windows/`** — 主产品界面。用户第一感觉优先。负责选择系统安全的 `data_root` 并呈现流程。
- 其他客户端（如后续 Android）可用不同 UI 壳承载同一套 SDK。

## 设计支柱

1. **模块化 SDK** — 能力独立演进，避免上帝对象。
2. **UI 优先客户端** — Windows WPF + Material Design 3；外观自动/浅色/深色；报告导出（md/pdf/docx）；主壳不用 WebView。
3. **纯本地数据** — 客户端提供 `data_root`；一个项目 = 一棵目录树 + 加密库。
4. **可扩展工作区** — 一个根下多项目；schema 从最小集逐步扩展。
5. **多窗口 / 多进程** — 同项目多次打开时，用稳定锁与 IPC 同步。
6. **务实安全** — 用常见平台做法加密库与密钥；不追求极限，也不轻视。
7. **面向 UI 的网络流用 XAIOP** — 当流量既走网络、又服务客户端 UI 渲染/信息交换时，用 XAIOP 做流式 Snapshot/Diff（见 [流式与 XAIOP](streaming-xaiop.zh-CN.md)）。
8. **通用网络模块** — 供 MCP / AI / 调研共用的 HTTP 运输；可选 `enable_xaiop` 平面（见 [网络](network.zh-CN.md)）。
9. **文件型 skills** — 管理 `data_root/skills` 下的 `SKILL.md` 树（见 [Skills](skills.zh-CN.md)）。
10. **内部 utils** — 共享字符串/路径/时间/JSON 助手（见 [Utils](utils.zh-CN.md)）。
11. **搜索模块注册表** — JSON 目录：提供方 + skill + 鉴权（见 [Registry](registry.zh-CN.md)）。
12. **提示词 + MCP** — 可用模块完整注入提示词；MCP 返回完整 skill（见 [提示词与 MCP](prompts-mcp.zh-CN.md)）。
13. **OAuth + GitHub REST** — Device Flow（PAT 回退）与完整保真的 GitHub 搜索/GET（见 [OAuth 与 GitHub REST](oauth-github.zh-CN.md)）。
14. **AI 提供商** — 注册表驱动的 OpenAI 兼容模型（Kimi / DeepSeek）；**AI 返回一律为 XAIOP**（见 [AI](ai.zh-CN.md)）。

## 相关文档

- [存储](storage.zh-CN.md)
- [并发](concurrency.zh-CN.md)
- [安全](security.zh-CN.md)
- [流式与 XAIOP](streaming-xaiop.zh-CN.md)
- [网络](network.zh-CN.md)
- [Skills](skills.zh-CN.md)
- [Utils](utils.zh-CN.md)
- [Registry](registry.zh-CN.md)
- [提示词与 MCP](prompts-mcp.zh-CN.md)
- [OAuth 与 GitHub REST](oauth-github.zh-CN.md)
- [AI](ai.zh-CN.md)
- [SDK 概览](../sdk/overview.zh-CN.md)
