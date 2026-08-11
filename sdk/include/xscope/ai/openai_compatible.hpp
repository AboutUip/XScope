#pragma once

#include "xscope/ai/types.hpp"
#include "xscope/network/cancel.hpp"
#include "xscope/network/http_client.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xscope::ai {

/// OpenAI Chat Completions compatible transport (DeepSeek / Kimi / Moonshot / …).
class OpenAiCompatibleClient {
public:
    using TokenFn = std::function<std::optional<std::string>()>;

    OpenAiCompatibleClient(network::HttpClient& http, TokenFn token_fn);

    /// Non-stream completion; returns full assistant text + usage in `out_delta`.
    std::string chat(const AiProvider& provider, const AiModel& model, const ChatRequest& request,
                     ChatDelta* out_delta = nullptr, network::CancelToken* cancel = nullptr);

    /// Stream SSE; invokes on_delta for each content piece (full fidelity, no truncation).
    void chat_stream(const AiProvider& provider, const AiModel& model, const ChatRequest& request,
                     const std::function<void(const ChatDelta& delta)>& on_delta,
                     network::CancelToken* cancel = nullptr);

    /// OpenAI-compatible `GET {base_url}/models` → vendor model ids.
    std::vector<std::string> list_models(const AiProvider& provider,
                                         network::CancelToken* cancel = nullptr);

private:
    std::string build_url(const AiProvider& provider) const;
    std::string build_models_url(const AiProvider& provider) const;
    std::string build_body(const AiModel& model, const ChatRequest& request, bool stream) const;
    network::HeaderList build_headers(const AiProvider& provider) const;

    network::HttpClient& http_;
    TokenFn token_fn_;
};

} // namespace xscope::ai
