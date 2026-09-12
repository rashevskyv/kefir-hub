#pragma once

#include "version_compare.hpp"
#include <string>
#include <tuple>
#include <vector>

namespace sphaira::ui::menu::settings {

struct FirmwareCompatibility {
    bool available{false};
    std::string target_tag;
    std::string metadata_tag;
    bool warning_required{false};
};

struct TranslationRelease {
    std::tuple<int, int, int> version;
    const char* target_tag;
    const char* metadata_tag;
};

inline constexpr TranslationRelease KNOWN_TRANSLATIONS[] = {
    {{16, 1, 0}, "FW16.1.0-TR1.09", "FW17.0.1-TR1.18"},
    {{17, 0, 0}, "FW17.0.0-TR1.11", "FW17.0.1-TR1.18"},
    {{17, 0, 1}, "FW17.0.1-TR1.18", "FW17.0.1-TR1.18"},
    {{18, 1, 0}, "FW18.1.0-TR1.20", "FW18.1.0-TR1.20"},
    {{19, 0, 0}, "FW19.0.0-TR1.21", "FW19.0.0-TR1.21"},
    {{20, 4, 0}, "FW20.4.0-TR2.00", "FW20.4.0-TR2.00"},
    {{22, 5, 0}, "FW22.5.0-TR2.01", "FW22.5.0-TR2.01"},
};

inline auto ExtractFirmwareFromTag(const std::string& tag) -> std::string {
    if (tag.rfind("FW", 0) != 0) {
        return {};
    }
    const auto dash = tag.find('-');
    if (dash == std::string::npos || dash <= 2) {
        return tag.substr(2);
    }
    return tag.substr(2, dash - 2);
}

inline auto ResolveFirmwareCompatibility(const std::string& fw_version, bool is_china_region = false) -> FirmwareCompatibility {
    auto parts = version::Parse(fw_version);
    if (parts.empty()) {
        return {};
    }
    while (parts.size() < 3) {
        parts.push_back(0);
    }
    const int major = parts[0];
    const int minor = parts[1];
    const int patch = parts[2];
    const auto ver = std::tuple{major, minor, patch};

    // Firmware below 16.0.0 is unsupported
    if (ver < std::tuple{16, 0, 0}) {
        return {.available = false, .target_tag = "", .metadata_tag = "", .warning_required = false};
    }

    // Specific China region exception
    if (ver == std::tuple{19, 0, 2} && is_china_region) {
        return {.available = false, .target_tag = "", .metadata_tag = "", .warning_required = false};
    }

    // 1. Exact match: if current firmware matches a translation release version exactly, use it
    for (const auto& rel : KNOWN_TRANSLATIONS) {
        if (ver == rel.version) {
            return {.available = true, .target_tag = rel.target_tag, .metadata_tag = rel.metadata_tag, .warning_required = false};
        }
    }

    // 2. Higher than latest: if current firmware is higher than the latest translation release version,
    // automatically use the latest release with a compatibility warning
    constexpr size_t num_releases = sizeof(KNOWN_TRANSLATIONS) / sizeof(KNOWN_TRANSLATIONS[0]);
    static_assert(num_releases > 0, "KNOWN_TRANSLATIONS must not be empty");
    const auto& latest = KNOWN_TRANSLATIONS[num_releases - 1];
    if (ver > latest.version) {
        return {.available = true, .target_tag = latest.target_tag, .metadata_tag = latest.metadata_tag, .warning_required = true};
    }

    // 3. Concrete mappings for intermediate firmware versions
    if (ver >= std::tuple{16, 0, 0} && ver <= std::tuple{16, 1, 0}) {
        return {.available = true, .target_tag = "FW16.1.0-TR1.09", .metadata_tag = "FW17.0.1-TR1.18", .warning_required = false};
    }
    if (ver >= std::tuple{18, 0, 0} && ver <= std::tuple{18, 1, 0}) {
        return {.available = true, .target_tag = "FW18.1.0-TR1.20", .metadata_tag = "FW18.1.0-TR1.20", .warning_required = false};
    }
    if (ver >= std::tuple{19, 0, 0} && ver <= std::tuple{19, 0, 2}) {
        return {.available = true, .target_tag = "FW19.0.0-TR1.21", .metadata_tag = "FW19.0.0-TR1.21", .warning_required = false};
    }
    if (ver >= std::tuple{20, 0, 0} && ver <= std::tuple{20, 5, 0}) {
        return {.available = true, .target_tag = "FW20.4.0-TR2.00", .metadata_tag = "FW20.4.0-TR2.00", .warning_required = false};
    }
    if (ver >= std::tuple{21, 0, 0} && ver < std::tuple{22, 5, 0}) {
        return {.available = true, .target_tag = "FW20.4.0-TR2.00", .metadata_tag = "FW20.4.0-TR2.00", .warning_required = true};
    }

    return {.available = false, .target_tag = "", .metadata_tag = "", .warning_required = false};
}

inline auto TranslationExtractFolder(const std::string& zip_name) -> std::string {
    auto folder = zip_name;
    if (const auto dot = folder.find_last_of('.'); dot != std::string::npos) {
        folder = folder.substr(0, dot);
    }
    if (folder.rfind("TR", 0) == 0 || folder.rfind("tr", 0) == 0) {
        const auto underscore = folder.find('_');
        if (underscore != std::string::npos) {
            folder = folder.substr(underscore + 1);
        }
    } else if (folder.rfind("NX-", 0) == 0) {
        folder = "Nx-" + folder.substr(3);
    }
    return folder;
}

inline auto GetReleaseUrl(const std::string& target_tag) -> std::string {
    if (target_tag.empty()) {
        return {};
    }
    return "https://github.com/NX-Family/NX-Translation/releases/tag/" + target_tag;
}

inline auto GetMetadataUrl(const std::string& metadata_tag) -> std::string {
    if (metadata_tag.empty()) {
        return {};
    }
    return "https://raw.githubusercontent.com/NX-Family/NX-Translation/" + metadata_tag + "/api.json";
}

} // namespace sphaira::ui::menu::settings
