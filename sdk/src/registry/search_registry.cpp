#include "xscope/registry/search_registry.hpp"

#include "xscope/utils/utils.hpp"
#include "xscope/xaiop/bridge.hpp"

#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace xscope::registry {

void SearchRegistry::open(const fs::path& registry_file) {
    if (registry_file.empty()) {
        throw RegistryError("registry path is empty");
    }
    path_ = fs::absolute(registry_file);
    xscope::utils::ensure_directory(path_.parent_path());
    if (!fs::exists(path_)) {
        modules_.clear();
        save();
        return;
    }
    reload();
}

void SearchRegistry::close() noexcept {
    path_.clear();
    modules_.clear();
}

void SearchRegistry::reload() {
    if (path_.empty()) {
        throw RegistryError("registry is not open");
    }
    load_from_text(xscope::utils::read_file_utf8(path_));
}

void SearchRegistry::save() const {
    if (path_.empty()) {
        throw RegistryError("registry is not open");
    }
    xscope::utils::write_file_utf8(path_, dump_text());
}

void SearchRegistry::merge_file(const fs::path& other_file) {
    if (!fs::exists(other_file)) {
        throw RegistryError("merge file does not exist");
    }
    SearchRegistry other;
    other.path_ = other_file; // temporary
    other.load_from_text(xscope::utils::read_file_utf8(other_file));
    for (const auto& m : other.modules_) {
        upsert(m);
    }
}

std::vector<SearchModule> SearchRegistry::list() const { return modules_; }

std::vector<SearchModule> SearchRegistry::list_enabled() const {
    std::vector<SearchModule> out;
    for (const auto& m : modules_) {
        if (m.enabled) {
            out.push_back(m);
        }
    }
    return out;
}

std::optional<SearchModule> SearchRegistry::find(const std::string& id) const {
    for (const auto& m : modules_) {
        if (m.id == id) {
            return m;
        }
    }
    return std::nullopt;
}

void SearchRegistry::upsert(const SearchModule& module) {
    if (module.id.empty()) {
        throw RegistryError("module id is empty");
    }
    for (auto& m : modules_) {
        if (m.id == module.id) {
            m = module;
            return;
        }
    }
    modules_.push_back(module);
}

void SearchRegistry::remove(const std::string& id) {
    modules_.erase(std::remove_if(modules_.begin(), modules_.end(),
                                  [&](const SearchModule& m) { return m.id == id; }),
                   modules_.end());
}

void SearchRegistry::set_enabled(const std::string& id, bool enabled) {
    for (auto& m : modules_) {
        if (m.id == id) {
            m.enabled = enabled;
            return;
        }
    }
    throw RegistryError("module not found: " + id);
}

ValidationResult SearchRegistry::validate(
    const SearchModule& module, const skills::SkillStore* skills, bool require_secret_present,
    const std::function<bool(const std::string& secret_id)>& has_secret) const {
    ValidationResult result;
    result.ok = true;

    auto fail = [&](const std::string& msg) {
        result.ok = false;
        result.errors.push_back(msg);
    };

    if (module.id.empty()) {
        fail("id is required");
    }
    if (module.skill_id.empty()) {
        fail("skill_id is required");
    }
    if (module.name.empty()) {
        fail("name is required");
    }
    if (module.requires_api_key && module.auth.type == AuthType::None) {
        fail("requires_api_key=true but auth.type is none");
    }
    if (module.requires_api_key && module.auth.secret_id.empty()) {
        fail("requires_api_key=true but auth.secret_id is empty");
    }
    if (module.auth.type != AuthType::None && module.auth.secret_id.empty()) {
        fail("auth.type requires auth.secret_id");
    }
    if (skills && !module.skill_id.empty() && !skills->find(module.skill_id)) {
        fail("skill not found: " + module.skill_id);
    }
    if (require_secret_present && module.requires_api_key && has_secret &&
        !module.auth.secret_id.empty() && !has_secret(module.auth.secret_id)) {
        fail("api key secret missing: " + module.auth.secret_id);
    }
    return result;
}

ValidationResult SearchRegistry::validate_all(
    const skills::SkillStore* skills, bool require_secret_present,
    const std::function<bool(const std::string& secret_id)>& has_secret) const {
    ValidationResult all;
    all.ok = true;
    for (const auto& m : modules_) {
        auto one = validate(m, skills, require_secret_present, has_secret);
        if (!one.ok) {
            all.ok = false;
            for (const auto& e : one.errors) {
                all.errors.push_back(m.id + ": " + e);
            }
        }
    }
    return all;
}

SearchModule SearchRegistry::import_skill_module(skills::SkillStore& skills,
                                                 const fs::path& skill_source,
                                                 SearchModule draft) {
    const std::string installed_id = skills.install(skill_source);
    if (draft.skill_id.empty()) {
        draft.skill_id = installed_id;
    }
    if (draft.id.empty()) {
        draft.id = draft.skill_id;
    }
    if (draft.name.empty()) {
        if (auto info = skills.find(draft.skill_id)) {
            draft.name = info->name;
            if (draft.description.empty()) {
                draft.description = info->description;
            }
        } else {
            draft.name = draft.id;
        }
    }
    const auto check = validate(draft, &skills, false, {});
    if (!check.ok) {
        std::ostringstream oss;
        oss << "imported module is invalid:";
        for (const auto& e : check.errors) {
            oss << ' ' << e << ';';
        }
        throw RegistryError(oss.str());
    }
    upsert(draft);
    save();
    return draft;
}

std::string SearchRegistry::catalog_xaiop() const {
    std::ostringstream json;
    json << "{\"meta\":{\"kind\":\"search_module_registry\",\"schema\":" << kSchemaVersion
         << "},\"modules\":{";
    for (std::size_t i = 0; i < modules_.size(); ++i) {
        const auto& m = modules_[i];
        if (i) {
            json << ',';
        }
        json << '"' << xscope::utils::json_escape(m.id) << "\":{"
             << "\"name\":\"" << xscope::utils::json_escape(m.name) << "\","
             << "\"description\":\"" << xscope::utils::json_escape(m.description) << "\","
             << "\"enabled\":" << (m.enabled ? "true" : "false") << ','
             << "\"skill_id\":\"" << xscope::utils::json_escape(m.skill_id) << "\","
             << "\"requires_api_key\":" << (m.requires_api_key ? "true" : "false") << ','
             << "\"auth_type\":\"" << auth_type_to_string(m.auth.type) << "\","
             << "\"secret_id\":\"" << xscope::utils::json_escape(m.auth.secret_id) << "\"}";
    }
    json << "}}";
    return xscope::xaiop::Bridge::instance().encode_json(json.str());
}

void SearchRegistry::load_from_text(const std::string& text) {
    modules_.clear();
    if (xscope::utils::trim_copy(text).empty()) {
        return;
    }
    const auto root = xscope::utils::Json::parse(text);
    if (!root.is_object()) {
        throw RegistryError("registry root must be an object");
    }
    const int schema = static_cast<int>(root.at("schema").as_number(kSchemaVersion));
    if (schema > kSchemaVersion) {
        throw RegistryError("registry schema is newer than this SDK");
    }
    const auto& modules = root.at("modules");
    if (modules.is_null()) {
        return;
    }
    if (!modules.is_array()) {
        throw RegistryError("modules must be an array");
    }
    for (const auto& node : modules.as_array()) {
        modules_.push_back(module_from_json(node));
    }
}

std::string SearchRegistry::dump_text() const {
    xscope::utils::Json::Array arr;
    for (const auto& m : modules_) {
        arr.push_back(module_to_json(m));
    }
    xscope::utils::Json::Object root;
    root.emplace("schema", xscope::utils::Json(kSchemaVersion));
    root.emplace("modules", xscope::utils::Json(std::move(arr)));
    return xscope::utils::Json(std::move(root)).dump(2);
}

SearchModule SearchRegistry::module_from_json(const xscope::utils::Json& node) {
    if (!node.is_object()) {
        throw RegistryError("module entry must be an object");
    }
    SearchModule m;
    m.id = node.at("id").as_string("");
    m.name = node.at("name").as_string(m.id);
    m.description = node.at("description").as_string("");
    m.enabled = node.at("enabled").as_bool(false);
    m.skill_id = node.at("skill_id").as_string("");
    m.requires_api_key = node.at("requires_api_key").as_bool(false);

    const auto& auth = node.at("auth");
    if (auth.is_object()) {
        const auto type = auth_type_from_string(auth.at("type").as_string("none"));
        m.auth.type = type.value_or(AuthType::None);
        m.auth.secret_id = auth.at("secret_id").as_string("");
        m.auth.param_name = auth.at("param_name").as_string("");
    } else {
        // Flat optional fields for simpler static JSON.
        const auto type = auth_type_from_string(node.at("auth_type").as_string("none"));
        m.auth.type = type.value_or(AuthType::None);
        m.auth.secret_id = node.at("secret_id").as_string("");
        m.auth.param_name = node.at("auth_param").as_string("");
    }

    const auto& tags = node.at("tags");
    if (tags.is_array()) {
        for (const auto& t : tags.as_array()) {
            if (t.is_string()) {
                m.tags.push_back(t.as_string());
            }
        }
    }
    return m;
}

xscope::utils::Json SearchRegistry::module_to_json(const SearchModule& module) {
    xscope::utils::Json::Object auth;
    auth.emplace("type", std::string(auth_type_to_string(module.auth.type)));
    auth.emplace("secret_id", module.auth.secret_id);
    auth.emplace("param_name", module.auth.param_name);

    xscope::utils::Json::Array tags;
    for (const auto& t : module.tags) {
        tags.emplace_back(t);
    }

    xscope::utils::Json::Object obj;
    obj.emplace("id", module.id);
    obj.emplace("name", module.name);
    obj.emplace("description", module.description);
    obj.emplace("enabled", module.enabled);
    obj.emplace("skill_id", module.skill_id);
    obj.emplace("requires_api_key", module.requires_api_key);
    obj.emplace("auth", xscope::utils::Json(std::move(auth)));
    obj.emplace("tags", xscope::utils::Json(std::move(tags)));
    return xscope::utils::Json(std::move(obj));
}

} // namespace xscope::registry
