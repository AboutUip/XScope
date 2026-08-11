#include "xscope/utils/base64.hpp"

#include <cctype>
#include <stdexcept>

namespace xscope::utils {
namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decode_char(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

} // namespace

std::vector<std::uint8_t> base64_decode(std::string_view input) {
    std::vector<int> vals;
    vals.reserve(input.size());
    for (unsigned char c : input) {
        if (std::isspace(c)) {
            continue;
        }
        if (c == '=') {
            break;
        }
        const int v = decode_char(c);
        if (v < 0) {
            throw std::runtime_error("invalid base64 character");
        }
        vals.push_back(v);
    }
    std::vector<std::uint8_t> out;
    out.reserve(vals.size() * 3 / 4);
    for (size_t i = 0; i + 1 < vals.size();) {
        const int a = vals[i++];
        const int b = vals[i++];
        out.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
        if (i >= vals.size()) {
            break;
        }
        const int c = vals[i++];
        out.push_back(static_cast<std::uint8_t>(((b & 0xF) << 4) | (c >> 2)));
        if (i >= vals.size()) {
            break;
        }
        const int d = vals[i++];
        out.push_back(static_cast<std::uint8_t>(((c & 0x3) << 6) | d));
    }
    return out;
}

std::string base64_decode_string(std::string_view input) {
    const auto bytes = base64_decode(input);
    return std::string(bytes.begin(), bytes.end());
}

std::string base64_encode(std::span<const std::uint8_t> input) {
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < input.size()) {
        const std::uint32_t n =
            (std::uint32_t(input[i]) << 16) | (std::uint32_t(input[i + 1]) << 8) | input[i + 2];
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(kAlphabet[(n >> 6) & 63]);
        out.push_back(kAlphabet[n & 63]);
        i += 3;
    }
    if (i < input.size()) {
        std::uint32_t n = std::uint32_t(input[i]) << 16;
        out.push_back(kAlphabet[(n >> 18) & 63]);
        if (i + 1 < input.size()) {
            n |= std::uint32_t(input[i + 1]) << 8;
            out.push_back(kAlphabet[(n >> 12) & 63]);
            out.push_back(kAlphabet[(n >> 6) & 63]);
            out.push_back('=');
        } else {
            out.push_back(kAlphabet[(n >> 12) & 63]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

std::string base64_encode(std::string_view input) {
    return base64_encode(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size()));
}

} // namespace xscope::utils
