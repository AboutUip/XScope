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
    b.ignore_cost = false;
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
        // Essential difference: no cost/time budget — research until accuracy is enough.
        b.max_depth_layers = -1;
        b.max_directions = -1;
        b.items_per_layer = 20;
        b.ignore_cost = true;
        break;
    }
    return b;
}

std::string policy_prompt_text(const ResearchBudget& budget) {
    std::ostringstream oss;
    oss << "Research precision policy (ENGINE-ENFORCED):\n"
        << "- precision: " << precision_to_string(budget.kind) << "\n"
        << "- DEPTH vs BREADTH: a \"layer\" is one deeper step along ONE investigation direction. "
           "Breadth = how many directions you open; depth = how deep each direction goes. "
           "Precision is ONE knob covering both — not two separate systems.\n";

    if (budget.ignore_cost || budget.kind == PrecisionKind::Maximum) {
        oss << "\n## MAXIMUM — essential difference from all other tiers\n"
            << "- IGNORE cost, token spend, and wall-clock time completely.\n"
            << "- Prioritize RESULT ACCURACY over speed.\n"
            << "- You MAY and SHOULD keep researching indefinitely until evidence is solid enough "
               "to write an accurate report.\n"
            << "- Depth layers and direction count are UNLIMITED — dig deeper, open new angles, "
               "read code/files/APIs, cross-check sources.\n"
            << "- NEVER stop early to \"save tokens\" or because \"enough turns passed\".\n"
            << "- NEVER repeat the same search query/module/endpoint. If a query already ran, "
               "choose a STRICTLY deeper or different angle (new path, file, API, contradiction, "
               "source) or synthesize.\n"
            << "- synthesize ONLY when you can defend accuracy; until then, deepen.\n\n";
    }

    if (budget.max_depth_layers < 0) {
        oss << "- max depth layers per direction: unlimited\n";
    } else {
        oss << "- max depth layers per direction: " << budget.max_depth_layers << "\n";
    }
    if (budget.max_directions < 0) {
        oss << "- max directions (breadth): unlimited\n";
    } else {
        oss << "- soft max directions (breadth): " << budget.max_directions << "\n";
    }
    oss << "- items/detail per deepen step: " << budget.items_per_layer << "\n";

    if (!budget.ignore_cost) {
        oss << "- Balance thoroughness against cost for this tier; stop a direction early if "
               "evidence is already enough.\n";
    }

    oss << "- If the need is GitHub-related, prefer github run_search + github_rest/code and dig into "
           "code-level detail (files, APIs, protocols)"
        << (budget.ignore_cost ? " — required at Maximum.\n" : ".\n")
        << "- User clarify/choice turns do NOT consume depth layers.\n"
        << "- Knowledge graph (YOU own it — engine never auto-builds):\n"
           "  * Emit action=knowledge with valid=true for EVERY meaningful, reusable finding "
           "(entities, definitions, sourced claims, APIs, protocols, decisions, relationships).\n"
           "  * Always include edges (cites/depends_on/part_of/supports/contradicts/related).\n"
           "  * Do not leave the graph empty when evidence is solid; omit only noise/guesses.\n"
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
               "branch. Do not roam other side-paths. Prefer few directions and shallow layers.\n";
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
        oss << "  * MAXIMUM: NO read constraints and NO cost constraints. Push reading ALL related "
               "memories; use the knowledge graph heavily (add/link/update). Keep deepening until "
               "the report would be accurate.\n";
        break;
    }
    return oss.str();
}

} // namespace xscope::research
