# Network module

[中文](network.zh-CN.md)

## Role

`network` is a **generic infrastructure** module in the SDK. It will be depended on by:

- MCP capability adapters
- AI / model provider calls
- Other research transports

It does **not** own search strategy, storage, or UI widgets.

## Layers

| Piece | Responsibility |
|-------|----------------|
| `HttpClient` | Generic HTTP (methods, headers, body, timeouts, cancel, streaming chunks) |
| `CancelToken` | Cooperative cancellation |
| `NetworkClient` | Facade; optional XAIOP plane |
| `XaiopSession` | Encode/parse/stream XAIOP **on top of** HTTP when enabled |

```
MCP / AI / research adapters
        │
        ▼
 NetworkClient ── HttpClient (always)
        │
        └─ XaiopSession (only if enable_xaiop)
                 │
                 ▼
         embedded Go XAIOP (LiveParser / Encode / Parse)
```

## Enabling XAIOP

```cpp
xscope::network::ClientOptions opts;
opts.enable_xaiop = true; // default false
xscope::network::NetworkClient net(opts);

net.http().send(req);          // always available
net.xaiop().post(...);         // requires enable_xaiop
net.xaiop().get_stream(...);   // phase callbacks on '.' + final snapshot
```

When XAIOP is enabled:

- Requests can carry `application/x-xaiop` bodies (JSON auto-encoded to wire)
- Streaming responses are line-assembled and fed to `LiveParser`
- Phase callback fires on `.` and once at end-of-stream

This matches the product rule: **network + UI information exchange → XAIOP**. MCP/AI may use raw HTTP for third-party APIs, then publish UI-facing results via XAIOP helpers.

## Platform

Windows v1 uses **WinHTTP**. The `HttpClient` surface stays portable so Android/other ports can swap the implementation later.

## Non-goals (v1)

- Full connection pooling / HTTP2 tuning
- Parsing business MCP schemas inside `network`

OAuth lives in the `auth` module (not inside raw `HttpClient`). See [OAuth & GitHub REST](oauth-github.md).
