#include "xscope/research/precision.hpp"

#include <sstream>

namespace xscope::research {

PrecisionKind precision_from_int(int v) noexcept {
    if (v <= 0) {
        return PrecisionKind::Quick;
    }
    if (v == 1) {
        return PrecisionKind::Normal;
    }
    if (v == 2) {
        return PrecisionKind::Deep;
    }
    return PrecisionKind::Maximum;
}

int precision_to_int(PrecisionKind k) noexcept {
    return static_cast<int>(k);
}

const char* precision_to_string(PrecisionKind k) noexcept {
    switch (k) {
    case PrecisionKind::Quick:
        return "quick";
    case PrecisionKind::Normal:
        return "normal";
    case PrecisionKind::Deep:
        return "deep";
    case PrecisionKind::Maximum:
        return "maximum";
    }
    return "normal";
}

ResearchBudget budget_for(PrecisionKind kind) noexcept {
    ResearchBudget b;
    b.kind = kind;
    switch (kind) {
    case PrecisionKind::Quick:
        b.max_depth_layers = 3;
        b.max_directions = 4;
        b.items_per_layer = 5;
        break;
    case PrecisionKind::Normal:
        b.max_depth_layers = 5;
        b.max_directions = 6;
        b.items_per_layer = 10;
        break;
    case PrecisionKind::Deep:
        b.max_depth_layers = 10;
        b.max_directions = 8;
        b.items_per_layer = 15;
        break;
    case PrecisionKind::Maximum:
        b.max_depth_layers = -1;
        b.max_directions = 16;
        b.items_per_layer = 15;
        break;
    }
    return b;
}

std::string policy_prompt_text(const ResearchBudget& budget) {
    std::ostringstream oss;
    oss << "Research precision policy (ENGINE-ENFORCED):\n"
        << "- precision: " << precision_to_string(budget.kind) << "\n"
        << "- DEPTH vs BREADTH: a \"layer\" is one deeper step along ONE investigation direction. "
           "Breadth = how many directions you open; depth = how deep each direction goes.\n";
    if (budget.max_depth_layers < 0) {
        oss << "- max depth layers per direction: unlimited (must be VERY thorough)\n";
    } else {
        oss << "- max depth layers per direction: " << budget.max_depth_layers << "\n";
    }
    oss << "- soft max directions (breadth): " << budget.max_directions << "\n"
        << "- items/detail per deepen step: " << budget.items_per_layer << "\n"
        << "- No forced minimum depth; you may stop a direction early if evidence is enough.\n"
        << "- If the need is GitHub-related, prefer github run_search + github_rest/code and dig into "
           "code-level detail (files, APIs, protocols), especially at Maximum.\n"
        << "- User clarify/choice turns do NOT consume depth layers.\n"
        << "- Knowledge nodes: only emit valid=true knowledge for the project knowledge graph.\n"
        << "- During deep research you MAY ask_user (question/choice) to verify or adjust directions.\n"
        << "\n## Stage memory (radiating tree) & catalogs (MANDATORY in deep research)\n"
        << "- Memory is a radiating tree of branches (side-paths / follow-ups), not a flat list.\n"
        << "- The engine does NOT dump full memory bodies into your context.\n"
        << "- EVERY deep-research turn you MUST obtain BOTH directories first:\n"
        << "  1) memory catalog (branch + entry directory)\n"
        << "  2) knowledge-graph catalog (node/edge directory)\n"
        << "  (Engine also refreshes these directories into the turn; you still decide what to open.)\n"
        << "- Selecting which memory/knowledge bodies to read is YOUR choice, guided by precision:\n";
    switch (budget.kind) {
    case PrecisionKind::Quick:
        oss << "  * QUICK: only the current explicit task, or SHALLOW related memory on the current "
               "branch. Do not roam other side-paths.\n";
        break;
    case PrecisionKind::Normal:
        oss << "  * NORMAL: read related dependency memories and/or the FULL chain on the current "
               "branch. Side-path directories may be skimmed; open bodies only if clearly relevant.\n";
        break;
    case PrecisionKind::Deep:
        oss << "  * DEEP: read the FULL memory chain on the current path, PLUS side-path "
               "(other branch) directories, and open those bodies on demand.\n";
        break;
    case PrecisionKind::Maximum:
        oss << "  * MAXIMUM: NO read constraints. Complete the task — push reading ALL related "
               "memories and use the knowledge graph frequently (add/link/update).\n";
        break;
    }
    return oss.str();
}

} // namespace xscope::research
