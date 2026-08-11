# AI 模块

[English](ai.md)

## 目标

通用 **AI 提供商 + 模型注册表**，单一 OpenAI 兼容传输层。首批种子：**DeepSeek** 与 **Kimi（Moonshot）**。

**规范：凡是客户端可能流式查阅的 AI 相关返回，一律为 XAIOP 线文本**（目录、可用列表、对话阶段、错误）。设置页 / C API 便利快照可用普通 JSON。

## 为何 AI 必须走 XAIOP

客户端会对流式 AI 输出做 UI 渲染。厂商 HTTP（SSE/JSON）止于适配器边界；SDK 在交付“AI 结果”之前必须先编码为 XAIOP Snapshot/阶段。

## 布局

```
data_root/registry/ai_providers.json
```

在 `Workspace::open` 时通过 `ensure_ai_providers()` 种子化（**仅 providers**）。模型在写入 API Key 后，通过各厂商 OpenAI 兼容接口 `GET {base_url}/models` 同步。

## 注册表 schema（v1）

- `providers[]`：`id`、`name`、`base_url`、`api_style`（`openai_chat_completions`）、`auth`（`bearer` + `secret_id`）、`preferred_model_id`、**`model_capabilities`**
- `models[]`：`id`（内部 `{provider_id}/{vendor_model}`）、`provider_id`、`model`（厂商名）、`capabilities`

`model_capabilities` 为供应商侧的模型输入能力策略。规范标签：

| 标签 | 含义 |
|------|------|
| `chat` | 文本输入（始终开启） |
| `image_input` | 图片输入 |
| `video_input` | 视频输入 |

内置默认：**Kimi** → `chat` + `image_input` + `video_input`；**DeepSeek** → 仅 `chat`。设置页（C API `xscope_ai_set_model_capabilities`）修改后会同步到该供应商下全部模型；刷新模型列表时也会按当前策略写入。

密钥：`deepseek.default`、`kimi.default`（经 Workspace 加密）。

内部模型 id：`{provider_id}/{vendor_model}`（如 `deepseek/deepseek-chat`）。`preferred_model_id` 记录用户选择；刷新后若原偏好仍存在则保留，否则回落到列表第一项。

## SDK API

| API | 返回 |
|-----|------|
| `AiRuntime::catalog_xaiop()` | XAIOP 注册表目录 |
| `AiRuntime::list_usable_models_xaiop(require_secret)` | XAIOP 可用模型 |
| `AiRuntime::refresh_models_from_api(provider_id)` | JSON 状态（C API / 设置） |
| `AiRuntime::providers_status_json()` | JSON 提供商 + 缓存模型 |
| `AiRuntime::chat_xaiop(request)` | 最终 XAIOP `ai_chat` 快照 |
| `AiRuntime::chat_stream_xaiop(request, on_phase)` | 每一阶段皆为 XAIOP（`start`/`delta`/`final`/`error`） |

阶段文档（编码前 JSON）见英文版；`assistant.content` 为累计**全文**（不截断）。

## Workspace

```cpp
auto ai = ws.ai_runtime();
ws.put_secret("deepseek.default", "deepseek", api_key);
auto status = ai.refresh_models_from_api("deepseek");
ai.chat_stream_xaiop(req, [](const std::string& xaiop_wire, bool is_final) {
    // LiveParser / UI 流
});
```
