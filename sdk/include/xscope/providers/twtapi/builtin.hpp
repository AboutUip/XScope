#pragma once

#include "xscope/registry/search_registry.hpp"
#include "xscope/skills/skill_store.hpp"

namespace xscope::providers::twtapi {

/// Complete SKILL.md for TwtAPI (Twitter/X public data).
const char* twtapi_skill_markdown();

/// Idempotent: write skill + upsert search module `twtapi`.
void ensure_twtapi_search_module(skills::SkillStore& skills, registry::SearchRegistry& registry);

} // namespace xscope::providers::twtapi
