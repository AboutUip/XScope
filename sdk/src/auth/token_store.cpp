#include "xscope/auth/token_store.hpp"

#include "xscope/utils/string.hpp"
#include "xscope/utils/time.hpp"

namespace xscope::auth {

utils::Json token_set_to_json(const TokenSet& token) {
    utils::Json::Object obj;
    obj.emplace("provider", token.provider);
    obj.emplace("access_token", token.access_token);
    obj.emplace("token_type", token.token_type);
    obj.emplace("scope", token.scope);
    obj.emplace("refresh_token", token.refresh_token);
    obj.emplace("expires_at", token.expires_at);
    obj.emplace("obtained_at", token.obtained_at);
    obj.emplace("account_login", token.account_login);
    return utils::Json(std::move(obj));
}

TokenSet token_set_from_json(const utils::Json& json) {
    if (!json.is_object()) {
        throw AuthError("token payload must be a JSON object");
    }
    TokenSet t;
    t.provider = json.contains("provider") ? json.at("provider").as_string("") : "";
    t.access_token = json.contains("access_token") ? json.at("access_token").as_string("") : "";
    t.token_type = json.contains("token_type") ? json.at("token_type").as_string("bearer") : "bearer";
    t.scope = json.contains("scope") ? json.at("scope").as_string("") : "";
    t.refresh_token = json.contains("refresh_token") ? json.at("refresh_token").as_string("") : "";
    t.expires_at = json.contains("expires_at") ? json.at("expires_at").as_int64(0) : 0;
    t.obtained_at = json.contains("obtained_at") ? json.at("obtained_at").as_int64(0) : 0;
    t.account_login = json.contains("account_login") ? json.at("account_login").as_string("") : "";
    if (t.access_token.empty()) {
        throw AuthError("token payload missing access_token");
    }
    return t;
}

TokenSet parse_secret_payload(const std::string& plaintext, const std::string& default_provider) {
    const auto trimmed = utils::trim_copy(plaintext);
    if (trimmed.empty()) {
        throw AuthError("empty secret payload");
    }
    if (trimmed.front() == '{') {
        auto t = token_set_from_json(utils::Json::parse(trimmed));
        if (t.provider.empty()) {
            t.provider = default_provider;
        }
        return t;
    }
    TokenSet t;
    t.provider = default_provider;
    t.access_token = trimmed;
    t.token_type = "bearer";
    t.obtained_at = utils::now_unix_seconds();
    return t;
}

std::string serialize_token_set(const TokenSet& token) { return token_set_to_json(token).dump(0); }

utils::Json token_status_json(const TokenSet& token, bool connected) {
    utils::Json::Object obj;
    obj.emplace("connected", connected);
    obj.emplace("provider", token.provider);
    obj.emplace("token_type", token.token_type);
    obj.emplace("scope", token.scope);
    obj.emplace("expires_at", token.expires_at);
    obj.emplace("obtained_at", token.obtained_at);
    obj.emplace("account_login", token.account_login);
    obj.emplace("has_refresh_token", !token.refresh_token.empty());
    // Never include access_token here.
    return utils::Json(std::move(obj));
}

} // namespace xscope::auth
