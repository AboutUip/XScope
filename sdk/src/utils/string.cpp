#include "xscope/utils/string.hpp"

#include <cctype>
#include <cstdio>

namespace xscope::utils {

std::string trim_copy(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

std::string trim_copy(std::string s) { return trim_copy(std::string_view(s)); }

std::string strip_quotes(std::string_view s) {
    auto t = trim_copy(s);
    if (t.size() >= 2) {
        const char a = t.front();
        const char b = t.back();
        if ((a == '"' && b == '"') || (a == '\'' && b == '\'')) {
            t = t.substr(1, t.size() - 2);
        }
    }
    return t;
}

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

std::string sanitize_id(std::string_view id) {
    std::string out;
    out.reserve(id.size());
    for (unsigned char c : id) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('-');
        }
    }
    while (!out.empty() && (out.front() == '.' || out.front() == '-')) {
        out.erase(out.begin());
    }
    return out;
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto pos = text.find('\n', start);
        if (pos == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        auto line = text.substr(start, pos - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.emplace_back(line);
        start = pos + 1;
    }
    return lines;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace xscope::utils
