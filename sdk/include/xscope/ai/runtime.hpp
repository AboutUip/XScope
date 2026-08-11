#pragma once

#include "xscope/ai/openai_compatible.hpp"
#include "xscope/ai/provider_registry.hpp"
#include "xscope/network/cancel.hpp"
#include "xscope/network/http_client.hpp"
#include "xscope/utils/json.hpp"

#include <functional>
#include <optional>
#include <string>

namespace xscope::ai {

/// AI orchestration. **All public returns / stream phases are XAIOP wire** (UI stream contract).
class AiRuntime {
public:
    using GetSecretFn = std::function<std::optional<std::string>(const std::string& id)>;
    using XaiopPhaseFn = std::function<void(const std::string& xaiop_wire, bool is_final)>;

    AiRuntime(network::HttpClient& http, ProviderRegistry& registry, GetSecretFn get_secret);

    ProviderRegistry& registry() noexcept { return registry_; }
    const ProviderRegistry& registry() const noexcept { return registry_; }

    /// Provider+model catalog as XAIOP.
    std::string catalog_xaiop() const;

    /// Usable models as XAIOP (`require_secret_present` gates on auth.secret_id).
    std::string list_usable_models_xaiop(bool require_secret_present = false) const;

    /// One-shot chat; returns final XAIOP snapshot wire (also emits via on_phase if set).
    std::string chat_xaiop(const ChatRequest& request, const XaiopPhaseFn& on_phase = {},
                           network::CancelToken* cancel = nullptr);

    /// Streaming chat; every phase is XAIOP wire. Returns final XAIOP wire.
    std::string chat_stream_xaiop(const ChatRequest& request, const XaiopPhaseFn& on_phase,
                                  network::CancelToken* cancel = nullptr);

    /// Encode an AI JSON document to XAIOP (helper for tests / extensions).
    static std::string encode_ai_xaiop(const utils::Json& doc);

    /// Fetch vendor `/models`, sync into registry, preserve/fallback preferred_model_id.
    /// Returns a JSON object (not XAIOP) suitable for C API: provider_id, preferred_model_id, models[].
    utils::Json refresh_models_from_api(const std::string& provider_id,
                                        network::CancelToken* cancel = nullptr);

    /// Snapshot providers + cached models + secret_present flags as JSON object (C API).
    utils::Json providers_status_json() const;

private:
    struct Resolved {
        AiProvider provider;
        AiModel model;
        std::string secret;
    };

    Resolved resolve(const std::string& model_id, bool require_secret) const;
    utils::Json make_phase_doc(const ChatRequest& request, const std::string& phase,
                               const std::string& assistant_content, const ChatDelta& delta,
                               const std::string& error = {}) const;

    network::HttpClient& http_;
    ProviderRegistry& registry_;
    GetSecretFn get_secret_;
};

} // namespace xscope::ai
