#pragma once

#include "xscope/auth/device_flow.hpp"
#include "xscope/auth/token_store.hpp"
#include "xscope/network/http_client.hpp"
#include "xscope/utils/json.hpp"

#include <functional>
#include <optional>
#include <string>

namespace xscope::auth {

struct GithubOAuthConfig {
    std::string client_id;
    std::string client_secret;
    /// Space-delimited classic OAuth scopes. Empty still authenticates for public API.
    std::string scope = "read:user";
    std::string secret_id = "github.oauth";
    std::string provider = "github";
    std::string api_base = "https://api.github.com";
    std::string login_base = "https://github.com";
    std::string api_version = "2022-11-28";
};

/// Load client_id/secret from env and optional JSON file (file wins for set fields).
GithubOAuthConfig load_github_oauth_config(const std::string& data_root = {});

class GithubOAuth {
public:
    using GetSecretFn = std::function<std::optional<std::string>(const std::string& id)>;
    using PutSecretFn =
        std::function<void(const std::string& id, const std::string& provider, const std::string& plaintext)>;
    using RemoveSecretFn = std::function<void(const std::string& id)>;

    GithubOAuth(network::HttpClient& http, GithubOAuthConfig config, GetSecretFn get_secret,
                PutSecretFn put_secret, RemoveSecretFn remove_secret = {});

    const GithubOAuthConfig& config() const noexcept { return config_; }
    void set_config(GithubOAuthConfig config) { config_ = std::move(config); }

    /// Start device flow. Optionally open the verification URL in the browser.
    DeviceAuthorization start(const std::string& scope_override = {}, bool open_browser = true);

    /// Poll once. On success, persists TokenSet into encrypted secrets and fetches /user login.
    PollResult poll(const std::string& device_code);

    bool connected() const;
    std::optional<TokenSet> load_token() const;
    /// Access token string for Authorization header; nullopt if missing.
    std::optional<std::string> access_token() const;

    /// Manual PAT / token paste fallback (stored as TokenSet JSON).
    void set_pat(const std::string& token, const std::string& scope = {});

    void disconnect();

    utils::Json status_json() const;

    /// Refresh account_login via GET /user (uses stored token).
    std::string refresh_account_login();

private:
    DeviceFlowEndpoints endpoints() const;
    void persist(TokenSet token);

    network::HttpClient& http_;
    GithubOAuthConfig config_;
    GetSecretFn get_secret_;
    PutSecretFn put_secret_;
    RemoveSecretFn remove_secret_;
};

} // namespace xscope::auth
