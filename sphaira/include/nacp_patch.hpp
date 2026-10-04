#pragma once

// Game restrictions in control.nacp (linked account, screenshots, video) and the pieces needed to
// rewrite them inside a Control NCA: find a file in a RomFS image and rehash the IVFC tree.
// libnx-free so a host test can run it; the hash function is passed in.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sphaira::nacp_patch {

// NacpStruct offsets (libnx nacp.h, sizeof 0x4000).
inline constexpr std::size_t NACP_SIZE = 0x4000;
inline constexpr std::size_t OFF_STARTUP_USER_ACCOUNT = 0x3025;        // 1 = Required, 2 = with network service account
inline constexpr std::size_t OFF_SCREENSHOT = 0x3034;                  // 0 = Allow, 1 = Deny
inline constexpr std::size_t OFF_VIDEO_CAPTURE = 0x3035;               // 0 = Disable, 1 = Manual, 2 = Enable
inline constexpr std::size_t OFF_NETWORK_LICENSE_ON_LAUNCH = 0x3213;   // bit 0 = license required

struct State {
    bool linked_account_required{};
    bool screenshots_allowed{};
    bool video_allowed{};
};

struct Patch {
    std::optional<bool> linked_account_required{};
    std::optional<bool> screenshots_allowed{};
    std::optional<bool> video_allowed{};
};

inline auto ReadState(std::span<const std::uint8_t> nacp) -> State {
    State s;
    s.linked_account_required = nacp[OFF_STARTUP_USER_ACCOUNT] == 2 || (nacp[OFF_NETWORK_LICENSE_ON_LAUNCH] & 1);
    s.screenshots_allowed = nacp[OFF_SCREENSHOT] == 0;
    s.video_allowed = nacp[OFF_VIDEO_CAPTURE] != 0;
    return s;
}

// returns true when a byte changed.
inline bool Apply(std::span<std::uint8_t> nacp, const Patch& p) {
    std::vector<std::uint8_t> before(nacp.begin(), nacp.end());
    if (p.linked_account_required) {
        // never 0: a game with user saves still needs a user picked at launch.
        if (*p.linked_account_required) {
            nacp[OFF_STARTUP_USER_ACCOUNT] = 2;
            nacp[OFF_NETWORK_LICENSE_ON_LAUNCH] |= 1;
        } else {
            if (nacp[OFF_STARTUP_USER_ACCOUNT] == 2) {
                nacp[OFF_STARTUP_USER_ACCOUNT] = 1;
            }
            nacp[OFF_NETWORK_LICENSE_ON_LAUNCH] &= ~1;
        }
    }
    if (p.screenshots_allowed) {
        nacp[OFF_SCREENSHOT] = *p.screenshots_allowed ? 0 : 1;
    }
    if (p.video_allowed) {
        nacp[OFF_VIDEO_CAPTURE] = *p.video_allowed ? 2 : 0;
        if (*p.video_allowed) {
            nacp[OFF_SCREENSHOT] = 0; // capture needs screenshots allowed.
        }
    }
    return !std::equal(before.begin(), before.end(), nacp.begin());
}

template <typename T>
inline auto ReadLe(std::span<const std::uint8_t> b, std::size_t off) -> T {
    T v{};
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

// offset and size of a root-level file in a RomFS image.
inline auto FindRomfsFile(std::span<const std::uint8_t> romfs, std::string_view name) -> std::optional<std::pair<std::uint64_t, std::uint64_t>> {
    if (romfs.size() < 0x50) {
        return std::nullopt;
    }
    const auto file_meta_off = ReadLe<std::uint64_t>(romfs, 0x38);
    const auto file_meta_size = ReadLe<std::uint64_t>(romfs, 0x40);
    const auto data_off = ReadLe<std::uint64_t>(romfs, 0x48);
    if (file_meta_off > romfs.size() || file_meta_size > romfs.size() - file_meta_off) {
        return std::nullopt;
    }
    for (std::uint64_t pos = 0; pos + 0x20 <= file_meta_size;) {
        const auto e = file_meta_off + pos;
        const auto parent = ReadLe<std::uint32_t>(romfs, e + 0x00);
        const auto offset = ReadLe<std::uint64_t>(romfs, e + 0x08);
        const auto size = ReadLe<std::uint64_t>(romfs, e + 0x10);
        const auto name_size = ReadLe<std::uint32_t>(romfs, e + 0x1C);
        if (0x20 + name_size > file_meta_size - pos) {
            break;
        }
        const std::string_view entry_name{reinterpret_cast<const char*>(romfs.data() + e + 0x20), name_size};
        if (parent == 0 && entry_name == name) {
            return std::make_pair(data_off + offset, size);
        }
        pos += (0x20 + name_size + 3) & ~std::uint64_t{3};
    }
    return std::nullopt;
}

struct IvfcLevel {
    std::uint64_t offset{};     // within the section
    std::uint64_t size{};
    std::uint32_t block_log2{};
};

using HashFn = std::function<void(const std::uint8_t* data, std::size_t size, std::uint8_t out[32])>;

// Rehash the blocks of the data level (levels.back()) that overlap [dirty_off, +dirty_len) and every
// hash above them, up to `master`. With `verify`, only compares: true when every stored hash matches
// (blocks are hashed zero-padded to the block size). Run it with verify first, so a layout this does
// not understand is caught before anything is written.
// `master_exact`: the master hash covers level 0 exactly (not padded); found by verify, used by write.
inline bool Rehash(std::span<std::uint8_t> section, std::span<const IvfcLevel> levels, std::uint8_t master[32],
                   std::uint64_t dirty_off, std::uint64_t dirty_len, const HashFn& hash, bool verify, bool& master_exact) {
    if (levels.size() < 2 || dirty_len == 0) {
        return false;
    }
    std::uint8_t digest[32];
    std::vector<std::uint8_t> block;
    std::uint64_t cur_off = dirty_off;
    std::uint64_t cur_len = dirty_len;

    const auto hash_block = [&](const IvfcLevel& lv, std::uint64_t b) -> bool {
        const std::uint64_t bs = std::uint64_t{1} << lv.block_log2;
        const auto start = b * bs;
        if (start >= lv.size || lv.offset + lv.size > section.size()) {
            return false;
        }
        block.assign(bs, 0);
        std::memcpy(block.data(), section.data() + lv.offset + start, std::min(bs, lv.size - start));
        hash(block.data(), block.size(), digest);
        return true;
    };

    for (std::size_t k = levels.size() - 1; k >= 1; k--) {
        const auto& child = levels[k];
        const auto& parent = levels[k - 1];
        const std::uint64_t bs = std::uint64_t{1} << child.block_log2;
        const auto first = cur_off / bs;
        const auto last = (cur_off + cur_len - 1) / bs;
        for (auto b = first; b <= last; b++) {
            if (!hash_block(child, b) || (b + 1) * 32 > parent.size) {
                return false;
            }
            auto* dst = section.data() + parent.offset + b * 32;
            if (verify) {
                if (std::memcmp(dst, digest, 32)) {
                    return false;
                }
            } else {
                std::memcpy(dst, digest, 32);
            }
        }
        cur_off = first * 32;
        cur_len = (last - first + 1) * 32;
    }

    if (!hash_block(levels[0], 0)) {
        return false;
    }
    std::uint8_t exact[32];
    hash(section.data() + levels[0].offset, levels[0].size, exact);
    if (verify) {
        master_exact = !std::memcmp(master, exact, 32);
        return master_exact || !std::memcmp(master, digest, 32);
    }
    std::memcpy(master, master_exact ? exact : digest, 32);
    return true;
}

} // namespace sphaira::nacp_patch
