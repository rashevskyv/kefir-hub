#pragma once

// Which Build ID's cheats to take when the game's own Build ID has none in the
// database. Pure logic, no libnx, so it has a host test.

#include <string>
#include <vector>

namespace sphaira::ui::menu::hats::detail {

struct BuildIdCheats {
    std::string build_id;
    size_t cheat_count{};
};

// `sets` are in the order of the database file. The database carries no game
// version, so "closest" is the most complete set; among equals the one listed
// last wins (new versions are appended). Returns "" when there is nothing.
inline auto PickClosestBuildId(const std::vector<BuildIdCheats>& sets) -> std::string {
    const BuildIdCheats* best = nullptr;
    for (const auto& s : sets) {
        if (s.cheat_count == 0) {
            continue;
        }
        if (!best || s.cheat_count >= best->cheat_count) {
            best = &s;
        }
    }
    return best ? best->build_id : std::string{};
}

} // namespace sphaira::ui::menu::hats::detail
