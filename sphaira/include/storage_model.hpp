#pragma once

// The pie of the storage manager: what fills a card, as slices that add up to its size. No libnx: host-tested.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace sphaira::storage {

enum class Id {
    Games,        // games installed in the system (sysMMC): Nintendo/Contents
    EmuGames,     // games installed in the emuMMC: its own Nintendo folder
    EmuData,      // the emuMMC when it is files on the card (a copy of the system, its saves, its records)
    EmuPartition, // the emuMMC when it is a partition: outside the file system, so no folder walk can see it
    Saves,        // saves and the album (Nintendo/save, Nintendo/Album)
    System,    // Atmosphère, hekate, Kefir and the Hub's own files
    Homebrew,  // /switch
    Backups,   // dumps and backups
    Files,     // game files (NSP, XCI) kept on the card
    Emulators, // emulators (RetroArch, Tico...) with their cores, saves and the ROM folders
    Other,     // the rest of the used space
    Free,
};

inline auto BelongsToEmummc(Id id) -> bool {
    return id == Id::EmuGames || id == Id::EmuData || id == Id::EmuPartition;
}

// where a slice goes in the ring: the system's games, then the emuMMC with what is inside it (the emuMMC itself, then its
// games), then the rest: the family sits together so it reads as one container
inline auto Rank(Id id) -> int {
    switch (id) {
        case Id::Games: return 0;
        case Id::EmuPartition:
        case Id::EmuData: return 1;
        case Id::EmuGames: return 2;
        default: return 3;
    }
}

struct Slice {
    Id id{};
    std::int64_t bytes{};
};

// The known parts (a part of 0 bytes is dropped), then "Other" (what is used and is none of them) and "Free". `total` and `free`
// are the card's own numbers; when the parts add up to more than the used space (hard links, rounding) Other is 0 and the
// parts keep their sizes, so the ring then adds up to more than the card: Total() says by how much.
inline auto Build(std::int64_t total, std::int64_t free_bytes, const std::vector<Slice>& parts) -> std::vector<Slice> {
    std::vector<Slice> out;
    std::int64_t known = 0;
    for (const auto& p : parts) {
        if (p.bytes > 0) {
            out.push_back(p);
            known += p.bytes;
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Slice& a, const Slice& b) { return Rank(a.id) < Rank(b.id); });
    const auto used = std::max<std::int64_t>(0, total - free_bytes);
    out.push_back({Id::Other, std::max<std::int64_t>(0, used - known)});
    out.push_back({Id::Free, std::max<std::int64_t>(0, free_bytes)});
    return out;
}

inline auto Total(const std::vector<Slice>& slices) -> std::int64_t {
    std::int64_t sum = 0;
    for (const auto& s : slices) {
        sum += s.bytes;
    }
    return sum;
}

// everything the emuMMC takes, the games installed in it included
inline auto EmummcTotal(const std::vector<Slice>& slices) -> std::int64_t {
    std::int64_t sum = 0;
    for (const auto& s : slices) {
        if (BelongsToEmummc(s.id)) {
            sum += s.bytes;
        }
    }
    return sum;
}

// the part of the ring a slice takes, 0..1000 (per mille, no floats)
inline auto PerMille(const Slice& s, std::int64_t total) -> int {
    return total > 0 ? static_cast<int>(std::clamp<std::int64_t>(s.bytes * 1000 / total, 0, 1000)) : 0;
}

} // namespace sphaira::storage
