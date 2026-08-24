#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace sphaira::forwarder_auto {

inline auto IsKefirHubName(std::string_view raw_name) -> bool {
    std::string name{raw_name};
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return name.find("kefir") != std::string::npos || name.find("sphaira") != std::string::npos;
}

// Homebrew Menu / HBL forwarders. Kefir Hub / Sphaira titles are not this —
// they are handled by IsStaleOwnForwarder.
inline auto IsOldHomebrewTitle(std::string_view raw_name, std::uint64_t tid, std::uint64_t kefirhub_tid) -> bool {
    if (tid == kefirhub_tid && kefirhub_tid != 0) {
        return false;
    }
    if (IsKefirHubName(raw_name)) {
        return false;
    }

    if (tid == 0x03DB1280BD84000ULL || tid == 0x03DB12780BD84000ULL ||
        tid == 0x050000000000100DULL) {
        return true;
    }

    std::string name{raw_name};
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (name == "hbm" || name == "hbl" || name == "hbmenu" || name == "hblauncher" || name == "nx-hbmenu") {
        return true;
    }

    if (name.find("homebrew menu") != std::string::npos ||
        name.find("homebrew launcher") != std::string::npos ||
        name.find("hblauncher") != std::string::npos ||
        name.find("hbmenu") != std::string::npos ||
        name.find("nx-hbmenu") != std::string::npos) {
        return true;
    }

    return false;
}

// A previous Kefir Hub / Sphaira HOME icon whose Title ID is not the current
// path-hash. 0x05… is the owo forwarder prefix — never a Nintendo game.
inline auto IsStaleOwnForwarder(std::string_view raw_name, std::uint64_t tid, std::uint64_t kefirhub_tid) -> bool {
    if (!tid || tid == kefirhub_tid) {
        return false;
    }
    if ((tid & 0xFF00000000000000ULL) != 0x0500000000000000ULL) {
        return false;
    }
    return IsKefirHubName(raw_name);
}

enum class LaunchSource {
    NewForwarder,
    OldForwarder,
    StaleOwn,
    Album,
};

enum class Notice {
    None,
    OldWillBeRemoved,
    UseNewNextTime,
    PreferHomeIcon,
};

struct Plan {
    bool install_new{};
    bool delete_old{};
    Notice notice{Notice::None};
};

inline auto ClassifyLaunch(bool is_application, std::uint64_t own_tid, std::uint64_t kefirhub_tid, std::string_view name) -> LaunchSource {
    if (!is_application) {
        return LaunchSource::Album;
    }
    if (own_tid && own_tid == kefirhub_tid) {
        return LaunchSource::NewForwarder;
    }
    if (own_tid && IsStaleOwnForwarder(name, own_tid, kefirhub_tid)) {
        return LaunchSource::StaleOwn;
    }
    if (own_tid && IsOldHomebrewTitle(name, own_tid, kefirhub_tid)) {
        return LaunchSource::OldForwarder;
    }
    return LaunchSource::Album;
}

// Old forwarder is never deleted while we launched from it.
inline auto Decide(LaunchSource src, bool new_installed, bool old_installed) -> Plan {
    Plan p{};
    switch (src) {
    case LaunchSource::NewForwarder:
        if (old_installed) {
            p.delete_old = true;
            p.notice = Notice::OldWillBeRemoved;
        }
        break;
    case LaunchSource::OldForwarder:
        if (!new_installed) {
            p.install_new = true;
        }
        p.notice = Notice::UseNewNextTime;
        break;
    case LaunchSource::StaleOwn:
        if (!new_installed) {
            p.install_new = true;
        }
        break;
    case LaunchSource::Album:
        if (!new_installed) {
            p.install_new = true;
        }
        if (old_installed) {
            p.delete_old = true;
        }
        if (!new_installed || old_installed) {
            p.notice = Notice::PreferHomeIcon;
        }
        break;
    }
    return p;
}

} // namespace sphaira::forwarder_auto
