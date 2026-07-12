#include "ui/menus/cheats/cheats_lookup.hpp"
#include "log.hpp"
#include <format>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace sphaira::ui::menu::hats::detail {

// Format title ID as 16-character hex string (lowercase for atmosphere paths)
auto FormatTitleId(u64 title_id) -> std::string {
    return std::format("{:016X}", title_id);
}

// Format title ID as lowercase for file paths
auto FormatTitleIdLower(u64 title_id) -> std::string {
    return std::format("{:016x}", title_id);
}

auto GetBaseApplicationTitleId(u64 title_id) -> u64 {
    constexpr u64 update_title_id_suffix = 0x800;
    constexpr u64 title_id_content_suffix_mask = 0xFFF;

    if ((title_id & title_id_content_suffix_mask) == update_title_id_suffix) {
        return title_id & ~title_id_content_suffix_mask;
    }

    return title_id;
}

// Convert bytes to hex string (uppercase)
auto BytesToHex(const u8* data, size_t len) -> std::string {
    std::string hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02X", data[i]);
        hex += buf;
    }
    return hex;
}

auto BytesToBuildId(const u8* data, size_t len) -> std::string {
    std::string hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        const auto value = data[len - 1 - i];
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02X", value);
        hex += buf;
    }
    return hex;
}

auto NormalizeBuildId(std::string build_id) -> std::string {
    std::transform(build_id.begin(), build_id.end(), build_id.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return build_id;
}

auto ReverseBuildIdBytes(std::string build_id) -> std::string {
    build_id = NormalizeBuildId(std::move(build_id));
    if ((build_id.size() % 2) != 0) {
        return build_id;
    }

    std::string reversed;
    reversed.reserve(build_id.size());
    for (size_t i = build_id.size(); i > 0; i -= 2) {
        reversed.push_back(build_id[i - 2]);
        reversed.push_back(build_id[i - 1]);
    }
    return reversed;
}

auto IsValidBuildId(const std::string& build_id) -> bool {
    if (build_id.size() != 16) {
        return false;
    }

    bool all_zero = true;
    for (const auto c : build_id) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        if (c != '0') {
            all_zero = false;
        }
    }

    return !all_zero;
}

// Case-insensitive string comparison
auto StringsEqualIgnoreCase(const std::string& a, const std::string& b) -> bool {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char c1, char c2) {
        return std::tolower(c1) == std::tolower(c2);
    });
}

} // namespace sphaira::ui::menu::hats::detail
