#include "xscope/providers/bocha/client.hpp"

#include "xscope/utils/string.hpp"

#include <stdexcept>

namespace xscope::providers::bocha {
namespace {

std::string lower_copy(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

} // namespace

Client::Client(network::HttpClient& http, TokenFn token_fn, ClientOptions options)
    : http_(http), token_fn_(std::move(token_fn)), options_(std::move(options)) {}

network::HttpResponse Client::web_search(const utils::Json& body) {
    return search("web-search", body);
}

network::HttpResponse Client::ai_search(const utils::Json& body) {
    return search("ai-search", body);
}

network::HttpResponse Client::search(const std::string& endpoint, const utils::Json& body) {
    if (endpoint != "web-search" && endpoint != "ai-search") {
        throw network::NetworkError("Bocha endpoint must be web-search or ai-search");
    }
    return post_json("/" + endpoint, body);
}

network::HttpResponse Client::post_json(const std::string& path, const utils::Json& body) {
    if (path.empty() || path.front() != '/') {
        throw network::NetworkError("Bocha path must start with '/'");
    }
    auto token = token_fn_ ? token_fn_() : std::nullopt;
    if (!token || token->empty()) {
        throw network::NetworkError("missing Bocha API key (secret bocha.default)");
    }

    network::HttpRequest req;
    req.method = network::HttpMethod::Post;
    req.url = options_.api_base + path;
    req.body = body.dump(0);
    req.headers.emplace_back("Authorization", "Bearer " + *token);
    req.headers.emplace_back("Content-Type", "application/json");
    req.headers.emplace_back("Accept", "application/json");
    if (!options_.user_agent.empty()) {
        req.headers.emplace_back("User-Agent", options_.user_agent);
    }
    req.timeout = std::chrono::seconds(60);
    return http_.send(req);
}

utils::Json Client::response_to_json(const network::HttpResponse& resp) {
    utils::Json::Object obj;
    obj.emplace("status", resp.status);
    utils::Json::Object headers;
    for (const auto& h : resp.headers) {
        const auto lower = lower_copy(h.first);
        if (lower == "content-type" || lower == "x-request-id" || lower == "retry-after" ||
            lower == "date") {
            headers.emplace(lower, h.second);
        }
    }
    obj.emplace("headers", utils::Json(std::move(headers)));
    const auto trimmed = utils::trim_copy(resp.body);
    if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[')) {
        try {
            obj.emplace("body", utils::Json::parse(resp.body));
            obj.emplace("body_format", std::string("json"));
        } catch (...) {
            obj.emplace("body", resp.body);
            obj.emplace("body_format", std::string("text"));
        }
    } else {
        obj.emplace("body", resp.body);
        obj.emplace("body_format", std::string("text"));
    }
    return utils::Json(std::move(obj));
}

} // namespace xscope::providers::bocha
