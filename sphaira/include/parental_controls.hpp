#pragma once

// Tools -> Maintenance: the console's own parental controls and NS Parental Control
// (github.com/TristanIsrael/NSParentalControl, GPL-3.0): a sysmodule and an Ultrahand overlay
// with play-time limits, bed time and blocked games. It has its own administrator PIN.

#include <optional>
#include <string>
#include <string_view>

namespace sphaira::parental {

// clears the console's parental controls (PIN, restrictions, the link to the phone app) and stops
// NS Parental Control (now and at boot; its files stay). No question.
void Remove();

// explains NS Parental Control; downloads and installs it when it is missing, turns it on when it is stopped.
void OpenManager();

// release zip entry -> SD path; nullopt = skip it. The release zip has a version folder on top
// ("2.4.0/atmosphere/...") and macOS junk; only the sysmodule folder and the overlay are kept.
inline auto MapZipEntry(std::string_view name) -> std::optional<std::string> {
    if (name.find("__MACOSX") != std::string_view::npos || name.ends_with(".DS_Store")) {
        return std::nullopt;
    }
    if (!name.starts_with("atmosphere/") && !name.starts_with("switch/")) {
        const auto slash = name.find('/');
        if (slash == std::string_view::npos) {
            return std::nullopt;
        }
        name.remove_prefix(slash + 1);
    }
    if (!name.starts_with("atmosphere/contents/4200000000003103/") && !name.starts_with("switch/.overlays/")) {
        return std::nullopt;
    }
    if (name.find("..") != std::string_view::npos) {
        return std::nullopt;
    }
    return "/" + std::string{name};
}

} // namespace sphaira::parental
