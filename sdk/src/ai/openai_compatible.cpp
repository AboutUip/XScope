#include "xscope/ai/openai_compatible.hpp"

#include "xscope/utils/json.hpp"
#include "xscope/utils/string.hpp"

#include <chrono>
#include <sstream>

namespace xscope::ai {
namespace {

void feed_sse_line(const std::string& line, const std::function<void(const ChatDelta&)>& on_delta,
                   std::string& assistant_acc, ChatDelta& last) {
    auto t = utils::trim_copy(line);
    if (t.empty() || t[0] == ':') {
        return;
    }
    if (!utils::starts_with(t, "data:")) {
        return;
    }
    auto payload = utils::trim_copy(t.substr(5));
    if (payload == "[DONE]") {
        last.done = true;
        if (last.finish_reason.empty()) {
            last.finish_reason = "stop";
        }
        on_delta(last);
        return;
    }
    try {
        const auto json = utils::Json::parse(payload);
        if (json.contains("usage") && json.at("usage").is_object()) {
            const auto& u = json.at("usage");
            last.prompt_tokens = u.contains("prompt_tokens") ? u.at("prompt_tokens").as_int64(0) : 0;
            last.completion_tokens =
                u.contains("completion_tokens") ? u.at("completion_tokens").as_int64(0) : 0;
            last.total_tokens = u.contains("total_tokens") ? u.at("total_tokens").as_int64(0) : 0;
        }
        if (!json.contains("choices") || !json.at("choices").is_array() ||
            json.at("choices").as_array().empty()) {
            return;
        }
        const auto& choice = json.at("choices").as_array()[0];
        if (choice.contains("finish_reason") && !choice.at("finish_reason").is_null()) {
            last.finish_reason = choice.at("finish_reason").as_string("");
        }
        // Keep reasoning and content as SEPARATE deltas. Merging them into one buffer
        // made JSON action parse fail while the UI still showed coherent "thinking".
        auto emit_reasoning = [&](const std::string& piece) {
            if (piece.empty()) {
                return;
            }
            last.reasoning_delta = piece;
            last.content_delta.clear();
            on_delta(last);
        };
        auto emit_content = [&](const std::string& piece) {
            if (piece.empty()) {
                return;
            }
            assistant_acc += piece;
            last.content_delta = piece;
            last.reasoning_delta.clear();
            on_delta(last);
        };
        if (choice.contains("delta") && choice.at("delta").is_object()) {
            const auto& delta = choice.at("delta");
            if (delta.contains("reasoning_content") && !delta.at("reasoning_content").is_null()) {
                emit_reasoning(delta.at("reasoning_content").as_string(""));
            }
            if (delta.contains("reasoning") && !delta.at("reasoning").is_null()) {
                emit_reasoning(delta.at("reasoning").as_string(""));
            }
            if (delta.contains("content") && !delta.at("content").is_null()) {
                emit_content(delta.at("content").as_string(""));
            }
        } else if (choice.contains("message") && choice.at("message").is_object()) {
            const auto& msg = choice.at("message");
            if (msg.contains("reasoning_content") && !msg.at("reasoning_content").is_null()) {
                emit_reasoning(msg.at("reasoning_content").as_string(""));
            }
            if (msg.contains("content") && !msg.at("content").is_null()) {
                emit_content(msg.at("content").as_string(""));
            }
        }
    } catch (...) {
        // Ignore malformed SSE fragments; continue stream.
    }
}

} // namespace

OpenAiCompatibleClient::OpenAiCompatibleClient(network::HttpClient& http, TokenFn token_fn)
    : http_(http), token_fn_(std::move(token_fn)) {}

std::string OpenAiCompatibleClient::build_url(const AiProvider& provider) const {
    std::string base = provider.base_url;
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) {
        base.pop_back();
    }
    const std::string path =
        provider.chat_path.empty() ? std::string("/chat/completions") : provider.chat_path;
    if (!path.empty() && path.front() == '/') {
        return base + path;
    }
    return base + "/" + path;
}

std::string OpenAiCompatibleClient::build_models_url(const AiProvider& provider) const {
    std::string base = provider.base_url;
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) {
        base.pop_back();
    }
    return base + "/models";
}

std::string OpenAiCompatibleClient::build_body(const AiModel& model, const ChatRequest& request,
                                               bool stream) const {
    utils::Json::Array messages;
    for (const auto& m : request.messages) {
        utils::Json::Object msg;
        msg.emplace("role", m.role);
        msg.emplace("content", m.content);
        if (!m.name.empty()) {
            msg.emplace("name", m.name);
        }
        if (!m.tool_call_id.empty()) {
            msg.emplace("tool_call_id", m.tool_call_id);
        }
        messages.emplace_back(std::move(msg));
    }
    utils::Json::Object body;
    body.emplace("model", model.model);
    body.emplace("messages", utils::Json(std::move(messages)));
    body.emplace("stream", stream);
    // Some models (DeepSeek reasoner / o1-class) only accept temperature=1.
    double temp = request.temperature;
    const auto& mid = model.model;
    auto lower = mid;
    for (auto& c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (lower.find("reasoner") != std::string::npos || lower.find("o1") != std::string::npos ||
        lower.find("o3") != std::string::npos || lower.find("r1") != std::string::npos) {
        temp = 1.0;
    }
    body.emplace("temperature", temp);
    if (request.max_tokens > 0) {
        body.emplace("max_tokens", request.max_tokens);
    }
    (void)stream;
    return utils::Json(std::move(body)).dump(0);
}

network::HeaderList OpenAiCompatibleClient::build_headers(const AiProvider& provider) const {
    network::HeaderList headers;
    headers.emplace_back("Content-Type", "application/json");
    headers.emplace_back("Accept", "application/json, text/event-stream");
    if (provider.auth.kind == AuthKind::None) {
        return headers;
    }
    std::string token;
    if (token_fn_) {
        if (auto t = token_fn_()) {
            token = *t;
        }
    }
    if (token.empty()) {
        throw AiError("AI provider secret missing: " + provider.auth.secret_id);
    }
    if (provider.auth.kind == AuthKind::Bearer) {
        headers.emplace_back(provider.auth.param_name.empty() ? "Authorization" : provider.auth.param_name,
                             "Bearer " + token);
    } else if (provider.auth.kind == AuthKind::ApiKeyHeader) {
        headers.emplace_back(provider.auth.param_name.empty() ? "Authorization" : provider.auth.param_name,
                             token);
    }
    return headers;
}

std::string OpenAiCompatibleClient::chat(const AiProvider& provider, const AiModel& model,
                                         const ChatRequest& request, ChatDelta* out_delta,
                                         network::CancelToken* cancel) {
    if (provider.api_style != ApiStyle::OpenAiChatCompletions) {
        throw AiError("unsupported api_style for OpenAiCompatibleClient");
    }
    network::HttpRequest req;
    req.method = network::HttpMethod::Post;
    req.url = build_url(provider);
    req.headers = build_headers(provider);
    req.body = build_body(model, request, false);
    req.timeout = std::chrono::seconds(180);
    auto resp = http_.send(req, cancel);
    if (resp.status < 200 || resp.status >= 300) {
        throw AiError("AI HTTP " + std::to_string(resp.status) + ": " + resp.body);
    }
    const auto json = utils::Json::parse(resp.body);
    ChatDelta delta;
    delta.done = true;
    if (json.contains("usage") && json.at("usage").is_object()) {
        const auto& u = json.at("usage");
        delta.prompt_tokens = u.contains("prompt_tokens") ? u.at("prompt_tokens").as_int64(0) : 0;
        delta.completion_tokens =
            u.contains("completion_tokens") ? u.at("completion_tokens").as_int64(0) : 0;
        delta.total_tokens = u.contains("total_tokens") ? u.at("total_tokens").as_int64(0) : 0;
    }
    if (!json.contains("choices") || !json.at("choices").is_array() ||
        json.at("choices").as_array().empty()) {
        throw AiError("AI response missing choices");
    }
    const auto& choice = json.at("choices").as_array()[0];
    delta.finish_reason =
        choice.contains("finish_reason") ? choice.at("finish_reason").as_string("stop") : "stop";
    std::string content;
    if (choice.contains("message") && choice.at("message").is_object()) {
        const auto& msg = choice.at("message");
        content = msg.contains("content") ? msg.at("content").as_string("") : "";
    }
    delta.content_delta = content;
    if (out_delta) {
        *out_delta = delta;
    }
    return content;
}

void OpenAiCompatibleClient::chat_stream(const AiProvider& provider, const AiModel& model,
                                         const ChatRequest& request,
                                         const std::function<void(const ChatDelta&)>& on_delta,
                                         network::CancelToken* cancel) {
    if (provider.api_style != ApiStyle::OpenAiChatCompletions) {
        throw AiError("unsupported api_style for OpenAiCompatibleClient");
    }
    if (!on_delta) {
        throw AiError("on_delta is required for chat_stream");
    }
    network::HttpRequest req;
    req.method = network::HttpMethod::Post;
    req.url = build_url(provider);
    req.headers = build_headers(provider);
    req.body = build_body(model, request, true);
    req.timeout = std::chrono::seconds(300);

    std::string line_buf;
    std::string assistant_acc;
    ChatDelta last;
    auto resp = http_.send_stream(
        req,
        [&](std::span<const std::uint8_t> chunk) {
            line_buf.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            size_t pos = 0;
            while (true) {
                const auto nl = line_buf.find('\n', pos);
                if (nl == std::string::npos) {
                    break;
                }
                auto line = line_buf.substr(pos, nl - pos);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                feed_sse_line(line, on_delta, assistant_acc, last);
                pos = nl + 1;
            }
            if (pos > 0) {
                line_buf.erase(0, pos);
            }
            return true;
        },
        cancel);

    if (!line_buf.empty()) {
        feed_sse_line(line_buf, on_delta, assistant_acc, last);
    }
    if (resp.status < 200 || resp.status >= 300) {
        throw AiError("AI stream HTTP " + std::to_string(resp.status));
    }
    if (!last.done) {
        last.done = true;
        if (last.finish_reason.empty()) {
            last.finish_reason = "stop";
        }
        last.content_delta.clear();
        on_delta(last);
    }
}

std::vector<std::string> OpenAiCompatibleClient::list_models(const AiProvider& provider,
                                                             network::CancelToken* cancel) {
    if (provider.api_style != ApiStyle::OpenAiChatCompletions) {
        throw AiError("unsupported api_style for OpenAiCompatibleClient::list_models");
    }
    network::HttpRequest req;
    req.method = network::HttpMethod::Get;
    req.url = build_models_url(provider);
    req.headers = build_headers(provider);
    req.timeout = std::chrono::seconds(60);
    auto resp = http_.send(req, cancel);
    if (resp.status < 200 || resp.status >= 300) {
        throw AiError("AI models HTTP " + std::to_string(resp.status) + ": " + resp.body);
    }
    const auto json = utils::Json::parse(resp.body);
    if (!json.contains("data") || !json.at("data").is_array()) {
        throw AiError("AI models response missing data[]");
    }
    std::vector<std::string> out;
    for (const auto& item : json.at("data").as_array()) {
        if (!item.is_object()) {
            continue;
        }
        const auto id = item.contains("id") ? item.at("id").as_string("") : "";
        if (!id.empty()) {
            out.push_back(id);
        }
    }
    return out;
}

} // namespace xscope::ai
