#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xscope::registry {

enum class AuthType {
    None,
    ApiKey,
    Bearer,
    Basic,
    Custom,
    OAuth,
};

const char* auth_type_to_string(AuthType type);
std::optional<AuthType> auth_type_from_string(std::string_view s);

struct AuthConfig {
    AuthType type = AuthType::None;
    /// Secret id in Workspace secrets store (e.g. "serp.default").
    std::string secret_id;
    /// Optional header / query parameter name for injecting the key.
    std::string param_name;
};

struct SearchModule {
    std::string id;
    std::string name;
    std::string description;
    bool enabled = false;
    /// Required skill id under SkillStore (directory/file skill).
    std::string skill_id;
    bool requires_api_key = false;
    AuthConfig auth;
    /// Optional free-form tags for client filtering.
    std::vector<std::string> tags;
};

struct ValidationResult {
    bool ok = false;
    std::vector<std::string> errors;
};

} // namespace xscope::registry
