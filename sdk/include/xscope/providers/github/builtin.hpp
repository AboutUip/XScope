#pragma once

#include "xscope/registry/search_registry.hpp"
#include "xscope/skills/skill_store.hpp"

namespace xscope::providers::github {

/// Complete SKILL.md body for the GitHub search module (full fidelity, no summarization).
const char* github_skill_markdown();

/// Install/refresh github skill + registry module (id=github, auth=oauth, secret_id=github.oauth).
void ensure_github_search_module(skills::SkillStore& skills, registry::SearchRegistry& registry);

} // namespace xscope::providers::github
