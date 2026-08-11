#include "xscope/ai/builtin.hpp"

namespace xscope::ai {

void ensure_builtin_ai_providers(ProviderRegistry& registry) {
    auto seed_provider = [&](AiProvider draft) {
        if (auto existing = registry.find_provider(draft.id)) {
            // Keep user prefs / preferred_model_id; refresh endpoint metadata if empty.
            AiProvider p = *existing;
            if (p.base_url.empty()) {
                p.base_url = draft.base_url;
            }
            if (p.auth.secret_id.empty()) {
                p.auth = draft.auth;
            }
            if (p.name.empty()) {
                p.name = draft.name;
            }
            if (p.model_capabilities.empty()) {
                p.model_capabilities = draft.model_capabilities;
            } else {
                p.model_capabilities = normalize_model_capabilities(p.model_capabilities);
            }
            registry.upsert_provider(p);
            return;
        }
        registry.upsert_provider(draft);
    };

    AiProvider deepseek;
    deepseek.id = "deepseek";
    deepseek.name = "DeepSeek";
    deepseek.description = "DeepSeek OpenAI-compatible Chat Completions API";
    deepseek.enabled = true;
    deepseek.base_url = "https://api.deepseek.com";
    deepseek.api_style = ApiStyle::OpenAiChatCompletions;
    deepseek.auth.kind = AuthKind::Bearer;
    deepseek.auth.secret_id = "deepseek.default";
    deepseek.auth.param_name = "Authorization";
    deepseek.model_capabilities = default_model_capabilities_for_provider("deepseek");
    seed_provider(deepseek);

    AiProvider kimi;
    kimi.id = "kimi";
    kimi.name = "Kimi";
    kimi.description = "Moonshot / Kimi OpenAI-compatible Chat Completions API";
    kimi.enabled = true;
    kimi.base_url = "https://api.moonshot.cn/v1";
    kimi.api_style = ApiStyle::OpenAiChatCompletions;
    kimi.auth.kind = AuthKind::Bearer;
    kimi.auth.secret_id = "kimi.default";
    kimi.auth.param_name = "Authorization";
    kimi.model_capabilities = default_model_capabilities_for_provider("kimi");
    seed_provider(kimi);

    // Keep cached models aligned with each provider's model-side policy.
    for (const auto& p : registry.list_providers()) {
        registry.set_model_capabilities(
            p.id, p.model_capabilities.empty()
                      ? default_model_capabilities_for_provider(p.id)
                      : p.model_capabilities);
    }

    registry.save();
}

} // namespace xscope::ai
