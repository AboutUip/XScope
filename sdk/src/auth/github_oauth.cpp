#include "xscope/auth/github_oauth.hpp"

#include "xscope/utils/path.hpp"
#include "xscope/utils/string.hpp"
#include "xscope/utils/time.hpp"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace xscope::auth {
namespace {

std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string{};
}

} // namespace

GithubOAuthConfig load_github_oauth_config(const std::string& data_root) {
    GithubOAuthConfig cfg;
    cfg.client_id = env_or_empty("XSCOPE_GITHUB_OAUTH_CLIENT_ID");
    cfg.client_secret = env_or_empty("XSCOPE_GITHUB_OAUTH_CLIENT_SECRET");
    if (const auto scope = env_or_empty("XSCOPE_GITHUB_OAUTH_SCOPE"); !scope.empty()) {
        cfg.scope = scope;
    }
    if (data_root.empty()) {
        return cfg;
    }
    const auto path = utils::path_from_utf8(data_root) / "global" / "github_oauth.json";
    if (!fs::exists(path)) {
        return cfg;
    }
    try {
        const auto text = utils::read_file_utf8(path);
        const auto json = utils::Json::parse(text);
        if (json.contains("client_id")) {
            const auto v = json.at("client_id").as_string("");
            if (!v.empty()) {
                cfg.client_id = v;
            }
        }
        if (json.contains("client_secret")) {
            cfg.client_secret = json.at("client_secret").as_string("");
        }
        if (json.contains("scope")) {
            const auto v = json.at("scope").as_string("");
            if (!v.empty()) {
                cfg.scope = v;
            }
        }
        if (json.contains("secret_id")) {
            const auto v = json.at("secret_id").as_string("");
            if (!v.empty()) {
                cfg.secret_id = v;
            }
        }
        if (json.contains("api_base")) {
            const auto v = json.at("api_base").as_string("");
            if (!v.empty()) {
                cfg.api_base = v;
            }
        }
        if (json.contains("login_base")) {
            const auto v = json.at("login_base").as_string("");
            if (!v.empty()) {
                cfg.login_base = v;
            }
        }
    } catch (...) {
        // Keep env defaults if config file is malformed.
    }
    return cfg;
}

GithubOAuth::GithubOAuth(network::HttpClient& http, GithubOAuthConfig config, GetSecretFn get_secret,
                         PutSecretFn put_secret, RemoveSecretFn remove_secret)
    : http_(http), config_(std::move(config)), get_secret_(std::move(get_secret)),
      put_secret_(std::move(put_secret)), remove_secret_(std::move(remove_secret)) {}

DeviceFlowEndpoints GithubOAuth::endpoints() const {
    DeviceFlowEndpoints ep;
    ep.device_code_url = config_.login_base + "/login/device/code";
    ep.token_url = config_.login_base + "/login/oauth/access_token";
    return ep;
}

void GithubOAuth::persist(TokenSet token) {
    token.provider = config_.provider;
    if (token.obtained_at == 0) {
        token.obtained_at = utils::now_unix_seconds();
    }
    put_secret_(config_.secret_id, config_.provider, serialize_token_set(token));
}

DeviceAuthorization GithubOAuth::start(const std::string& scope_override, bool open_browser) {
    DeviceFlowClient flow(http_, endpoints());
    const std::string scope = scope_override.empty() ? config_.scope : scope_override;
    auto auth = flow.request_device_code(config_.client_id, scope, config_.client_secret);
    if (open_browser) {
        const std::string url =
            !auth.verification_uri_complete.empty() ? auth.verification_uri_complete : auth.verification_uri;
        open_url_in_browser(url);
    }
    return auth;
}

PollResult GithubOAuth::poll(const std::string& device_code) {
    DeviceFlowClient flow(http_, endpoints());
    auto result = flow.poll_token(config_.client_id, device_code, config_.client_secret);
    if (result.status != PollStatus::Authorized) {
        return result;
    }
    result.token.provider = config_.provider;
    persist(result.token);
    try {
        result.token.account_login = refresh_account_login();
        // refresh_account_login already re-persists with login.
        if (auto t = load_token()) {
            result.token = *t;
        }
    } catch (...) {
        // Login enrichment is best-effort.
    }
    return result;
}

bool GithubOAuth::connected() const { return access_token().has_value(); }

std::optional<TokenSet> GithubOAuth::load_token() const {
    if (!get_secret_) {
        return std::nullopt;
    }
    auto raw = get_secret_(config_.secret_id);
    if (!raw) {
        return std::nullopt;
    }
    try {
        return parse_secret_payload(*raw, config_.provider);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> GithubOAuth::access_token() const {
    auto t = load_token();
    if (!t || t->access_token.empty()) {
        return std::nullopt;
    }
    return t->access_token;
}

void GithubOAuth::set_pat(const std::string& token, const std::string& scope) {
    TokenSet t;
    t.provider = config_.provider;
    t.access_token = utils::trim_copy(token);
    t.token_type = "bearer";
    t.scope = scope;
    t.obtained_at = utils::now_unix_seconds();
    if (t.access_token.empty()) {
        throw AuthError("PAT/token is empty");
    }
    persist(t);
    try {
        refresh_account_login();
    } catch (...) {
    }
}

void GithubOAuth::disconnect() {
    if (remove_secret_) {
        remove_secret_(config_.secret_id);
    } else {
        // Overwrite with empty marker removed — require remove_secret for clean disconnect.
        throw AuthError("remove_secret callback is required for disconnect");
    }
}

utils::Json GithubOAuth::status_json() const {
    auto t = load_token();
    if (!t) {
        utils::Json::Object obj;
        obj.emplace("connected", false);
        obj.emplace("provider", config_.provider);
        obj.emplace("secret_id", config_.secret_id);
        obj.emplace("client_id_configured", !config_.client_id.empty());
        obj.emplace("default_scope", config_.scope);
        return utils::Json(std::move(obj));
    }
    auto status = token_status_json(*t, true);
    status["secret_id"] = config_.secret_id;
    status["client_id_configured"] = !config_.client_id.empty();
    status["default_scope"] = config_.scope;
    return status;
}

std::string GithubOAuth::refresh_account_login() {
    auto token = access_token();
    if (!token) {
        throw AuthError("not connected");
    }
    network::HttpRequest req;
    req.method = network::HttpMethod::Get;
    req.url = config_.api_base + "/user";
    req.headers.emplace_back("Accept", "application/vnd.github+json");
    req.headers.emplace_back("Authorization", "Bearer " + *token);
    req.headers.emplace_back("X-GitHub-Api-Version", config_.api_version);
    auto resp = http_.send(req);
    if (resp.status < 200 || resp.status >= 300) {
        throw AuthError("GET /user failed HTTP " + std::to_string(resp.status) + ": " + resp.body);
    }
    const auto json = utils::Json::parse(resp.body);
    const std::string login = json.contains("login") ? json.at("login").as_string("") : "";
    auto stored = load_token();
    if (!stored) {
        return login;
    }
    stored->account_login = login;
    persist(*stored);
    return login;
}

} // namespace xscope::auth
