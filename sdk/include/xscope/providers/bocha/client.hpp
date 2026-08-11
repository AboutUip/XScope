#pragma once

#include "xscope/network/http_client.hpp"
#include "xscope/network/types.hpp"
#include "xscope/utils/json.hpp"

#include <functional>
#include <optional>
#include <string>

namespace xscope::providers::bocha {

struct ClientOptions {
    /// Matches Bocha open platform docs (api.bocha.cn).
    std::string api_base = "https://api.bocha.cn/v1";
    std::string user_agent = "XScope-Bocha/0.1";
};

/// OpenAI-style Bearer client for Bocha Web Search + AI Search.
class Client {
public:
    using TokenFn = std::function<std::optional<std::string>()>;

    Client(network::HttpClient& http, TokenFn token_fn, ClientOptions options = {});

    const ClientOptions& options() const noexcept { return options_; }

    /// POST /web-search — full-fidelity HTTP response.
    network::HttpResponse web_search(const utils::Json& body);

    /// POST /ai-search — full-fidelity HTTP response (non-stream by default).
    network::HttpResponse ai_search(const utils::Json& body);

    /// POST /{endpoint} where endpoint is `web-search` or `ai-search`.
    network::HttpResponse search(const std::string& endpoint, const utils::Json& body);

    /// Normalize HTTP response for MCP/tool output (complete body, no truncation).
    static utils::Json response_to_json(const network::HttpResponse& resp);

private:
    network::HttpResponse post_json(const std::string& path, const utils::Json& body);

    network::HttpClient& http_;
    TokenFn token_fn_;
    ClientOptions options_;
};

} // namespace xscope::providers::bocha
