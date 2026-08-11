#pragma once

#include "xscope/network/http_client.hpp"
#include "xscope/network/types.hpp"
#include "xscope/utils/json.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xscope::providers::twtapi {

struct ClientOptions {
    /// Direct API host (docs also expose www.twtapi.com/api-proxy/... for the browser console).
    std::string api_base = "https://api.twtapi.com";
    std::string lang = "zh";
    std::string user_agent = "XScope-TwtAPI/0.1";
};

/// TwtAPI (https://www.twtapi.com) — public Twitter/X read APIs via X-API-Key / Bearer.
class Client {
public:
    using TokenFn = std::function<std::optional<std::string>()>;

    Client(network::HttpClient& http, TokenFn token_fn, ClientOptions options = {});

    const ClientOptions& options() const noexcept { return options_; }

    /// Resolve friendly endpoint names to API path segments (e.g. Search, UserTweets).
    /// Empty / unknown values default to "Search". Returns false only if endpoint is explicitly invalid.
    static std::string normalize_endpoint(std::string endpoint);

    /// True when endpoint maps to account status (/myapi/status) instead of /api/v1/twitter/*.
    static bool is_status_endpoint(const std::string& endpoint);

    /// GET Twitter/X proxy endpoint with query params. `endpoint` is a friendly name or path segment.
    network::HttpResponse get(const std::string& endpoint,
                              const std::vector<std::pair<std::string, std::string>>& query = {});

    /// Account status / credits / rate limits.
    network::HttpResponse status();

    static utils::Json response_to_json(const network::HttpResponse& resp);

private:
    network::HttpResponse get_url(const std::string& url);

    network::HttpClient& http_;
    TokenFn token_fn_;
    ClientOptions options_;
};

} // namespace xscope::providers::twtapi
