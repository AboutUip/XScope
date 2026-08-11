# 网络模块

[English](network.md)

## 职责

`network` 是 SDK 中的**通用基础设施**模块，后续会被这些能力依赖：

- MCP 能力适配
- AI / 模型提供方调用
- 其他调研传输

它**不**拥有搜索策略、存储或 UI 控件。

## 分层

| 部件 | 职责 |
|------|------|
| `HttpClient` | 通用 HTTP（方法、头、体、超时、取消、分块流式读） |
| `CancelToken` | 协作式取消 |
| `NetworkClient` | 门面；可选 XAIOP 平面 |
| `XaiopSession` | 在启用时，于 HTTP **之上**做 XAIOP 编解码/流式消费 |

```
MCP / AI / 调研适配
        │
        ▼
 NetworkClient ── HttpClient（始终可用）
        │
        └─ XaiopSession（仅 enable_xaiop 时）
                 │
                 ▼
         内嵌 Go XAIOP（LiveParser / Encode / Parse）
```

## 启用 XAIOP

```cpp
xscope::network::ClientOptions opts;
opts.enable_xaiop = true; // 默认 false
xscope::network::NetworkClient net(opts);

net.http().send(req);          // 始终可用
net.xaiop().post(...);         // 需要 enable_xaiop
net.xaiop().get_stream(...);   // 在 '.' 与结束时回调 phase snapshot
```

启用 XAIOP 后：

- 请求可携带 `application/x-xaiop` 正文（JSON 可自动编码为线文本）
- 流式响应按行组装并喂给 `LiveParser`
- 遇到 `.` 与流结束时触发 phase 回调

这与产品规则一致：**网络 + UI 信息交换 → XAIOP**。MCP/AI 调第三方 API 可用原始 HTTP，再把面向 UI 的结果通过 XAIOP 辅助接口发布。

## 平台

Windows 第一版使用 **WinHTTP**。`HttpClient` 表面保持可移植，便于后续 Android 等替换实现。

## 非目标（第一版）

- 完整连接池 / HTTP2 调优
- 在 `network` 内解析业务 MCP schema

OAuth 位于 `auth` 模块（不在裸 `HttpClient` 内）。见 [OAuth 与 GitHub REST](oauth-github.zh-CN.md)。
