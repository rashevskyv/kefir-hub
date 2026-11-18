#pragma once

// What the main screen's L button can open or start. Pure data, no libnx: tests/test_l_button_targets.cpp.
// A target is saved as "<group>/<key>"; the key is the en.json key of the row's label in that group's live list
// (Tools tiles, Game Tools, Software, the Tools page, Console Transfer, Install & Share), so the saved choice
// survives a language change and a reorder. A row that is gone (Kefir Settings off Kefir) resolves to nothing.

#include <string>
#include <string_view>

namespace sphaira::l_button {

struct Target {
    std::string_view group;
    std::string_view key;
    std::string_view sub{}; // a Tools tile that is a menu of its own: the group of its rows
};

inline constexpr Target TARGETS[] = {
    {"tile", "File Browser"},
    {"tile", "Game Tools", "game"},
    {"tile", "Themes"},
    {"tile", "Updater"},
    {"tile", "Software", "software"},
    {"tile", "Tools", "system"},
    {"tile", "Kefir Settings"},
    {"tile", "Settings"},
    {"tile", "Console Transfer", "transfer"},

    {"game", "Games"},
    {"game", "Saves"},
    {"game", "Cheats"},
    {"game", "Emulators"},
    {"game", "Shops"},

    {"software", "Homebrew App Store"},
    {"software", "DBI"},
    {"software", "Custom Link"},

    // "Remove parental controls" is left out: it acts at once, with no question first
    {"system", "System information"},
    {"system", "Check game updates"},
    {"system", "Module Manager"},
    {"system", "Fan curve"},
    {"system", "Wi-Fi"},
    {"system", "Web browser"},
    {"system", "Chromium (experimental)"},
    {"system", "Users"},
    {"system", "Battery calibration"},
    {"system", "Battery"},
    {"system", "Storage manager"},
    {"system", "Trash"},
    {"system", "Clean system junk"},
    {"system", "Fill free SD space with zeros"},
    {"system", "Manage parental controls"},

    {"transfer", "Send installed games"},
    {"transfer", "Receive games"},
    {"transfer", "Receive files"},
    {"transfer", "Share Entire microSD"},
    {"transfer", "Share Save Backups"},
    {"transfer", "Share User Backups"},
    {"transfer", "Share Profiles & Play Hours"},
    {"transfer", "Share Screenshots & Videos"},
    {"transfer", "Share switch Folder"},
    {"transfer", "Send a folder"},

    {"share", "Install & Share"},
    {"share", "Web Server"},
    {"share", "MTP"},
    {"share", "FTP"},
    {"share", "PC Install (USB)"},
    {"share", "Ownfoil"},
};

inline auto MakeId(const Target& t) -> std::string {
    return std::string{t.group} + "/" + std::string{t.key};
}

// nullptr: empty (L does nothing) or not a known target
inline auto Find(std::string_view id) -> const Target* {
    for (const auto& t : TARGETS) {
        if (id.size() == t.group.size() + 1 + t.key.size() && id.starts_with(t.group) &&
            id[t.group.size()] == '/' && id.ends_with(t.key)) {
            return &t;
        }
    }
    return nullptr;
}

} // namespace sphaira::l_button
