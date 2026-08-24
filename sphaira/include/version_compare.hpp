#pragma once

// Dotted version-string handling, split out of the firmware menus so both of
// them share one implementation and so it can be tested on the host: nothing
// here touches switch.h.
//
// NOTE: App::GetVersionFromString / App::IsVersionNewer look like duplicates of
// this but are NOT interchangeable. They go through sscanf("%u.%u.%u") and
// MAKEHOSVERSION, so they read exactly three components and clamp each to the
// bit width of the packed field. These read any number of components and
// compare them as ints. Firmware versions ("20.1.5") behave the same under
// both; anything with a fourth component or a component over 255 does not.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::version {

// "20.1.5" -> {20, 1, 5}. Empty segments are skipped; parsing stops at the
// first segment that does not start with a number, so "1.2.beta" -> {1, 2}.
inline auto Parse(const std::string& version) -> std::vector<int> {
    std::string_view in = version;
    while (!in.empty() && (in.front() == 'v' || in.front() == 'V' || in.front() == ' ')) {
        in.remove_prefix(1);
    }

    std::vector<int> parts;
    std::stringstream ss{std::string{in}};
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (segment.empty()) {
            continue;
        }

        char* end = nullptr;
        const auto part = std::strtol(segment.c_str(), &end, 10);
        if (end == segment.c_str()) {
            break;
        }
        parts.push_back(static_cast<int>(part));
    }

    return parts;
}

// true when target sorts before current. A missing component counts as 0, so
// "20" and "20.0.0" compare equal.
inline auto IsLower(const std::string& target, const std::string& current) -> bool {
    const auto target_parts = Parse(target);
    const auto current_parts = Parse(current);
    const auto max_len = std::max(target_parts.size(), current_parts.size());

    for (size_t i = 0; i < max_len; i++) {
        const auto t = i < target_parts.size() ? target_parts[i] : 0;
        const auto c = i < current_parts.size() ? current_parts[i] : 0;
        if (t < c) {
            return true;
        }
        if (t > c) {
            return false;
        }
    }

    return false;
}

inline auto IsEqual(const std::string& a, const std::string& b) -> bool {
    return !IsLower(a, b) && !IsLower(b, a);
}

// Unpacks the packed version word amssuGetUpdateInformation reports.
inline auto FormatPacked(std::uint32_t version) -> std::string {
    return std::to_string((version >> 26) & 0x1f) + "." +
           std::to_string((version >> 20) & 0x1f) + "." +
           std::to_string((version >> 16) & 0xf);
}

// Formats the 32-bit SDK version word (major.minor.micro.revision) from NCA headers.
// Returns an empty string if sdk_version is 0.
inline auto FormatSdkVersion(std::uint32_t sdk_version) -> std::string {
    if (!sdk_version) {
        return "";
    }
    const auto major = (sdk_version >> 24) & 0xFF;
    const auto minor = (sdk_version >> 16) & 0xFF;
    const auto micro = (sdk_version >> 8) & 0xFF;
    const auto revision = sdk_version & 0xFF;
    return std::to_string(major) + "." +
           std::to_string(minor) + "." +
           std::to_string(micro) + "." +
           std::to_string(revision);
}

// Returns true if the installed firmware string is lower than the packed requirement.
// Returns false if required_packed is 0, or if installed_version contains no valid numeric version.
inline auto IsFirmwareLower(const std::string& installed_version, std::uint32_t required_packed) -> bool {
    if (required_packed == 0) {
        return false;
    }
    if (Parse(installed_version).empty()) {
        return false;
    }
    const auto required_version = FormatPacked(required_packed);
    return IsLower(installed_version, required_version);
}

} // namespace sphaira::version
