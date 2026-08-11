#pragma once

#include <string>

namespace xscope::research {

enum class PrecisionKind {
    Quick = 0,
    Normal = 1,
    Deep = 2,
    Maximum = 3,
};

/// Precision is one knob: how thorough the investigation should be.
/// Lower tiers trade cost for speed; Maximum turns cost off and prioritizes accuracy.
struct ResearchBudget {
    PrecisionKind kind = PrecisionKind::Normal;
    /// Max depth layers per direction. -1 = unlimited (Maximum only).
    int max_depth_layers = 5;
    /// Soft cap on investigation directions (breadth). -1 = unlimited (Maximum).
    int max_directions = 8;
    /// Hits / REST richness per deepen step.
    int items_per_layer = 10;
    /// When true (Maximum), the model must ignore cost/time and dig until accuracy is enough.
    bool ignore_cost = false;
};

PrecisionKind precision_from_int(int v) noexcept;
int precision_to_int(PrecisionKind k) noexcept;
const char* precision_to_string(PrecisionKind k) noexcept;

ResearchBudget budget_for(PrecisionKind kind) noexcept;

/// AUTHORITATIVE policy text injected into research AI calls.
std::string policy_prompt_text(const ResearchBudget& budget);

} // namespace xscope::research
