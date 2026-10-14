#pragma once

// Which installed games have a newer update than the one on the console. Pure logic, no libnx,
// so it has a host test; the download and the page are in game_updates.cpp.

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sphaira::game_updates {

struct Installed {
    std::uint64_t id{};
    std::string name{};
    std::uint32_t version{};
};

struct Outdated {
    std::uint64_t id{};
    std::string name{};
    std::uint32_t installed{};
    std::uint32_t latest{};
};

struct Report {
    std::vector<Outdated> outdated{};
    // installed games the list does not know: the check says nothing about them.
    std::size_t unknown{};
};

// `latest` maps a title id to the newest version the list knows. A console ahead of the list
// (an update newer than the list) is not outdated. Sorted by name.
inline auto FindOutdated(const std::vector<Installed>& installed, const std::unordered_map<std::uint64_t, std::uint32_t>& latest) -> Report {
    Report report;
    for (const auto& game : installed) {
        const auto it = latest.find(game.id);
        if (it == latest.end()) {
            report.unknown++;
        } else if (game.version < it->second) {
            report.outdated.push_back({game.id, game.name, game.version, it->second});
        }
    }
    std::sort(report.outdated.begin(), report.outdated.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return report;
}

// the update number as the rest of the Hub writes it: the version over 65536 ("v6").
inline auto UpdateLabel(std::uint32_t version) -> std::string {
    return "v" + std::to_string(version >> 16);
}

// Opens the check: downloads the list, compares, shows the outdated games.
void Open();

} // namespace sphaira::game_updates
