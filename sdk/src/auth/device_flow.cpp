#include "xscope/auth/device_flow.hpp"

#include "xscope/utils/json.hpp"
#include "xscope/utils/string.hpp"
#include "xscope/utils/time.hpp"
#include "xscope/utils/url.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace xscope::auth {
namespace {

utils::Json post_form_json(network::HttpClient& http, const std::string& url,
                           const std::vector<std::pair<std::string, std::string>>& form) {
    network::HttpRequest req;
    req.method = network::HttpMethod::Post;
    req.url = url;
    req.headers.emplace_back("Accept", "application/json");
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    req.body = utils::build_query(form);
    req.timeout = std::chrono::seconds(30);
    auto resp = http.send(req);
    if (resp.status < 200 || resp.status >= 300) {
        throw AuthError("device flow HTTP " + std::to_string(resp.status) + ": " + resp.body);
    }
    if (utils::trim_copy(resp.body).empty()) {
        throw AuthError("device flow empty response body");
    }
    return utils::Json::parse(resp.body);
}

} // namespace

DeviceFlowClient::DeviceFlowClient(network::HttpClient& http, DeviceFlowEndpoints endpoints)
    : http_(http), endpoints_(std::move(endpoints)) {}

DeviceAuthorization DeviceFlowClient::request_device_code(const std::string& client_id,
                                                          const std::string& scope,
                                                          const std::string& client_secret) {
    if (client_id.empty()) {
        throw AuthError("OAuth client_id is required (set XSCOPE_GITHUB_OAUTH_CLIENT_ID)");
    }
    std::vector<std::pair<std::string, std::string>> form;
    form.emplace_back("client_id", client_id);
    if (!scope.empty()) {
        form.emplace_back("scope", scope);
    }
    if (!client_secret.empty()) {
        form.emplace_back("client_secret", client_secret);
    }
    const auto json = post_form_json(http_, endpoints_.device_code_url, form);
    if (json.contains("error")) {
        throw AuthError(json.at("error").as_string("error") + ": " +
                        json.at("error_description").as_string(""));
    }
    DeviceAuthorization out;
    out.device_code = json.at("device_code").as_string("");
    out.user_code = json.at("user_code").as_string("");
    out.verification_uri = json.contains("verification_uri")
                               ? json.at("verification_uri").as_string("https://github.com/login/device")
                               : "https://github.com/login/device";
    out.verification_uri_complete =
        json.contains("verification_uri_complete") ? json.at("verification_uri_complete").as_string("")
                                                   : "";
    out.expires_in = static_cast<int>(json.contains("expires_in") ? json.at("expires_in").as_int64(900) : 900);
    out.interval = static_cast<int>(json.contains("interval") ? json.at("interval").as_int64(5) : 5);
    if (out.device_code.empty() || out.user_code.empty()) {
        throw AuthError("device code response missing device_code/user_code");
    }
    if (out.interval < 5) {
        out.interval = 5;
    }
    return out;
}

PollResult DeviceFlowClient::poll_token(const std::string& client_id, const std::string& device_code,
                                        const std::string& client_secret) {
    if (client_id.empty() || device_code.empty()) {
        throw AuthError("client_id and device_code are required to poll");
    }
    std::vector<std::pair<std::string, std::string>> form;
    form.emplace_back("client_id", client_id);
    form.emplace_back("device_code", device_code);
    form.emplace_back("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    if (!client_secret.empty()) {
        form.emplace_back("client_secret", client_secret);
    }
    const auto json = post_form_json(http_, endpoints_.token_url, form);
    PollResult result;
    if (json.contains("error")) {
        const std::string err = json.at("error").as_string("");
        result.error = err;
        result.error_description =
            json.contains("error_description") ? json.at("error_description").as_string("") : "";
        if (json.contains("interval")) {
            result.interval = static_cast<int>(json.at("interval").as_int64(0));
        }
        if (err == "authorization_pending") {
            result.status = PollStatus::Pending;
        } else if (err == "slow_down") {
            result.status = PollStatus::SlowDown;
            if (result.interval <= 0) {
                result.interval = 10;
            }
        } else if (err == "expired_token") {
            result.status = PollStatus::Expired;
        } else if (err == "access_denied") {
            result.status = PollStatus::AccessDenied;
        } else {
            result.status = PollStatus::Error;
        }
        return result;
    }

    result.status = PollStatus::Authorized;
    result.token.access_token = json.at("access_token").as_string("");
    result.token.token_type = json.contains("token_type") ? json.at("token_type").as_string("bearer") : "bearer";
    result.token.scope = json.contains("scope") ? json.at("scope").as_string("") : "";
    result.token.refresh_token =
        json.contains("refresh_token") ? json.at("refresh_token").as_string("") : "";
    result.token.obtained_at = utils::now_unix_seconds();
    if (json.contains("expires_in")) {
        const auto exp = json.at("expires_in").as_int64(0);
        if (exp > 0) {
            result.token.expires_at = result.token.obtained_at + exp;
        }
    }
    if (result.token.access_token.empty()) {
        result.status = PollStatus::Error;
        result.error = "missing_access_token";
        result.error_description = "token endpoint returned no access_token";
    }
    return result;
}

bool open_url_in_browser(const std::string& url) {
    if (url.empty()) {
        return false;
    }
#ifdef _WIN32
    const auto rc = reinterpret_cast<INT_PTR>(
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return rc > 32;
#else
    (void)url;
    return false;
#endif
}

} // namespace xscope::auth
