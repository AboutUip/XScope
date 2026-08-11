#pragma once

#include "xscope/ai/provider_registry.hpp"

namespace xscope::ai {

/// Seed DeepSeek + Kimi (Moonshot) providers if missing (idempotent upsert).
/// Models are populated via AiRuntime::refresh_models_from_api (vendor /models).
void ensure_builtin_ai_providers(ProviderRegistry& registry);

} // namespace xscope::ai
