#pragma once

#include "xscope/registry/search_registry.hpp"
#include "xscope/skills/skill_store.hpp"

namespace xscope::providers::bocha {

/// Complete SKILL.md for Bocha web-search + ai-search (Bearer API key).
const char* bocha_skill_markdown();

/// Install/refresh bocha skill + registry module (id=bocha, auth=bearer, secret_id=bocha.default).
void ensure_bocha_search_module(skills::SkillStore& skills, registry::SearchRegistry& registry);

} // namespace xscope::providers::bocha
