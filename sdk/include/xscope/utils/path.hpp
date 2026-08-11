#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace xscope::utils {

/// UTF-8 path string (safe across MSVC `std::u8string`).
std::string path_to_utf8(const std::filesystem::path& path);

/// Build a native path from a UTF-8 string (required on Windows; `path(string)` is NOT UTF-8).
std::filesystem::path path_from_utf8(std::string_view utf8);

void ensure_directory(const std::filesystem::path& dir);

std::string read_file_utf8(const std::filesystem::path& path);
void write_file_utf8(const std::filesystem::path& path, std::string_view data);

} // namespace xscope::utils
