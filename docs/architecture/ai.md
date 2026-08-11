# AI module

[中文](ai.zh-CN.md)

## Goal

Generic **AI provider + model registry** with a single OpenAI-compatible transport. First seeds: **DeepSeek** and **Kimi (Moonshot)**.

**Normative:** every AI-facing return that the client may stream/consume is **XAIOP wire** (catalog, usable list, chat phases, errors). Settings/C API convenience snapshots may use plain JSON.

## Why XAIOP for AI

Clients will stream AI output for UI rendering. External vendor HTTP (SSE/JSON) stays at the adapter boundary; the SDK adapts into XAIOP snapshots/phases before anything is considered an “AI result”.

## Layout

```
data_root/registry/ai_providers.json
```

Seeded on `Workspace::open` via `ensure_ai_providers()` (**providers only**). Models are synced from each vendor’s OpenAI-compatible `GET {base_url}/models` after an API key is stored.

## Registry schema (v1)

- `providers[]`: `id`, `name`, `base_url`, `api_style` (`openai_chat_completions`), `auth` (`bearer` + `secret_id`), `preferred_model_id`, **`model_capabilities`**
- `models[]`: `id` (internal `{provider_id}/{vendor_model}`), `provider_id`, `model` (vendor name), `capabilities`

`model_capabilities` is the provider’s model-side input policy. Canonical tags:

| Tag | Meaning |
|-----|---------|
| `chat` | Text input (always on) |
| `image_input` | Image input |
| `video_input` | Video input |

Builtin defaults: **Kimi** → `chat` + `image_input` + `video_input`; **DeepSeek** → `chat` only. Changing the policy in settings (C API `xscope_ai_set_model_capabilities`) mirrors onto all models for that provider; model refresh also stamps the current policy.

Secrets: `deepseek.default`, `kimi.default` (encrypted via Workspace).

Internal model ids use `{provider_id}/{vendor_model}` (e.g. `deepseek/deepseek-chat`). `preferred_model_id` on the provider records the user’s selection; after a refresh, an existing preference is kept if still present, otherwise the first listed model is selected.

## SDK API

| API | Return |
|-----|--------|
| `AiRuntime::catalog_xaiop()` | XAIOP registry catalog |
| `AiRuntime::list_usable_models_xaiop(require_secret)` | XAIOP usable models |
| `AiRuntime::refresh_models_from_api(provider_id)` | JSON status (C API / settings) |
| `AiRuntime::providers_status_json()` | JSON providers + cached models |
| `AiRuntime::chat_xaiop(request)` | Final XAIOP `ai_chat` snapshot |
| `AiRuntime::chat_stream_xaiop(request, on_phase)` | Each phase is XAIOP (`start`/`delta`/`final`/`error`) |

Phase document shape (JSON before encode):

```json
{
  "meta": { "kind": "ai_chat", "schema": 1, "phase": "delta", "stream_id": "..." },
  "model_id": "deepseek/deepseek-chat",
  "assistant": { "role": "assistant", "content": "<full text so far>" },
  "delta": { "content": "<chunk>" },
  "usage": { "prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0 },
  "finish_reason": "",
  "done": false,
  "ok": true
}
```

Assistant `content` is accumulated **full fidelity** (no truncation).

## Workspace

```cpp
auto ai = ws.ai_runtime();
ws.put_secret("deepseek.default", "deepseek", api_key);
auto status = ai.refresh_models_from_api("deepseek");
ai.chat_stream_xaiop(req, [](const std::string& xaiop_wire, bool is_final) {
    // feed LiveParser / UI stream
});
```
