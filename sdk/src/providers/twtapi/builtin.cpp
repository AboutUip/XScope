#include "xscope/providers/twtapi/builtin.hpp"

namespace xscope::providers::twtapi {

const char* twtapi_skill_markdown() {
    return R"MD(---
name: twtapi-search
description: >
  TwtAPI public Twitter/X data: tweet search, user lookup, timelines, lists, communities.
  Use module_id=twtapi whenever the research need involves Twitter, X.com, tweets, hashtags,
  @handles, or social listening on X. Prefer complete payloads; never truncate result lists.
---

# TwtAPI (XScope) — Twitter / X search

**When to use:** Twitter、X、推特、推文、hashtag、@用户、粉丝/关注、社媒舆情相关需求。
**When NOT to use:** general web facts (use `bocha`); GitHub repos/code (use `github`).
**Gate:** only call when this module's `secret_configured` is true in the injected search-modules
block. If false, tell the user to add a TwtAPI key in Settings — do not invent results.

Docs: https://www.twtapi.com/zh/docs/

## XScope orchestrator call shape (IMPORTANT)

The research engine does **not** call TwtAPI HTTP itself from free text. You MUST emit JSON
`action=search` with `module_id="twtapi"`. Examples:

Tweet keyword / hashtag / mention search (default start):
```json
{"thinking":"...","action":"search","searches":[{"module_id":"twtapi","endpoint":"Search","q":"(OpenAI OR #AI) lang:en","type":"Latest","count":20}]}
```

Deep research single search turn:
```json
{"thinking":"...","action":"search","module_id":"twtapi","endpoint":"Search","q":"from:solana","type":"Latest","count":20,"direction_id":"d1"}
```

Resolve @handle → profile:
```json
{"thinking":"...","action":"search","module_id":"twtapi","endpoint":"UserByScreenName","username":"solana"}
```

User timeline (needs numeric `user_id` from UsernameToUserId / UserByScreenName):
```json
{"thinking":"...","action":"search","module_id":"twtapi","endpoint":"UserTweets","user_id":"951329744804392960"}
```

Tweet detail:
```json
{"thinking":"...","action":"search","module_id":"twtapi","endpoint":"TweetDetail","tweet_id":"1768778186186195177"}
```

Trends (WOEID; 1=Worldwide):
```json
{"thinking":"...","action":"search","module_id":"twtapi","endpoint":"Trends","woeid":1}
```

Account / credits check:
```json
{"thinking":"...","action":"search","module_id":"twtapi","endpoint":"status"}
```

Aliases accepted for `module_id`: `twtapi` | `twitter` | `x` (normalized to `twtapi`).

## Authentication

1. Operator stores a TwtAPI API key as secret `twtapi.default`
   (Settings → Search modules, or `xscope_search_module_set_api_key`).
2. Obtain keys at https://www.twtapi.com (Dashboard).
3. Never echo the raw API key in tool output.

Headers used by the SDK:
- `X-API-Key: <api-key>` (primary; matches docs console)
- `Authorization: Bearer <api-key>` (also accepted by TwtAPI)
- `X-Lang: zh|en`

Base URL: `https://api.twtapi.com`

## Tools

### `run_search` (module_id = `twtapi`)

Arguments:
- `module_id`: must be `"twtapi"` (aliases: `twitter`, `x`)
- `endpoint`: TwtAPI endpoint name (see catalog below). Default: `Search`
- Query parameters (pass as top-level tool / search-object fields; only non-empty values are sent):
  - `q` / `query` — search text or advanced operators (`from:user`, `#tag`, `list:ID`, …)
  - `type` — Search result type: `Top` | `Latest` | `User` | `Image` | `Video`
  - `count` — result count (default ~20)
  - `username` / `screen_name` — screen name without `@`
  - `user_id` — numeric Rest ID
  - `tweet_id` / `tweet_ids` — tweet Rest ID(s); batch uses comma-separated `tweet_ids`
  - `list_id`, `community_id`
  - `cursor` — pagination cursor
  - `woeid` — Trends region (1=Worldwide, 23424977=USA, …)
  - `language` — translate target (ISO 639-1)
  - `stringify_ids`, `safe_search`, `time_filter`

Returns the **complete** HTTP status + JSON body (`code`/`msg`/`data`, plus TwtAPI `_normalized`
helpers when present). Keep tweet ids, text, author, and URLs for citations.

## Endpoint catalog

### Account
| endpoint | path | notes |
|---|---|---|
| `status` / `AccountStatus` | `GET /myapi/status` | credits, plan, rate limits |

### Explore
| endpoint | path | required params |
|---|---|---|
| `Trends` | `/api/v1/twitter/Trends` | `woeid` |
| `Search` | `/api/v1/twitter/Search` | `q` ; optional `type`,`count` |
| `AutoComplete` | `/api/v1/twitter/AutoComplete` | `q` |

### User
| endpoint | path | required params |
|---|---|---|
| `UsernameToUserId` | `/UsernameToUserId` | `username` |
| `UserByScreenName` / `UserResultByScreenName` | `/UserResultByScreenName` | `username` |
| `UserByRestId` / `UserResultByRestId` | `/UserResultByRestId` | `user_id` |
| `UserTweets` | `/UserTweets` | `user_id` ; optional `cursor` |
| `UserTweetsReplies` | `/UserTweetsReplies` | `user_id` |
| `UserMedia` | `/UserMedia` | `user_id` |
| `UserLikes` | `/UserLikes` | `user_id` (often empty after X privacy changes) |
| `UserFollowers` | `/UserFollowers` | `user_id` |
| `FollowersLight` | `/FollowersLight` | `username` or `user_id` |
| `FollowersIds` | `/FollowersIds` | `username` ; optional `count`,`stringify_ids` |
| `UserVerifiedFollowers` | `/UserVerifiedFollowers` | `user_id` |
| `UserSubscriptions` | `/UserSubscriptions` | `user_id` |
| `TranslateProfile` | `/TranslateProfile` | `user_id`,`language` |
| `UserFollowing` | `/UserFollowing` | `user_id` |
| `FollowingLight` | `/FollowingLight` | `username` |
| `FollowingIds` | `/FollowingIds` | `username` |

### Tweet
| endpoint | path | required params |
|---|---|---|
| `TweetDetailConversation` | `/TweetDetailConversation` | `tweet_id` |
| `TweetDetailConversationv2` | `/TweetDetailConversationv2` | `tweet_id` |
| `TweetDetail` / `v2` / `v3` | `/TweetDetail{,v2,v3}` | `tweet_id` |
| `TweetResultsByRestIds` | `/TweetResultsByRestIds` | `tweet_ids` (comma-separated) |
| `TweetFavoriters` | `/TweetFavoriters` | `tweet_id` (often empty) |
| `TweetRetweeters` | `/TweetRetweeters` | `tweet_id` |
| `TweetQuotes` | `/TweetQuotes` | `tweet_id` |
| `TranslateTweet` | `/TranslateTweet` | `tweet_id`,`language` |
| `TweetArticle` | `/TweetArticle` | `tweet_id` |

### List
| endpoint | path | required params |
|---|---|---|
| `ListSearch` | `/ListSearch` | `q` |
| `ListTweets` / `ListTweetsTimeline` | `/ListTweetsTimeline` | `list_id` |
| `ListSubscribers` | `/ListSubscribersTimeline` | `list_id` |
| `ListMembers` | `/ListMembersTimeline` | `list_id` |
| List tweet search | use `Search` with `q=list:<listID> <keyword>` | |

### Community
| endpoint | path | required params |
|---|---|---|
| `CommunityMembers` | `/CommunityMembers` | `community_id` |
| `CommunityModerators` | `/CommunityModerators` | `community_id` |
| `CommunitySearch` / `CommunitiesSearchSlice` | `/CommunitiesSearchSlice` | `q` |
| `CommunityInfo` / `CommunityResultsById` | `/CommunityResultsById` | `community_id` |
| `CommunityTimeline` | `/CommunityTimeline` | `community_id` |
| `CommunityMediaTimeline` | `/CommunityMediaTimeline` | `community_id` |
| `CommunityMemberSearch` | `/CommunityMemberSearch` | `community_id`,`q` |
| `CommunityAboutTimeline` | `/CommunityAboutTimeline` | `community_id` |

## Suggested research workflow

1. Confirm module enabled + `secret_configured=true`; optional `endpoint=status` to check credits.
2. Start with `endpoint=Search`, `type=Latest`, realistic `q` (brand, `#tag`, `from:user`, language ops).
3. Enrich authors via `UserByScreenName` / `UsernameToUserId` → `UserTweets`.
4. Inspect important posts with `TweetDetail` / `TweetDetailConversation`.
5. Prefer `_normalized.tweets` / `tweet_ids` when present; keep `rest_id` for stable citations
   (`https://x.com/i/status/<rest_id>`).

## Accuracy policy

- Prefer **more complete evidence** over short summaries.
- Do not strip tweet ids, timestamps, or author handles.
- On `code=401/402/429`, stop hammering; report the error and switch modules if needed.
)MD";
}

void ensure_twtapi_search_module(skills::SkillStore& skills, registry::SearchRegistry& registry) {
    skills.save("twtapi-search", twtapi_skill_markdown());

    registry::SearchModule mod;
    mod.id = "twtapi";
    mod.name = "TwtAPI";
    mod.description =
        "TwtAPI Twitter/X public data (X-API-Key): search, users, timelines, lists, communities";
    mod.enabled = true;
    mod.skill_id = "twtapi-search";
    mod.requires_api_key = true;
    mod.auth.type = registry::AuthType::ApiKey;
    mod.auth.secret_id = "twtapi.default";
    mod.auth.param_name = "X-API-Key";
    mod.tags = {"twtapi", "twitter", "x", "social", "api-key"};
    registry.upsert(mod);
    registry.save();
}

} // namespace xscope::providers::twtapi
