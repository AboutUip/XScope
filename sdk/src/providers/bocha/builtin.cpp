#include "xscope/providers/bocha/builtin.hpp"

namespace xscope::providers::bocha {

const char* bocha_skill_markdown() {
    return R"MD(---
name: bocha-search
description: >
  Bocha (博查) Web Search + AI Search via Bearer API key.
  Prefer complete search payloads for research evidence; never truncate result lists for token savings.
---

# Bocha search (XScope)

Use this module for open-web research: page titles, URLs, snippets/summaries, site metadata,
and (via AI Search) vertical structured cards plus optional model answers.

## Authentication

1. Operator stores a Bocha API key as secret `bocha.default` (settings UI or `xscope_secret_put` /
   `xscope_search_module_set_api_key`).
2. Obtain keys at https://open.bocha.cn (or https://open.bochaai.com).
3. Never echo the raw API key in tool output.

Header used by the SDK: `Authorization: Bearer <api-key>`.

## Tools

### `run_search` (module_id = `bocha`)

Arguments:
- `module_id`: must be `"bocha"`
- `endpoint`: `"web-search"` or `"ai-search"`
- `q` (or `query`): natural-language search query — do not truncate for token savings
- optional shared: `freshness` (`noLimit`|`oneDay`|`oneWeek`|`oneMonth`|`oneYear`|…), `count` (1–50)
- web-search optional: `summary` (bool) — request text summaries
- ai-search optional: `answer` (bool), `stream` (bool; keep `false` unless a streaming consumer is ready)

POST targets:
- `https://api.bocha.cn/v1/web-search`
- `https://api.bocha.cn/v1/ai-search`

Returns the **complete** HTTP status, selected headers, and JSON body. Keep identifiers
(`name`, `url`, `snippet`/`summary`, `siteName`, `datePublished`) for citations.

## Endpoint guide

### web-search (通搜)

Best for: general web evidence, link lists, timed freshness filters.
Typical body fields: `query`, `freshness`, `summary`, `count`.

### ai-search (AI 搜)

Best for: queries that benefit from vertical cards (weather, encyclopedia, …) and optional
LLM answers. Typical body fields: `query`, `freshness`, `count`, `answer`, `stream`.

Prefer `answer=false` when you only need sources for downstream research synthesis.

## Research accuracy policy

- Prefer **more complete evidence** over shorter answers.
- Keep raw API payloads (or full extracted page text) in the research project when they support claims.
- Do not summarize SKILL content or strip search hits to save tokens.
- When listing candidates, retain URLs and titles for follow-up fetches.

## Suggested workflow

1. Confirm the Bocha module is enabled and `bocha.default` is configured.
2. Start with `endpoint=web-search` for broad evidence.
3. Use `endpoint=ai-search` when structured cards or a draft answer help.
4. Record URLs so results are reproducible.
)MD";
}

void ensure_bocha_search_module(skills::SkillStore& skills, registry::SearchRegistry& registry) {
    skills.save("bocha-search", bocha_skill_markdown());

    registry::SearchModule mod;
    mod.id = "bocha";
    mod.name = "Bocha";
    mod.description =
        "Bocha Web Search + AI Search (Bearer API key): open-web pages and vertical cards";
    mod.enabled = true;
    mod.skill_id = "bocha-search";
    mod.requires_api_key = true;
    mod.auth.type = registry::AuthType::Bearer;
    mod.auth.secret_id = "bocha.default";
    mod.auth.param_name = "Authorization";
    mod.tags = {"bocha", "web", "ai-search", "bearer"};
    registry.upsert(mod);
    registry.save();
}

} // namespace xscope::providers::bocha
