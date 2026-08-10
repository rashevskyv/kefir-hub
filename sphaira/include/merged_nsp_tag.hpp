#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace sphaira::title {

inline std::string FormatMergedNspTag(bool has_base, bool has_patch, std::uint32_t patch_version, std::uint32_t dlc_count) {
    std::string tag;
    if (has_base) {
        tag += "B";
    }
    if (has_patch) {
        if (!tag.empty()) {
            tag += "+";
        }
        tag += "U" + std::to_string(patch_version);
    }
    if (dlc_count > 0) {
        if (!tag.empty()) {
            tag += "+";
        }
        tag += std::to_string(dlc_count) + "DLC";
    }
    return tag.empty() ? "" : "[" + tag + "]";
}

inline std::string FormatMergedNspFilename(const std::string& name, std::uint64_t app_id, bool has_base, bool has_patch, std::uint32_t patch_version, std::uint32_t dlc_count) {
    char id_str[17];
    std::snprintf(id_str, sizeof(id_str), "%016llX", static_cast<unsigned long long>(app_id));
    std::string tag = FormatMergedNspTag(has_base, has_patch, patch_version, dlc_count);
    return name + " [" + id_str + "]" + tag + ".nsp";
}

} // namespace sphaira::title
