#pragma once

#include <string>

namespace sphaira::ui::menu::game {

// Right-hand column of a Games list row. Cart titles use [GC] instead of [S|N|b].
inline auto FormatGameListInfo(bool on_sd, bool on_nand, bool on_gamecard,
    bool has_base, bool has_update, bool has_dlc, bool layeredfs,
    const std::string& size) -> std::string
{
    if (on_gamecard) {
        return size.empty() ? std::string{"[GC]"} : ("[GC]  " + size);
    }

    std::string flags;
    if (on_sd) {
        flags += 'S';
    }
    if (on_nand) {
        flags += 'N';
    }
    flags += '|';
    if (has_base) {
        flags += 'b';
    }
    if (has_update) {
        flags += 'u';
    }
    if (has_dlc) {
        flags += 'd';
    }
    if (layeredfs) {
        flags += 'L';
    }

    return size.empty() ? ("[" + flags + "]  ") : ("[" + flags + "]  " + size);
}

} // namespace sphaira::ui::menu::game
