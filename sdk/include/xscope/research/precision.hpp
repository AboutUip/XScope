#pragma once

#include <string>

namespace xscope::research {

enum class PrecisionKind {
    Quick = 0,
    Normal = 1,
    Deep = 2,
    Maximum = 3,
};

/// Precision limits **depth along one investigation direction**, not breadth.
struct ResearchBudget {
    PrecisionKind kind = PrecisionKind::Normal;
    /// Max depth layers per direction. -1 = unlimited (Maximum).
    int max_depth_layers = 5;
    /// Soft cap on concurrent/total breadth directions (engine safety, not user precision).
    int max_directions = 8;
    /// Hits / REST richness per deepen step.
    int items_per_layer = 10;
};

PrecisionKind precision_from_int(int v) noexcept;
int precision_to_int(PrecisionKind k) noexcept;
const char* precision_to_string(PrecisionKind k) noexcept;

ResearchBudget budget_for(PrecisionKind kind) noexcept;

/// AUTHORITATIVE policy text injected into research AI calls.
std::string policy_prompt_text(const ResearchBudget& budget);

} // namespace xscope::research
