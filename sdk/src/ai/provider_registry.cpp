#include "xscope/ai/provider_registry.hpp"

#include "xscope/utils/path.hpp"
#include "xscope/utils/string.hpp"
#include "xscope/xaiop/bridge.hpp"

#include <algorithm>
#include <sstream>

namespace xscope::ai {

void ProviderRegistry::open(const std::filesystem::path& registry_file) {
    path_ = registry_file;
    if (std::filesystem::exists(path_)) {
        reload();
    } else {
        providers_.clear();
        models_.clear();
        utils::ensure_directory(path_.parent_path());
        save();
    }
}

void ProviderRegistry::close() noexcept {
    path_.clear();
    providers_.clear();
    models_.clear();
}

void ProviderRegistry::reload() {
    if (path_.empty()) {
        throw AiError("AI registry is not open");
    }
    load_from_text(utils::read_file_utf8(path_));
}

void ProviderRegistry::save() const {
    if (path_.empty()) {
        throw AiError("AI registry is not open");
    }
    utils::write_file_utf8(path_, dump_text());
}

std::vector<AiProvider> ProviderRegistry::list_providers() const { return providers_; }
std::vector<AiModel> ProviderRegistry::list_models() const { return models_; }

std::vector<AiModel> ProviderRegistry::list_enabled_models() const {
    std::vector<AiModel> out;
    for (const auto& m : models_) {
        if (!m.enabled) {
            continue;
        }
        auto p = find_provider(m.provider_id);
        if (p && p->enabled) {
            out.push_back(m);
        }
    }
    return out;
}

std::optional<AiProvider> ProviderRegistry::find_provider(const std::string& id) const {
    for (const auto& p : providers_) {
        if (p.id == id) {
            return p;
        }
    }
    return std::nullopt;
}

std::optional<AiModel> ProviderRegistry::find_model(const std::string& id) const {
    for (const auto& m : models_) {
        if (m.id == id) {
            return m;
        }
    }
    return std::nullopt;
}

void ProviderRegistry::upsert_provider(const AiProvider& provider) {
    if (provider.id.empty()) {
        throw AiError("provider id is required");
    }
    for (auto& p : providers_) {
        if (p.id == provider.id) {
            p = provider;
            return;
        }
    }
    providers_.push_back(provider);
}

void ProviderRegistry::upsert_model(const AiModel& model) {
    if (model.id.empty() || model.provider_id.empty() || model.model.empty()) {
        throw AiError("model id, provider_id, and model are required");
    }
    for (auto& m : models_) {
        if (m.id == model.id) {
            m = model;
            return;
        }
    }
    models_.push_back(model);
}

void ProviderRegistry::set_model_enabled(const std::string& id, bool enabled) {
    for (auto& m : models_) {
        if (m.id == id) {
            m.enabled = enabled;
            return;
        }
    }
    throw AiError("unknown model id: " + id);
}

void ProviderRegistry::replace_provider_models(const std::string& provider_id,
                                               std::vector<AiModel> models) {
    models_.erase(std::remove_if(models_.begin(), models_.end(),
                                 [&](const AiModel& m) { return m.provider_id == provider_id; }),
                  models_.end());
    for (auto& m : models) {
        if (m.provider_id.empty()) {
            m.provider_id = provider_id;
        }
        upsert_model(m);
    }
}

void ProviderRegistry::set_preferred_model(const std::string& provider_id,
                                           const std::string& model_id) {
    for (auto& p : providers_) {
        if (p.id == provider_id) {
            if (!model_id.empty()) {
                auto m = find_model(model_id);
                if (!m || m->provider_id != provider_id) {
                    throw AiError("model_id not in provider: " + model_id);
                }
            }
            p.preferred_model_id = model_id;
            return;
        }
    }
    throw AiError("unknown provider id: " + provider_id);
}

void ProviderRegistry::set_model_capabilities(const std::string& provider_id,
                                              std::vector<std::string> capabilities) {
    const auto normalized = normalize_model_capabilities(capabilities);
    for (auto& p : providers_) {
        if (p.id != provider_id) {
            continue;
        }
        p.model_capabilities = normalized;
        for (auto& m : models_) {
            if (m.provider_id == provider_id) {
                m.capabilities = normalized;
            }
        }
        return;
    }
    throw AiError("unknown provider id: " + provider_id);
}

std::vector<AiModel> ProviderRegistry::list_models_for_provider(
    const std::string& provider_id) const {
    std::vector<AiModel> out;
    for (const auto& m : models_) {
        if (m.provider_id == provider_id) {
            out.push_back(m);
        }
    }
    return out;
}

std::string ProviderRegistry::catalog_xaiop() const {
    std::ostringstream json;
    json << "{\"meta\":{\"kind\":\"ai_provider_registry\",\"schema\":" << kSchemaVersion << "},"
         << "\"providers\":{";
    for (size_t i = 0; i < providers_.size(); ++i) {
        const auto& p = providers_[i];
        if (i) {
            json << ',';
        }
        json << '"' << utils::json_escape(p.id) << "\":{"
             << "\"name\":\"" << utils::json_escape(p.name) << "\","
             << "\"description\":\"" << utils::json_escape(p.description) << "\","
             << "\"enabled\":" << (p.enabled ? "true" : "false") << ','
             << "\"base_url\":\"" << utils::json_escape(p.base_url) << "\","
             << "\"api_style\":\"" << api_style_to_string(p.api_style) << "\","
             << "\"auth_kind\":\"" << auth_kind_to_string(p.auth.kind) << "\","
             << "\"secret_id\":\"" << utils::json_escape(p.auth.secret_id) << "\","
             << "\"preferred_model_id\":\"" << utils::json_escape(p.preferred_model_id) << "\","
             << "\"model_capabilities\":[";
        for (size_t ci = 0; ci < p.model_capabilities.size(); ++ci) {
            if (ci) {
                json << ',';
            }
            json << '"' << utils::json_escape(p.model_capabilities[ci]) << '"';
        }
        json << "]}";
    }
    json << "},\"models\":{";
    for (size_t i = 0; i < models_.size(); ++i) {
        const auto& m = models_[i];
        if (i) {
            json << ',';
        }
        json << '"' << utils::json_escape(m.id) << "\":{"
             << "\"provider_id\":\"" << utils::json_escape(m.provider_id) << "\","
             << "\"model\":\"" << utils::json_escape(m.model) << "\","
             << "\"name\":\"" << utils::json_escape(m.name) << "\","
             << "\"description\":\"" << utils::json_escape(m.description) << "\","
             << "\"enabled\":" << (m.enabled ? "true" : "false") << ','
             << "\"capabilities\":[";
        for (size_t ci = 0; ci < m.capabilities.size(); ++ci) {
            if (ci) {
                json << ',';
            }
            json << '"' << utils::json_escape(m.capabilities[ci]) << '"';
        }
        json << "]}";
    }
    json << "}}";
    return xscope::xaiop::Bridge::instance().encode_json(json.str());
}

std::string ProviderRegistry::list_usable_models_xaiop(
    bool require_secret_present,
    const std::function<bool(const std::string& secret_id)>& has_secret) const {
    std::ostringstream json;
    json << "{\"meta\":{\"kind\":\"ai_usable_models\",\"schema\":" << kSchemaVersion
         << ",\"policy\":\"full_fidelity\"},\"models\":{";
    bool first = true;
    for (const auto& m : list_enabled_models()) {
        auto p = find_provider(m.provider_id);
        if (!p) {
            continue;
        }
        bool secret_ok = true;
        if (p->auth.kind != AuthKind::None && !p->auth.secret_id.empty()) {
            secret_ok = has_secret && has_secret(p->auth.secret_id);
            if (require_secret_present && !secret_ok) {
                continue;
            }
        }
        if (!first) {
            json << ',';
        }
        first = false;
        json << '"' << utils::json_escape(m.id) << "\":{"
             << "\"name\":\"" << utils::json_escape(m.name) << "\","
             << "\"description\":\"" << utils::json_escape(m.description) << "\","
             << "\"provider_id\":\"" << utils::json_escape(m.provider_id) << "\","
             << "\"provider_name\":\"" << utils::json_escape(p->name) << "\","
             << "\"model\":\"" << utils::json_escape(m.model) << "\","
             << "\"base_url\":\"" << utils::json_escape(p->base_url) << "\","
             << "\"api_style\":\"" << api_style_to_string(p->api_style) << "\","
             << "\"secret_id\":\"" << utils::json_escape(p->auth.secret_id) << "\","
             << "\"secret_configured\":" << (secret_ok ? "true" : "false") << ','
             << "\"capabilities\":[";
        for (size_t i = 0; i < m.capabilities.size(); ++i) {
            if (i) {
                json << ',';
            }
            json << '"' << utils::json_escape(m.capabilities[i]) << '"';
        }
        json << "]}";
    }
    json << "}}";
    return xscope::xaiop::Bridge::instance().encode_json(json.str());
}

AiProvider ProviderRegistry::provider_from_json(const utils::Json& node) {
    AiProvider p;
    p.id = node.at("id").as_string("");
    p.name = node.contains("name") ? node.at("name").as_string("") : p.id;
    p.description = node.contains("description") ? node.at("description").as_string("") : "";
    p.enabled = !node.contains("enabled") || node.at("enabled").as_bool(true);
    p.base_url = node.contains("base_url") ? node.at("base_url").as_string("") : "";
    if (node.contains("api_style")) {
        if (auto s = api_style_from_string(node.at("api_style").as_string(""))) {
            p.api_style = *s;
        }
    }
    p.chat_path = node.contains("chat_path") ? node.at("chat_path").as_string("") : "";
    p.preferred_model_id =
        node.contains("preferred_model_id") ? node.at("preferred_model_id").as_string("") : "";
    if (node.contains("auth") && node.at("auth").is_object()) {
        const auto& a = node.at("auth");
        if (a.contains("type")) {
            if (auto k = auth_kind_from_string(a.at("type").as_string(""))) {
                p.auth.kind = *k;
            }
        }
        p.auth.secret_id = a.contains("secret_id") ? a.at("secret_id").as_string("") : "";
        p.auth.param_name = a.contains("param_name") ? a.at("param_name").as_string("Authorization")
                                                     : "Authorization";
    }
    if (node.contains("model_capabilities") && node.at("model_capabilities").is_array()) {
        for (const auto& c : node.at("model_capabilities").as_array()) {
            if (c.is_string()) {
                p.model_capabilities.push_back(c.as_string());
            }
        }
    }
    p.model_capabilities = normalize_model_capabilities(
        p.model_capabilities.empty() ? default_model_capabilities_for_provider(p.id)
                                     : p.model_capabilities);
    return p;
}

AiModel ProviderRegistry::model_from_json(const utils::Json& node) {
    AiModel m;
    m.id = node.at("id").as_string("");
    m.provider_id = node.contains("provider_id") ? node.at("provider_id").as_string("") : "";
    m.model = node.contains("model") ? node.at("model").as_string("") : "";
    m.name = node.contains("name") ? node.at("name").as_string("") : m.id;
    m.description = node.contains("description") ? node.at("description").as_string("") : "";
    m.enabled = !node.contains("enabled") || node.at("enabled").as_bool(true);
    if (node.contains("capabilities") && node.at("capabilities").is_array()) {
        for (const auto& c : node.at("capabilities").as_array()) {
            if (c.is_string()) {
                m.capabilities.push_back(c.as_string());
            }
        }
    }
    if (m.capabilities.empty()) {
        m.capabilities = {kCapChat};
    } else {
        m.capabilities = normalize_model_capabilities(m.capabilities);
    }
    return m;
}

utils::Json ProviderRegistry::provider_to_json(const AiProvider& p) {
    utils::Json::Object auth;
    auth.emplace("type", std::string(auth_kind_to_string(p.auth.kind)));
    auth.emplace("secret_id", p.auth.secret_id);
    auth.emplace("param_name", p.auth.param_name);
    utils::Json::Array caps;
    for (const auto& c : p.model_capabilities) {
        caps.emplace_back(c);
    }
    utils::Json::Object obj;
    obj.emplace("id", p.id);
    obj.emplace("name", p.name);
    obj.emplace("description", p.description);
    obj.emplace("enabled", p.enabled);
    obj.emplace("base_url", p.base_url);
    obj.emplace("api_style", std::string(api_style_to_string(p.api_style)));
    obj.emplace("chat_path", p.chat_path);
    obj.emplace("preferred_model_id", p.preferred_model_id);
    obj.emplace("model_capabilities", utils::Json(std::move(caps)));
    obj.emplace("auth", utils::Json(std::move(auth)));
    return utils::Json(std::move(obj));
}

utils::Json ProviderRegistry::model_to_json(const AiModel& m) {
    utils::Json::Array caps;
    for (const auto& c : m.capabilities) {
        caps.emplace_back(c);
    }
    utils::Json::Object obj;
    obj.emplace("id", m.id);
    obj.emplace("provider_id", m.provider_id);
    obj.emplace("model", m.model);
    obj.emplace("name", m.name);
    obj.emplace("description", m.description);
    obj.emplace("enabled", m.enabled);
    obj.emplace("capabilities", utils::Json(std::move(caps)));
    return utils::Json(std::move(obj));
}

void ProviderRegistry::load_from_text(const std::string& text) {
    providers_.clear();
    models_.clear();
    if (utils::trim_copy(text).empty()) {
        return;
    }
    const auto root = utils::Json::parse(text);
    if (!root.is_object()) {
        throw AiError("ai registry root must be an object");
    }
    if (root.contains("providers") && root.at("providers").is_array()) {
        for (const auto& n : root.at("providers").as_array()) {
            if (n.is_object()) {
                providers_.push_back(provider_from_json(n));
            }
        }
    }
    if (root.contains("models") && root.at("models").is_array()) {
        for (const auto& n : root.at("models").as_array()) {
            if (n.is_object()) {
                models_.push_back(model_from_json(n));
            }
        }
    }
}

std::string ProviderRegistry::dump_text() const {
    utils::Json::Array providers;
    for (const auto& p : providers_) {
        providers.push_back(provider_to_json(p));
    }
    utils::Json::Array models;
    for (const auto& m : models_) {
        models.push_back(model_to_json(m));
    }
    utils::Json::Object root;
    root.emplace("schema", kSchemaVersion);
    root.emplace("providers", utils::Json(std::move(providers)));
    root.emplace("models", utils::Json(std::move(models)));
    return utils::Json(std::move(root)).dump(2);
}

} // namespace xscope::ai
