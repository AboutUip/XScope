#include "xscope/registry/search_module.hpp"

namespace xscope::registry {

const char* auth_type_to_string(AuthType type) {
    switch (type) {
    case AuthType::None:
        return "none";
    case AuthType::ApiKey:
        return "api_key";
    case AuthType::Bearer:
        return "bearer";
    case AuthType::Basic:
        return "basic";
    case AuthType::Custom:
        return "custom";
    case AuthType::OAuth:
        return "oauth";
    }
    return "none";
}

std::optional<AuthType> auth_type_from_string(std::string_view s) {
    if (s == "none") {
        return AuthType::None;
    }
    if (s == "api_key") {
        return AuthType::ApiKey;
    }
    if (s == "bearer") {
        return AuthType::Bearer;
    }
    if (s == "basic") {
        return AuthType::Basic;
    }
    if (s == "custom") {
        return AuthType::Custom;
    }
    if (s == "oauth") {
        return AuthType::OAuth;
    }
    return std::nullopt;
}

} // namespace xscope::registry
