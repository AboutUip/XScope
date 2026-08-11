#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xscope::utils {

/// RFC 4648 base64 decode. Ignores ASCII whitespace. Throws std::runtime_error on bad input.
std::vector<std::uint8_t> base64_decode(std::string_view input);

/// Decode to string (binary-safe).
std::string base64_decode_string(std::string_view input);

std::string base64_encode(std::span<const std::uint8_t> input);
std::string base64_encode(std::string_view input);

} // namespace xscope::utils
