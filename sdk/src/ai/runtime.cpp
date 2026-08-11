#include "xscope/ai/runtime.hpp"

#include "xscope/utils/string.hpp"
#include "xscope/utils/time.hpp"
#include "xscope/xaiop/bridge.hpp"

namespace xscope::ai {

AiRuntime::AiRuntime(network::HttpClient& http, ProviderRegistry& registry, GetSecretFn get_secret)
    : http_(http), registry_(registry), get_secret_(std::move(get_secret)) {}

std::string AiRuntime::encode_ai_xaiop(const utils::Json& doc) {
    return xscope::xaiop::Bridge::instance().encode_json(doc.dump(0));
}

std::string AiRuntime::catalog_xaiop() const { return registry_.catalog_xaiop(); }

std::string AiRuntime::list_usable_models_xaiop(bool require_secret_present) const {
    return registry_.list_usable_models_xaiop(
        require_secret_present, [this](const std::string& sid) {
            return get_secret_ && get_secret_(sid).has_value();
        });
}

AiRuntime::Resolved AiRuntime::resolve(const std::string& model_id, bool require_secret) const {
    auto model = registry_.find_model(model_id);
    if (!model || !model->enabled) {
        throw AiError("unknown or disabled model_id: " + model_id);
    }
    auto provider = registry_.find_provider(model->provider_id);
    if (!provider || !provider->enabled) {
        throw AiError("unknown or disabled provider for model: " + model_id);
    }
    Resolved r;
    r.provider = *provider;
    r.model = *model;
    if (provider->auth.kind != AuthKind::None) {
        if (!get_secret_) {
            throw AiError("secret lookup not configured");
        }
        auto secret = get_secret_(provider->auth.secret_id);
        if (!secret || secret->empty()) {
            if (require_secret) {
                throw AiError("missing AI secret: " + provider->auth.secret_id);
            }
        } else {
            r.secret = *secret;
        }
        if (r.secret.empty()) {
            throw AiError("missing AI secret: " + provider->auth.secret_id);
        }
    }
    return r;
}

utils::Json AiRuntime::make_phase_doc(const ChatRequest& request, const std::string& phase,
                                      const std::string& assistant_content, const ChatDelta& delta,
                                      const std::string& error,
                                      const std::string& assistant_reasoning) const {
    utils::Json::Object meta;
    meta.emplace("kind", std::string("ai_chat"));
    meta.emplace("schema", 1);
    meta.emplace("phase", phase);
    meta.emplace("stream_id", request.stream_id.empty() ? std::string("default") : request.stream_id);
    meta.emplace("ts", utils::now_unix_seconds());

    utils::Json::Object assistant;
    assistant.emplace("role", std::string("assistant"));
    assistant.emplace("content", assistant_content); // JSON / final answer only
    if (!assistant_reasoning.empty()) {
        assistant.emplace("reasoning", assistant_reasoning);
    }

    utils::Json::Object delta_obj;
    delta_obj.emplace("content", delta.content_delta);
    delta_obj.emplace("reasoning", delta.reasoning_delta);

    utils::Json::Object usage;
    usage.emplace("prompt_tokens", delta.prompt_tokens);
    usage.emplace("completion_tokens", delta.completion_tokens);
    usage.emplace("total_tokens", delta.total_tokens);

    utils::Json::Object root;
    root.emplace("meta", utils::Json(std::move(meta)));
    root.emplace("model_id", request.model_id);
    root.emplace("assistant", utils::Json(std::move(assistant)));
    root.emplace("delta", utils::Json(std::move(delta_obj)));
    root.emplace("usage", utils::Json(std::move(usage)));
    root.emplace("finish_reason", delta.finish_reason);
    root.emplace("done", delta.done || phase == "final" || phase == "error");
    if (!error.empty()) {
        root.emplace("error", error);
        root.emplace("ok", false);
    } else {
        root.emplace("ok", true);
    }
    return utils::Json(std::move(root));
}

std::string AiRuntime::chat_xaiop(const ChatRequest& request, const XaiopPhaseFn& on_phase,
                                  network::CancelToken* cancel) {
    auto resolved = resolve(request.model_id, true);
    OpenAiCompatibleClient client(http_, [secret = resolved.secret]() { return secret; });
    ChatDelta delta;
    std::string content;
    try {
        content = client.chat(resolved.provider, resolved.model, request, &delta, cancel);
    } catch (const std::exception& ex) {
        ChatDelta err;
        err.done = true;
        err.finish_reason = "error";
        auto doc = make_phase_doc(request, "error", "", err, ex.what());
        auto wire = encode_ai_xaiop(doc);
        if (on_phase) {
            on_phase(wire, true);
        }
        return wire;
    }
    delta.done = true;
    delta.content_delta = content;
    auto doc = make_phase_doc(request, "final", content, delta);
    auto wire = encode_ai_xaiop(doc);
    if (on_phase) {
        on_phase(wire, true);
    }
    return wire;
}

utils::Json AiRuntime::refresh_models_from_api(const std::string& provider_id,
                                               network::CancelToken* cancel) {
    auto provider = registry_.find_provider(provider_id);
    if (!provider) {
        throw AiError("unknown provider id: " + provider_id);
    }
    if (provider->auth.kind != AuthKind::None) {
        if (!get_secret_ || !get_secret_(provider->auth.secret_id).has_value() ||
            get_secret_(provider->auth.secret_id)->empty()) {
            throw AiError("missing AI secret: " + provider->auth.secret_id);
        }
    }
    const std::string secret =
        provider->auth.kind == AuthKind::None
            ? std::string{}
            : get_secret_(provider->auth.secret_id).value_or("");

    OpenAiCompatibleClient client(http_, [secret]() -> std::optional<std::string> {
        if (secret.empty()) {
            return std::nullopt;
        }
        return secret;
    });
    const auto vendor_ids = client.list_models(*provider, cancel);

    const auto caps = normalize_model_capabilities(
        provider->model_capabilities.empty()
            ? default_model_capabilities_for_provider(provider_id)
            : provider->model_capabilities);
    if (provider->model_capabilities != caps) {
        AiProvider patched = *provider;
        patched.model_capabilities = caps;
        registry_.upsert_provider(patched);
        provider = registry_.find_provider(provider_id);
    }

    std::vector<AiModel> models;
    models.reserve(vendor_ids.size());
    for (const auto& vid : vendor_ids) {
        AiModel m;
        m.id = provider_id + "/" + vid;
        m.provider_id = provider_id;
        m.model = vid;
        m.name = vid;
        m.description = "";
        m.enabled = true;
        m.capabilities = caps;
        models.push_back(std::move(m));
    }
    registry_.replace_provider_models(provider_id, models);

    std::string preferred = provider->preferred_model_id;
    bool preferred_ok = false;
    if (!preferred.empty()) {
        if (auto m = registry_.find_model(preferred); m && m->provider_id == provider_id) {
            preferred_ok = true;
        }
    }
    if (!preferred_ok) {
        preferred = models.empty() ? std::string{} : models.front().id;
        registry_.set_preferred_model(provider_id, preferred);
    }
    registry_.save();

    utils::Json::Array arr;
    for (const auto& m : registry_.list_models_for_provider(provider_id)) {
        utils::Json::Array mcaps;
        for (const auto& c : m.capabilities) {
            mcaps.emplace_back(c);
        }
        utils::Json::Object o;
        o.emplace("id", m.id);
        o.emplace("model", m.model);
        o.emplace("name", m.name);
        o.emplace("enabled", m.enabled);
        o.emplace("capabilities", utils::Json(std::move(mcaps)));
        arr.emplace_back(std::move(o));
    }
    utils::Json::Array pcaps;
    for (const auto& c : caps) {
        pcaps.emplace_back(c);
    }
    utils::Json::Object root;
    root.emplace("provider_id", provider_id);
    root.emplace("preferred_model_id", preferred);
    root.emplace("model_capabilities", utils::Json(std::move(pcaps)));
    root.emplace("models", utils::Json(std::move(arr)));
    return utils::Json(std::move(root));
}

utils::Json AiRuntime::providers_status_json() const {
    utils::Json::Array providers;
    for (const auto& p : registry_.list_providers()) {
        bool secret_present = false;
        if (p.auth.kind == AuthKind::None || p.auth.secret_id.empty()) {
            secret_present = true;
        } else if (get_secret_) {
            auto s = get_secret_(p.auth.secret_id);
            secret_present = s.has_value() && !s->empty();
        }
        utils::Json::Array models;
        for (const auto& m : registry_.list_models_for_provider(p.id)) {
            utils::Json::Array mcaps;
            for (const auto& c : m.capabilities) {
                mcaps.emplace_back(c);
            }
            utils::Json::Object mo;
            mo.emplace("id", m.id);
            mo.emplace("model", m.model);
            mo.emplace("name", m.name);
            mo.emplace("enabled", m.enabled);
            mo.emplace("capabilities", utils::Json(std::move(mcaps)));
            models.emplace_back(std::move(mo));
        }
        utils::Json::Array pcaps;
        const auto caps = normalize_model_capabilities(
            p.model_capabilities.empty() ? default_model_capabilities_for_provider(p.id)
                                         : p.model_capabilities);
        for (const auto& c : caps) {
            pcaps.emplace_back(c);
        }
        utils::Json::Object po;
        po.emplace("id", p.id);
        po.emplace("name", p.name);
        po.emplace("description", p.description);
        po.emplace("enabled", p.enabled);
        po.emplace("base_url", p.base_url);
        po.emplace("secret_id", p.auth.secret_id);
        po.emplace("secret_present", secret_present);
        po.emplace("preferred_model_id", p.preferred_model_id);
        po.emplace("model_capabilities", utils::Json(std::move(pcaps)));
        po.emplace("models", utils::Json(std::move(models)));
        providers.emplace_back(std::move(po));
    }
    utils::Json::Object root;
    root.emplace("providers", utils::Json(std::move(providers)));
    return utils::Json(std::move(root));
}

std::string AiRuntime::chat_stream_xaiop(const ChatRequest& request, const XaiopPhaseFn& on_phase,
                                         network::CancelToken* cancel) {
    if (!on_phase) {
        throw AiError("on_phase is required for chat_stream_xaiop (UI XAIOP contract)");
    }
    auto resolved = resolve(request.model_id, true);
    OpenAiCompatibleClient client(http_, [secret = resolved.secret]() { return secret; });

    std::string content;
    std::string reasoning;
    std::string final_wire;
    ChatDelta last;

    // Emit start phase so UI can bind the stream immediately.
    {
        ChatDelta start;
        auto doc = make_phase_doc(request, "start", "", start);
        auto wire = encode_ai_xaiop(doc);
        on_phase(wire, false);
    }

    try {
        client.chat_stream(
            resolved.provider, resolved.model, request,
            [&](const ChatDelta& d) {
                last = d;
                if (!d.reasoning_delta.empty()) {
                    reasoning += d.reasoning_delta;
                }
                if (!d.content_delta.empty()) {
                    content += d.content_delta;
                }
                const bool is_final = d.done;
                const char* phase = is_final ? "final" : "delta";
                // assistant.content stays JSON/answer-only; reasoning is separate.
                auto doc = make_phase_doc(request, phase, content, d, "", reasoning);
                auto wire = encode_ai_xaiop(doc);
                on_phase(wire, is_final);
                if (is_final) {
                    final_wire = wire;
                }
            },
            cancel);
    } catch (const std::exception& ex) {
        ChatDelta err = last;
        err.done = true;
        err.finish_reason = "error";
        err.content_delta.clear();
        err.reasoning_delta.clear();
        auto doc = make_phase_doc(request, "error", content, err, ex.what(), reasoning);
        final_wire = encode_ai_xaiop(doc);
        on_phase(final_wire, true);
        return final_wire;
    }

    if (final_wire.empty()) {
        last.done = true;
        if (last.finish_reason.empty()) {
            last.finish_reason = "stop";
        }
        auto doc = make_phase_doc(request, "final", content, last, "", reasoning);
        final_wire = encode_ai_xaiop(doc);
        on_phase(final_wire, true);
    }
    return final_wire;
}

} // namespace xscope::ai
