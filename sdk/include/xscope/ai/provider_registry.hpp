#pragma once

#include "xscope/ai/types.hpp"
#include "xscope/utils/json.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xscope::ai {

/// File-backed AI provider + model registry (`ai_providers.json`).
class ProviderRegistry {
public:
    static constexpr int kSchemaVersion = 1;

    void open(const std::filesystem::path& registry_file);
    void close() noexcept;
    bool is_open() const noexcept { return !path_.empty(); }

    void reload();
    void save() const;

    std::vector<AiProvider> list_providers() const;
    std::vector<AiModel> list_models() const;
    std::vector<AiModel> list_enabled_models() const;

    std::optional<AiProvider> find_provider(const std::string& id) const;
    std::optional<AiModel> find_model(const std::string& id) const;

    void upsert_provider(const AiProvider& provider);
    void upsert_model(const AiModel& model);
    void set_model_enabled(const std::string& id, bool enabled);

    /// Replace all models for `provider_id` with `models` (does not save).
    void replace_provider_models(const std::string& provider_id, std::vector<AiModel> models);

    /// Update preferred_model_id on an existing provider (throws if unknown).
    void set_preferred_model(const std::string& provider_id, const std::string& model_id);

    /// Set provider model-side capability policy and mirror onto all of its models.
    void set_model_capabilities(const std::string& provider_id,
                                std::vector<std::string> capabilities);

    /// Models belonging to a provider (any enabled flag).
    std::vector<AiModel> list_models_for_provider(const std::string& provider_id) const;

    /// UI catalog — **always XAIOP wire**.
    std::string catalog_xaiop() const;

    /// Usable models (enabled + provider enabled + optional secret gate) as **XAIOP wire**.
    std::string list_usable_models_xaiop(
        bool require_secret_present,
        const std::function<bool(const std::string& secret_id)>& has_secret) const;

private:
    void load_from_text(const std::string& text);
    std::string dump_text() const;
    static AiProvider provider_from_json(const utils::Json& node);
    static AiModel model_from_json(const utils::Json& node);
    static utils::Json provider_to_json(const AiProvider& p);
    static utils::Json model_to_json(const AiModel& m);

    std::filesystem::path path_;
    std::vector<AiProvider> providers_;
    std::vector<AiModel> models_;
};

} // namespace xscope::ai
