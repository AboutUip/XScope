#pragma once

#include "xscope/registry/search_module.hpp"
#include "xscope/skills/skill_store.hpp"
#include "xscope/utils/json.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xscope::registry {

class RegistryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// File-backed registry of search modules (JSON).
/// A valid module must declare a skill_id that exists on disk when validated against SkillStore.
class SearchRegistry {
public:
    static constexpr int kSchemaVersion = 1;

    /// Open (or create empty) registry JSON at path.
    void open(const std::filesystem::path& registry_file);
    void close() noexcept;

    bool is_open() const noexcept { return !path_.empty(); }
    const std::filesystem::path& path() const noexcept { return path_; }

    void reload();
    void save() const;

    /// Merge modules from another JSON file (later ids overwrite).
    void merge_file(const std::filesystem::path& other_file);

    std::vector<SearchModule> list() const;
    std::vector<SearchModule> list_enabled() const;
    std::optional<SearchModule> find(const std::string& id) const;

    void upsert(const SearchModule& module);
    void remove(const std::string& id);
    void set_enabled(const std::string& id, bool enabled);

    /// Structural checks + optional skill existence / secret hints.
    ValidationResult validate(const SearchModule& module, const skills::SkillStore* skills = nullptr,
                              bool require_secret_present = false,
                              const std::function<bool(const std::string& secret_id)>& has_secret = {}) const;

    ValidationResult validate_all(const skills::SkillStore* skills = nullptr,
                                  bool require_secret_present = false,
                                  const std::function<bool(const std::string& secret_id)>& has_secret = {}) const;

    /// Install/import a skill, then register (or update) a search module bound to it.
    SearchModule import_skill_module(skills::SkillStore& skills, const std::filesystem::path& skill_source,
                                     SearchModule draft);

    /// UI-bound registry catalog as XAIOP wire.
    std::string catalog_xaiop() const;

private:
    void load_from_text(const std::string& text);
    std::string dump_text() const;
    static SearchModule module_from_json(const utils::Json& node);
    static utils::Json module_to_json(const SearchModule& module);

    std::filesystem::path path_;
    std::vector<SearchModule> modules_;
};

} // namespace xscope::registry
