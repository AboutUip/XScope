#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xscope::ai {

class AiError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class ApiStyle {
    OpenAiChatCompletions,
};

const char* api_style_to_string(ApiStyle style);
std::optional<ApiStyle> api_style_from_string(std::string_view s);

enum class AuthKind {
    None,
    Bearer,
    ApiKeyHeader,
};

const char* auth_kind_to_string(AuthKind kind);
std::optional<AuthKind> auth_kind_from_string(std::string_view s);

struct ProviderAuth {
    AuthKind kind = AuthKind::Bearer;
    std::string secret_id;
    /// Header name; Bearer uses Authorization.
    std::string param_name = "Authorization";
};

/// Canonical model-side capability tags (provider policy applied to its models).
inline constexpr const char* kCapChat = "chat";
inline constexpr const char* kCapImageInput = "image_input";
inline constexpr const char* kCapVideoInput = "video_input";

/// Normalize: keep known tags, always include `chat`, stable order, no duplicates.
std::vector<std::string> normalize_model_capabilities(const std::vector<std::string>& caps);
bool has_capability(const std::vector<std::string>& caps, std::string_view name);
/// Builtin defaults when a provider has never set model_capabilities.
std::vector<std::string> default_model_capabilities_for_provider(std::string_view provider_id);

struct AiProvider {
    std::string id;
    std::string name;
    std::string description;
    bool enabled = true;
    std::string base_url;
    ApiStyle api_style = ApiStyle::OpenAiChatCompletions;
    ProviderAuth auth;
    /// Empty → `/chat/completions` for OpenAI style.
    std::string chat_path;
    /// Selected model id (`{provider_id}/{vendor_model}`); empty = unset.
    std::string preferred_model_id;
    /// Policy for this provider's models (chat / image_input / video_input…).
    /// Applied on model refresh and when the user updates settings.
    std::vector<std::string> model_capabilities;
};

struct AiModel {
    std::string id;
    std::string provider_id;
    std::string model;
    std::string name;
    std::string description;
    bool enabled = true;
    std::vector<std::string> capabilities;
};

struct ChatMessage {
    std::string role;
    std::string content;
    std::string name;
    std::string tool_call_id;
};

struct ChatRequest {
    std::string model_id;
    std::vector<ChatMessage> messages;
    double temperature = 0.7;
    std::int64_t max_tokens = 0;
    bool stream = true;
    std::string stream_id;
};

struct ChatDelta {
    std::string content_delta;
    /// Separate from content — reasoner models stream chain-of-thought here.
    std::string reasoning_delta;
    std::string finish_reason;
    std::int64_t prompt_tokens = 0;
    std::int64_t completion_tokens = 0;
    std::int64_t total_tokens = 0;
    bool done = false;
};

} // namespace xscope::ai
