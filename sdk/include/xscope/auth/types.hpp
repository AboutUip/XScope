#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace xscope::auth {

class AuthError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Stored OAuth / PAT credential (JSON in secrets store).
struct TokenSet {
    std::string provider;
    std::string access_token;
    std::string token_type = "bearer";
    std::string scope;
    std::string refresh_token;
    /// Unix seconds; 0 means unknown / non-expiring (classic GitHub OAuth PAT-style).
    std::int64_t expires_at = 0;
    std::int64_t obtained_at = 0;
    /// Optional profile hint (e.g. GitHub login) — never a secret by itself.
    std::string account_login;
};

struct DeviceAuthorization {
    std::string device_code;
    std::string user_code;
    std::string verification_uri;
    std::string verification_uri_complete;
    int expires_in = 0;
    int interval = 5;
};

enum class PollStatus {
    Pending,
    SlowDown,
    Authorized,
    Expired,
    AccessDenied,
    Error,
};

struct PollResult {
    PollStatus status = PollStatus::Pending;
    TokenSet token;
    std::string error;
    std::string error_description;
    /// Suggested new interval when status == SlowDown.
    int interval = 0;
};

} // namespace xscope::auth
