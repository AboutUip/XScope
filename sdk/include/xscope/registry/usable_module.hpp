#pragma once

#include "xscope/registry/search_module.hpp"
#include "xscope/registry/search_registry.hpp"
#include "xscope/skills/skill_store.hpp"
#include "xscope/utils/json.hpp"

#include <functional>
#include <string>
#include <vector>

namespace xscope::registry {

/// Full-fidelity usable search module for prompts / MCP (no summarization).
struct UsableSearchModule {
    SearchModule module;
    skills::SkillDocument skill; // complete SKILL.md (frontmatter + body + raw)
    bool secret_configured = false;
};

/// Build the usable list: enabled modules whose skill file exists.
/// When require_secret_present is true, modules needing a missing API key are excluded.
std::vector<UsableSearchModule> list_usable_search_modules(
    const SearchRegistry& registry, const skills::SkillStore& skills,
    bool require_secret_present = false,
    const std::function<bool(const std::string& secret_id)>& has_secret = {});

/// Serialize one/all usable modules to JSON without dropping fields or skill text.
utils::Json usable_module_to_json(const UsableSearchModule& item);
utils::Json usable_modules_to_json(const std::vector<UsableSearchModule>& items);

/// Plain-text block for prompt injection (complete module metadata + full skill raw).
std::string format_usable_modules_for_prompt(const std::vector<UsableSearchModule>& items);

} // namespace xscope::registry
