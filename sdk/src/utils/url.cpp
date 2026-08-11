#include "xscope/utils/url.hpp"

#include <cctype>
#include <cstdio>

namespace xscope::utils {

std::string url_encode(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out += "%20";
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string build_query(const std::vector<std::pair<std::string, std::string>>& params) {
    std::string out;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) {
            out.push_back('&');
        }
        out += url_encode(params[i].first);
        out.push_back('=');
        out += url_encode(params[i].second);
    }
    return out;
}

std::string url_with_query(std::string_view base_url,
                           const std::vector<std::pair<std::string, std::string>>& params) {
    std::string out(base_url);
    if (params.empty()) {
        return out;
    }
    const auto q = build_query(params);
    out.push_back(out.find('?') == std::string::npos ? '?' : '&');
    out += q;
    return out;
}

} // namespace xscope::utils
