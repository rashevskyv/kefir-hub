#pragma once

#include <string>
#include <string_view>
#include <vector>

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

// an entry may name its own zip (recompiles hosted on their own GitHub release).
inline auto ResolveAppstoreZipUrl(std::string_view name, std::string_view title, std::string_view base_url, std::string_view direct_url = {}) -> std::string {
    if (!direct_url.empty()) {
        return std::string(direct_url);
    }
    if (IsRetroArchPackageName(name, title)) {
        return RETROARCH_NIGHTLY_URL;
    }
    return std::string(base_url) + "/zips/" + std::string(name) + ".zip";
}

// hbstore-compatible sources. Every base serves /repo.json, /packages/<name>/icon.png and /zips/<name>.zip.
constexpr const char* SOURCE_FORTHEUSERS = "https://switch.cdn.fortheusers.org";
constexpr const char* SOURCE_KEFIR_RECOMPILES = "https://raw.githubusercontent.com/rashevskyv/kefir-store/main";

inline auto DefaultSources() -> std::vector<std::string> {
    return {SOURCE_FORTHEUSERS, SOURCE_KEFIR_RECOMPILES};
}

// user file: one base url per line, '#' starts a comment, a trailing '/' is dropped.
// lines that are not http(s) or that repeat an earlier source are ignored.
inline void AppendSourceList(std::string_view text, std::vector<std::string>& out) {
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);

        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r' || line.back() == '/')) {
            line.remove_suffix(1);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        if (!line.starts_with("http://") && !line.starts_with("https://")) {
            continue;
        }

        bool seen = false;
        for (const auto& s : out) {
            if (s == line) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            out.emplace_back(line);
        }
    }
}

} // namespace sphaira::ui::menu::appstore
