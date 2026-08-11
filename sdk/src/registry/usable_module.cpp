#include "xscope/registry/usable_module.hpp"

#include "xscope/utils/path.hpp"

#include <sstream>

namespace xscope::registry {

std::vector<UsableSearchModule> list_usable_search_modules(
    const SearchRegistry& registry, const skills::SkillStore& skills, bool require_secret_present,
    const std::function<bool(const std::string& secret_id)>& has_secret) {
    std::vector<UsableSearchModule> out;
    for (const auto& module : registry.list()) {
        if (!module.enabled) {
            continue;
        }
        const auto check = registry.validate(module, &skills, false, {});
        if (!check.ok) {
            continue;
        }
        bool secret_ok = true;
        if (module.requires_api_key) {
            secret_ok = has_secret && !module.auth.secret_id.empty() && has_secret(module.auth.secret_id);
            if (require_secret_present && !secret_ok) {
                continue;
            }
        }
        UsableSearchModule item;
        item.module = module;
        item.skill = skills.load(module.skill_id);
        item.secret_configured = !module.requires_api_key || secret_ok;
        out.push_back(std::move(item));
    }
    return out;
}

utils::Json usable_module_to_json(const UsableSearchModule& item) {
    utils::Json::Object auth;
    auth.emplace("type", std::string(auth_type_to_string(item.module.auth.type)));
    auth.emplace("secret_id", item.module.auth.secret_id);
    auth.emplace("param_name", item.module.auth.param_name);

    utils::Json::Array tags;
    for (const auto& t : item.module.tags) {
        tags.emplace_back(t);
    }

    utils::Json::Object skill;
    skill.emplace("id", item.skill.info.id);
    skill.emplace("name", item.skill.info.name);
    skill.emplace("description", item.skill.info.description);
    skill.emplace("path", xscope::utils::path_to_utf8(item.skill.info.path));
    skill.emplace("updated_at", static_cast<std::int64_t>(item.skill.info.updated_at));
    skill.emplace("is_directory", item.skill.info.is_directory);
    skill.emplace("frontmatter", item.skill.frontmatter);
    skill.emplace("body", item.skill.body);
    skill.emplace("raw", item.skill.raw);

    utils::Json::Object obj;
    obj.emplace("id", item.module.id);
    obj.emplace("name", item.module.name);
    obj.emplace("description", item.module.description);
    obj.emplace("enabled", item.module.enabled);
    obj.emplace("skill_id", item.module.skill_id);
    obj.emplace("requires_api_key", item.module.requires_api_key);
    obj.emplace("secret_configured", item.secret_configured);
    obj.emplace("auth", utils::Json(std::move(auth)));
    obj.emplace("tags", utils::Json(std::move(tags)));
    obj.emplace("skill", utils::Json(std::move(skill)));
    return utils::Json(std::move(obj));
}

utils::Json usable_modules_to_json(const std::vector<UsableSearchModule>& items) {
    utils::Json::Array arr;
    for (const auto& item : items) {
        arr.push_back(usable_module_to_json(item));
    }
    utils::Json::Object root;
    root.emplace("schema", 1);
    root.emplace("policy", std::string("full_fidelity"));
    root.emplace("count", static_cast<std::int64_t>(items.size()));
    root.emplace("modules", utils::Json(std::move(arr)));
    return utils::Json(std::move(root));
}

std::string format_usable_modules_for_prompt(const std::vector<UsableSearchModule>& items) {
    std::ostringstream oss;
    oss << "# Available search modules (full fidelity)\n"
        << "# Do not invent module ids outside this list.\n"
        << "# Each module includes complete SKILL.md content for precise research use.\n"
        << "# Module count: " << items.size() << "\n\n";

    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const auto& m = item.module;
        oss << "======== SEARCH MODULE [" << (i + 1) << "/" << items.size() << "] ========\n"
            << "id: " << m.id << '\n'
            << "name: " << m.name << '\n'
            << "description: " << m.description << '\n'
            << "enabled: " << (m.enabled ? "true" : "false") << '\n'
            << "skill_id: " << m.skill_id << '\n'
            << "requires_api_key: " << (m.requires_api_key ? "true" : "false") << '\n'
            << "secret_configured: " << (item.secret_configured ? "true" : "false") << '\n'
            << "auth.type: " << auth_type_to_string(m.auth.type) << '\n'
            << "auth.secret_id: " << m.auth.secret_id << '\n'
            << "auth.param_name: " << m.auth.param_name << '\n'
            << "tags:";
        if (m.tags.empty()) {
            oss << " (none)\n";
        } else {
            oss << '\n';
            for (const auto& t : m.tags) {
                oss << "  - " << t << '\n';
            }
        }
        oss << "---- SKILL FILE (complete raw) id=" << item.skill.info.id
            << " name=" << item.skill.info.name << " ----\n"
            << item.skill.raw;
        if (!item.skill.raw.empty() && item.skill.raw.back() != '\n') {
            oss << '\n';
        }
        oss << "---- END SKILL FILE ----\n"
            << "======== END SEARCH MODULE " << m.id << " ========\n\n";
    }
    return oss.str();
}

} // namespace xscope::registry
