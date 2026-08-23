#pragma once

namespace sphaira::forwarder_auto {

enum class LaunchSource {
    NewForwarder,
    OldForwarder,
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
