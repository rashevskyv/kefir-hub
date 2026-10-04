#pragma once

#include "forced_language.hpp" // ParseTitleId

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace sphaira::cleanup {

// /atmosphere/contents/<name> that belongs to a game: an application id (ends in 000) above the
// system range. Sysmodules (0100000000000xxx, 4200..., 00FF...) never match.
inline auto IsGameContentsFolder(std::string_view name) -> bool {
    const auto tid = forced_language::ParseTitleId(name);
    return tid >= 0x0100000000010000ULL && (tid >> 56) == 0x01 && (tid & 0xFFF) == 0;
}

struct PatchRef {
    std::uint64_t app_id{};
    std::uint32_t version{};
    std::size_t index{}; // caller's handle
};

// every update older than the newest one of the same game.
inline auto OldUpdates(const std::vector<PatchRef>& patches) -> std::vector<std::size_t> {
    std::map<std::uint64_t, std::uint32_t> newest;
    for (const auto& p : patches) {
        auto& v = newest[p.app_id];
        v = p.version > v ? p.version : v;
    }
    std::vector<std::size_t> out;
    for (const auto& p : patches) {
        if (p.version < newest[p.app_id]) {
            out.push_back(p.index);
        }
    }
    return out;
}

} // namespace sphaira::cleanup
