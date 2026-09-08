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

    if (ver >= std::tuple{16, 0, 0} && ver <= std::tuple{16, 1, 0}) {
        return {.available = true, .target_tag = "FW16.1.0-TR1.09", .metadata_tag = "FW17.0.1-TR1.18", .warning_required = false};
    }
    if (ver == std::tuple{17, 0, 0}) {
        return {.available = true, .target_tag = "FW17.0.0-TR1.11", .metadata_tag = "FW17.0.1-TR1.18", .warning_required = false};
    }
    if (ver == std::tuple{17, 0, 1}) {
        return {.available = true, .target_tag = "FW17.0.1-TR1.18", .metadata_tag = "FW17.0.1-TR1.18", .warning_required = false};
    }
    if (ver >= std::tuple{18, 0, 0} && ver <= std::tuple{18, 1, 0}) {
        return {.available = true, .target_tag = "FW18.1.0-TR1.20", .metadata_tag = "FW18.1.0-TR1.20", .warning_required = false};
    }
    if (ver >= std::tuple{19, 0, 0} && ver <= std::tuple{19, 0, 2}) {
        if (ver == std::tuple{19, 0, 2} && is_china_region) {
            return {.available = false, .target_tag = "", .metadata_tag = "", .warning_required = false};
        }
        return {.available = true, .target_tag = "FW19.0.0-TR1.21", .metadata_tag = "FW19.0.0-TR1.21", .warning_required = false};
    }
    if (ver >= std::tuple{20, 0, 0} && ver <= std::tuple{20, 5, 0}) {
        return {.available = true, .target_tag = "FW20.4.0-TR2.00", .metadata_tag = "FW20.4.0-TR2.00", .warning_required = false};
    }
    if (ver >= std::tuple{21, 0, 0} && ver <= std::tuple{22, 5, 0}) {
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
