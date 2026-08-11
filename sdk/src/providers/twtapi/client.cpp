#include "xscope/providers/twtapi/client.hpp"

#include "xscope/utils/string.hpp"
#include "xscope/utils/url.hpp"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace xscope::providers::twtapi {
namespace {

std::string lower_copy(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

std::string strip_spaces(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            out.push_back(c);
        }
    }
    return out;
}

/// Map docs titles / aliases → API path segment under /api/v1/twitter/.
const std::unordered_map<std::string, std::string>& endpoint_aliases() {
    static const std::unordered_map<std::string, std::string> k = {
        {"status", "status"},
        {"accountstatus", "status"},
        {"account-status", "status"},
        {"myapi/status", "status"},

        {"trends", "Trends"},
        {"search", "Search"},
        {"tweet-search", "Search"},
        {"tweetsearch", "Search"},
        {"autocomplete", "AutoComplete"},

        {"usernametouserid", "UsernameToUserId"},
        {"username-to-userid", "UsernameToUserId"},
        {"userbyscreenname", "UserResultByScreenName"},
        {"userresultbyscreenname", "UserResultByScreenName"},
        {"userbyrestid", "UserResultByRestId"},
        {"userresultbyrestid", "UserResultByRestId"},

        {"usertweets", "UserTweets"},
        {"user-tweets", "UserTweets"},
        {"usertweetsreplies", "UserTweetsReplies"},
        {"usertweetsandreplies", "UserTweetsReplies"},
        {"usermedia", "UserMedia"},
        {"userlikes", "UserLikes"},
        {"userlikeslimited", "UserLikes"},
        {"userfollowers", "UserFollowers"},
        {"followerslight", "FollowersLight"},
        {"followersids", "FollowersIds"},
        {"userverifiedfollowers", "UserVerifiedFollowers"},
        {"usersubscriptions", "UserSubscriptions"},
        {"translateprofile", "TranslateProfile"},
        {"userfollowing", "UserFollowing"},
        {"followinglight", "FollowingLight"},
        {"followingids", "FollowingIds"},

        {"tweetdetailconversation", "TweetDetailConversation"},
        {"tweetdetailconversationv2", "TweetDetailConversationv2"},
        {"tweetdetail", "TweetDetail"},
        {"tweetdetailv2", "TweetDetailv2"},
        {"tweetdetailv3", "TweetDetailv3"},
        {"tweetresultsbyrestids", "TweetResultsByRestIds"},
        {"tweetfavoriters", "TweetFavoriters"},
        {"tweetfavoriterslimited", "TweetFavoriters"},
        {"tweetretweeters", "TweetRetweeters"},
        {"tweetquotes", "TweetQuotes"},
        {"translatetweet", "TranslateTweet"},
        {"tweetarticle", "TweetArticle"},

        {"listsearch", "ListSearch"},
        {"listsearchtweet", "Search"},
        {"listtweets", "ListTweetsTimeline"},
        {"listtweetstimeline", "ListTweetsTimeline"},
        {"listsubscribers", "ListSubscribersTimeline"},
        {"listsubscriberstimeline", "ListSubscribersTimeline"},
        {"listmembers", "ListMembersTimeline"},
        {"listmemberstimeline", "ListMembersTimeline"},

        {"communitymembers", "CommunityMembers"},
        {"communitymoderators", "CommunityModerators"},
        {"communitysearch", "CommunitiesSearchSlice"},
        {"communitiessearchslice", "CommunitiesSearchSlice"},
        {"communityinfo", "CommunityResultsById"},
        {"communityresultsbyid", "CommunityResultsById"},
        {"communitytimeline", "CommunityTimeline"},
        {"communitymediatimeline", "CommunityMediaTimeline"},
        {"communitymembersearch", "CommunityMemberSearch"},
        {"communityabouttimeline", "CommunityAboutTimeline"},
    };
    return k;
}

} // namespace

Client::Client(network::HttpClient& http, TokenFn token_fn, ClientOptions options)
    : http_(http), token_fn_(std::move(token_fn)), options_(std::move(options)) {}

bool Client::is_status_endpoint(const std::string& endpoint) {
    const auto n = normalize_endpoint(endpoint);
    return n == "status";
}

std::string Client::normalize_endpoint(std::string endpoint) {
    endpoint = utils::trim_copy(endpoint);
    if (endpoint.empty()) {
        return "Search";
    }
    // Allow raw path segments already in API form.
    if (!endpoint.empty() && endpoint.front() == '/') {
        endpoint.erase(endpoint.begin());
    }
    if (endpoint.rfind("api/v1/twitter/", 0) == 0) {
        endpoint = endpoint.substr(std::string("api/v1/twitter/").size());
    }
    // Drop accidental query string.
    const auto qpos = endpoint.find('?');
    if (qpos != std::string::npos) {
        endpoint = endpoint.substr(0, qpos);
    }

    auto key = lower_copy(strip_spaces(endpoint));
    // Remove & from "User Tweets & Replies" style after strip → usertweets&replies
    std::string key2;
    for (char c : key) {
        if (c != '&' && c != '(' && c != ')' && c != '/') {
            key2.push_back(c);
        }
    }
    key = std::move(key2);

    const auto& aliases = endpoint_aliases();
    if (auto it = aliases.find(key); it != aliases.end()) {
        return it->second;
    }

    // Already a known PascalCase path segment (Search, UserTweets, …).
    if (!endpoint.empty() && std::isupper(static_cast<unsigned char>(endpoint.front()))) {
        return endpoint;
    }
    return "Search";
}

network::HttpResponse Client::status() {
    return get("status", {});
}

network::HttpResponse Client::get(const std::string& endpoint,
                                  const std::vector<std::pair<std::string, std::string>>& query) {
    const auto ep = normalize_endpoint(endpoint);
    std::string path;
    if (ep == "status") {
        path = "/myapi/status";
    } else {
        path = "/api/v1/twitter/" + ep;
    }
    auto url = options_.api_base;
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    url += path;

    std::vector<std::pair<std::string, std::string>> params = query;
    bool has_lang = false;
    for (const auto& p : params) {
        if (p.first == "lang") {
            has_lang = true;
            break;
        }
    }
    if (!has_lang && !options_.lang.empty()) {
        params.emplace_back("lang", options_.lang);
    }
    return get_url(utils::url_with_query(url, params));
}

network::HttpResponse Client::get_url(const std::string& url) {
    auto token = token_fn_ ? token_fn_() : std::nullopt;
    if (!token || token->empty()) {
        throw network::NetworkError("missing TwtAPI API key (secret twtapi.default)");
    }

    network::HttpRequest req;
    req.method = network::HttpMethod::Get;
    req.url = url;
    // Docs console uses X-API-Key; marketing samples also accept Bearer.
    req.headers.emplace_back("X-API-Key", *token);
    req.headers.emplace_back("Authorization", "Bearer " + *token);
    if (!options_.lang.empty()) {
        req.headers.emplace_back("X-Lang", options_.lang);
    }
    req.headers.emplace_back("Accept", "application/json");
    if (!options_.user_agent.empty()) {
        req.headers.emplace_back("User-Agent", options_.user_agent);
    }
    req.timeout = std::chrono::seconds(60);
    return http_.send(req);
}

utils::Json Client::response_to_json(const network::HttpResponse& resp) {
    utils::Json::Object obj;
    obj.emplace("status", resp.status);
    utils::Json::Object headers;
    for (const auto& h : resp.headers) {
        const auto lower = lower_copy(h.first);
        if (lower == "content-type" || lower == "x-request-id" || lower == "retry-after" ||
            lower == "date") {
            headers.emplace(lower, h.second);
        }
    }
    obj.emplace("headers", utils::Json(std::move(headers)));
    const auto trimmed = utils::trim_copy(resp.body);
    if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[')) {
        try {
            obj.emplace("body", utils::Json::parse(resp.body));
            obj.emplace("body_format", std::string("json"));
        } catch (...) {
            obj.emplace("body", resp.body);
            obj.emplace("body_format", std::string("text"));
        }
    } else {
        obj.emplace("body", resp.body);
        obj.emplace("body_format", std::string("text"));
    }
    return utils::Json(std::move(obj));
}

} // namespace xscope::providers::twtapi
