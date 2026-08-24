#pragma once

#include <array>
#include <cstddef>

namespace sphaira::ui::menu::game {

inline constexpr std::size_t kMaxGameBadges = 8;

// Same labels the cover pills use (GC / Base / DLC / Update / LayeredFS / "-").
// List rows also pass include_storage so SD and NAND show as pills, not [S|N].
inline auto CollectGameBadgeLabels(
    bool on_sd, bool on_nand, bool on_gamecard,
    bool has_base, bool has_update, bool has_dlc, bool layeredfs,
    bool include_storage,
    std::array<const char*, kMaxGameBadges>& out) -> std::size_t
{
    std::size_t n = 0;
    if (include_storage) {
        if (on_sd) {
            out[n++] = "SD";
        }
        if (on_nand) {
            out[n++] = "NAND";
        }
    }
    if (on_gamecard) {
        out[n++] = "GC";
    }
    if (has_base) {
        out[n++] = "Base";
    }
    if (has_dlc) {
        out[n++] = "DLC";
    }
    if (has_update) {
        out[n++] = "Update";
    }
    if (layeredfs) {
        out[n++] = "LayeredFS";
    }
    if (!has_base) {
        out[n++] = "-";
    }
    return n;
}

} // namespace sphaira::ui::menu::game
