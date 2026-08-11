#include "xscope/utils/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace xscope::utils {
namespace {

struct Parser {
    std::string_view text;
    std::size_t i = 0;

    [[noreturn]] void error(const char* msg) const { throw JsonError(msg); }

    void skip_ws() {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
    }

    char peek() {
        skip_ws();
        if (i >= text.size()) {
            error("unexpected end of JSON");
        }
        return text[i];
    }

    char get() {
        const char c = peek();
        ++i;
        return c;
    }

    bool match(char c) {
        skip_ws();
        if (i < text.size() && text[i] == c) {
            ++i;
            return true;
        }
        return false;
    }

    Json parse_value() {
        const char c = peek();
        if (c == 'n') {
            return parse_null();
        }
        if (c == 't' || c == 'f') {
            return parse_bool();
        }
        if (c == '"') {
            return Json(parse_string());
        }
        if (c == '[') {
            return parse_array();
        }
        if (c == '{') {
            return parse_object();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parse_number();
        }
        error("invalid JSON value");
    }

    Json parse_null() {
        if (text.substr(i, 4) != "null") {
            error("expected null");
        }
        i += 4;
        return Json(nullptr);
    }

    Json parse_bool() {
        if (text.substr(i, 4) == "true") {
            i += 4;
            return Json(true);
        }
        if (text.substr(i, 5) == "false") {
            i += 5;
            return Json(false);
        }
        error("expected boolean");
    }

    std::string parse_string() {
        if (get() != '"') {
            error("expected string");
        }
        std::string out;
        while (i < text.size()) {
            const char c = text[i++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (i >= text.size()) {
                    error("invalid escape");
                }
                const char e = text[i++];
                switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    if (i + 4 > text.size()) {
                        error("invalid unicode escape");
                    }
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = text[i++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') {
                            code |= static_cast<unsigned>(h - '0');
                        } else if (h >= 'a' && h <= 'f') {
                            code |= static_cast<unsigned>(h - 'a' + 10);
                        } else if (h >= 'A' && h <= 'F') {
                            code |= static_cast<unsigned>(h - 'A' + 10);
                        } else {
                            error("invalid unicode escape");
                        }
                    }
                    if (code <= 0x7F) {
                        out.push_back(static_cast<char>(code));
                    } else if (code <= 0x7FF) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default:
                    error("invalid escape");
                }
            } else {
                out.push_back(c);
            }
        }
        error("unterminated string");
    }

    Json parse_number() {
        const std::size_t start = i;
        if (match_raw('-')) {
        }
        if (match_raw('0')) {
        } else if (i < text.size() && text[i] >= '1' && text[i] <= '9') {
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
        } else {
            error("invalid number");
        }
        if (match_raw('.')) {
            if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) {
                error("invalid number");
            }
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
        }
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
            ++i;
            if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
                ++i;
            }
            if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) {
                error("invalid number");
            }
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
        }
        return Json(std::stod(std::string(text.substr(start, i - start))));
    }

    bool match_raw(char c) {
        if (i < text.size() && text[i] == c) {
            ++i;
            return true;
        }
        return false;
    }

    Json parse_array() {
        if (get() != '[') {
            error("expected array");
        }
        Json::Array arr;
        skip_ws();
        if (match(']')) {
            return Json(std::move(arr));
        }
        for (;;) {
            arr.push_back(parse_value());
            if (match(']')) {
                break;
            }
            if (!match(',')) {
                error("expected comma in array");
            }
        }
        return Json(std::move(arr));
    }

    Json parse_object() {
        if (get() != '{') {
            error("expected object");
        }
        Json::Object obj;
        skip_ws();
        if (match('}')) {
            return Json(std::move(obj));
        }
        for (;;) {
            const std::string key = parse_string();
            if (!match(':')) {
                error("expected colon");
            }
            obj.emplace(key, parse_value());
            if (match('}')) {
                break;
            }
            if (!match(',')) {
                error("expected comma in object");
            }
        }
        return Json(std::move(obj));
    }
};

void dump_string(std::ostringstream& oss, const std::string& s) {
    oss << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            oss << "\\\"";
            break;
        case '\\':
            oss << "\\\\";
            break;
        case '\b':
            oss << "\\b";
            break;
        case '\f':
            oss << "\\f";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                oss << buf;
            } else {
                oss << static_cast<char>(c);
            }
        }
    }
    oss << '"';
}

void dump_value(std::ostringstream& oss, const Json& v, int indent, int depth);

void dump_indent(std::ostringstream& oss, int indent, int depth) {
    if (indent <= 0) {
        return;
    }
    oss << '\n' << std::string(static_cast<std::size_t>(indent * depth), ' ');
}

void dump_value(std::ostringstream& oss, const Json& v, int indent, int depth) {
    if (v.is_null()) {
        oss << "null";
    } else if (v.is_bool()) {
        oss << (v.as_bool() ? "true" : "false");
    } else if (v.is_number()) {
        const double n = v.as_number();
        if (std::isfinite(n) && std::floor(n) == n && std::abs(n) < 1e15) {
            oss << static_cast<long long>(n);
        } else {
            oss << n;
        }
    } else if (v.is_string()) {
        dump_string(oss, v.as_string());
    } else if (v.is_array()) {
        const auto& arr = v.as_array();
        oss << '[';
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i) {
                oss << ',';
            }
            dump_indent(oss, indent, depth + 1);
            dump_value(oss, arr[i], indent, depth + 1);
        }
        if (!arr.empty()) {
            dump_indent(oss, indent, depth);
        }
        oss << ']';
    } else if (v.is_object()) {
        const auto& obj = v.as_object();
        oss << '{';
        std::size_t i = 0;
        for (const auto& [k, val] : obj) {
            if (i++) {
                oss << ',';
            }
            dump_indent(oss, indent, depth + 1);
            dump_string(oss, k);
            oss << (indent > 0 ? ": " : ":");
            dump_value(oss, val, indent, depth + 1);
        }
        if (!obj.empty()) {
            dump_indent(oss, indent, depth);
        }
        oss << '}';
    }
}

} // namespace

Json Json::parse(std::string_view text) {
    Parser p{text, 0};
    Json v = p.parse_value();
    p.skip_ws();
    if (p.i != p.text.size()) {
        throw JsonError("trailing data after JSON value");
    }
    return v;
}

std::string Json::dump(int indent) const {
    std::ostringstream oss;
    dump_value(oss, *this, indent, 0);
    if (indent > 0) {
        oss << '\n';
    }
    return oss.str();
}

bool Json::is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
bool Json::is_bool() const { return std::holds_alternative<bool>(data_); }
bool Json::is_number() const { return std::holds_alternative<double>(data_); }
bool Json::is_string() const { return std::holds_alternative<std::string>(data_); }
bool Json::is_array() const { return std::holds_alternative<Array>(data_); }
bool Json::is_object() const { return std::holds_alternative<Object>(data_); }

bool Json::as_bool(bool fallback) const {
    if (const auto* p = std::get_if<bool>(&data_)) {
        return *p;
    }
    return fallback;
}

double Json::as_number(double fallback) const {
    if (const auto* p = std::get_if<double>(&data_)) {
        return *p;
    }
    return fallback;
}

std::int64_t Json::as_int64(std::int64_t fallback) const {
    if (const auto* p = std::get_if<double>(&data_)) {
        return static_cast<std::int64_t>(*p);
    }
    return fallback;
}

const std::string& Json::as_string() const {
    if (const auto* p = std::get_if<std::string>(&data_)) {
        return *p;
    }
    throw JsonError("JSON value is not a string");
}

std::string Json::as_string(std::string_view fallback) const {
    if (const auto* p = std::get_if<std::string>(&data_)) {
        return *p;
    }
    return std::string(fallback);
}

const Json::Array& Json::as_array() const {
    if (const auto* p = std::get_if<Array>(&data_)) {
        return *p;
    }
    throw JsonError("JSON value is not an array");
}

Json::Array& Json::as_array() {
    if (auto* p = std::get_if<Array>(&data_)) {
        return *p;
    }
    throw JsonError("JSON value is not an array");
}

const Json::Object& Json::as_object() const {
    if (const auto* p = std::get_if<Object>(&data_)) {
        return *p;
    }
    throw JsonError("JSON value is not an object");
}

Json::Object& Json::as_object() {
    if (auto* p = std::get_if<Object>(&data_)) {
        return *p;
    }
    data_ = Object{};
    return std::get<Object>(data_);
}

bool Json::contains(std::string_view key) const {
    if (!is_object()) {
        return false;
    }
    return as_object().find(std::string(key)) != as_object().end();
}

const Json& Json::null_singleton() {
    static const Json n;
    return n;
}

const Json& Json::at(std::string_view key) const {
    if (!is_object()) {
        return null_singleton();
    }
    const auto& obj = as_object();
    const auto it = obj.find(std::string(key));
    if (it == obj.end()) {
        return null_singleton();
    }
    return it->second;
}

Json& Json::operator[](const std::string& key) { return as_object()[key]; }

const Json& Json::operator[](const std::string& key) const { return at(key); }

} // namespace xscope::utils
