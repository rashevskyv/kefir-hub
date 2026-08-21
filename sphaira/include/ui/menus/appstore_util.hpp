#pragma once

#include <string>
#include <string_view>

namespace sphaira::ui::menu::appstore {

inline auto IsRetroArchPackageName(std::string_view name, std::string_view title) -> bool {
    if (name == "RetroNX" || name == "retroarch" || name == "RetroArch") {
        return true;
    }
    if (title.find("Retroarch") != std::string_view::npos ||
        title.find("RetroArch") != std::string_view::npos) {
        return true;
    }
    return false;
}

constexpr const char* RETROARCH_NIGHTLY_URL = "https://buildbot.libretro.com/nightly/nintendo/switch/libnx/RetroArch.7z";

inline auto ResolveAppstoreZipUrl(std::string_view name, std::string_view title, std::string_view base_url) -> std::string {
    if (IsRetroArchPackageName(name, title)) {
        return RETROARCH_NIGHTLY_URL;
    }
    return std::string(base_url) + "/zips/" + std::string(name) + ".zip";
}

} // namespace sphaira::ui::menu::appstore
