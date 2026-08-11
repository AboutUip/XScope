#include "xscope/ai/types.hpp"

#include <algorithm>

namespace xscope::ai {

namespace {

const char* const kKnownCaps[] = {kCapChat, kCapImageInput, kCapVideoInput};

} // namespace

std::vector<std::string> normalize_model_capabilities(const std::vector<std::string>& caps) {
    bool has_chat = false;
    bool has_image = false;
    bool has_video = false;
    for (const auto& c : caps) {
        if (c == kCapChat) {
            has_chat = true;
        } else if (c == kCapImageInput) {
            has_image = true;
        } else if (c == kCapVideoInput) {
            has_video = true;
        }
    }
    // Text chat is always implied for AI providers in XScope.
    has_chat = true;

    std::vector<std::string> out;
    out.reserve(3);
    if (has_chat) {
        out.emplace_back(kCapChat);
    }
    if (has_image) {
        out.emplace_back(kCapImageInput);
    }
    if (has_video) {
        out.emplace_back(kCapVideoInput);
    }
    (void)kKnownCaps;
    return out;
}

bool has_capability(const std::vector<std::string>& caps, std::string_view name) {
    return std::find(caps.begin(), caps.end(), name) != caps.end();
}

std::vector<std::string> default_model_capabilities_for_provider(std::string_view provider_id) {
    if (provider_id == "kimi") {
        return {kCapChat, kCapImageInput, kCapVideoInput};
    }
    // DeepSeek and unknown providers: text-only.
    return {kCapChat};
}

const char* api_style_to_string(ApiStyle style) {
    switch (style) {
    case ApiStyle::OpenAiChatCompletions:
        return "openai_chat_completions";
    }
    return "openai_chat_completions";
}

std::optional<ApiStyle> api_style_from_string(std::string_view s) {
    if (s == "openai_chat_completions" || s == "openai") {
        return ApiStyle::OpenAiChatCompletions;
    }
    return std::nullopt;
}

const char* auth_kind_to_string(AuthKind kind) {
    switch (kind) {
    case AuthKind::None:
        return "none";
    case AuthKind::Bearer:
        return "bearer";
    case AuthKind::ApiKeyHeader:
        return "api_key_header";
    }
    return "bearer";
}

std::optional<AuthKind> auth_kind_from_string(std::string_view s) {
    if (s == "none") {
        return AuthKind::None;
    }
    if (s == "bearer") {
        return AuthKind::Bearer;
    }
    if (s == "api_key_header" || s == "api_key") {
        return AuthKind::ApiKeyHeader;
    }
    return std::nullopt;
}

} // namespace xscope::ai
