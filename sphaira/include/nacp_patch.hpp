#pragma once

// Game restrictions, name, publisher and version in control.nacp, and the pieces needed to
// rewrite them (and the icons) inside a Control NCA: list and replace files in a RomFS image,
// rehash the IVFC tree. libnx-free so a host test can run it; the hash function is passed in.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::nacp_patch {

// NacpStruct offsets (libnx nacp.h, sizeof 0x4000).
inline constexpr std::size_t NACP_SIZE = 0x4000;
inline constexpr std::size_t OFF_STARTUP_USER_ACCOUNT = 0x3025;        // 1 = Required, 2 = with network service account
inline constexpr std::size_t OFF_SCREENSHOT = 0x3034;                  // 0 = Allow, 1 = Deny
inline constexpr std::size_t OFF_VIDEO_CAPTURE = 0x3035;               // 0 = Disable, 1 = Manual, 2 = Enable
inline constexpr std::size_t OFF_NETWORK_LICENSE_ON_LAUNCH = 0x3213;   // bit 0 = license required
inline constexpr std::size_t OFF_DISPLAY_VERSION = 0x3060;
inline constexpr std::size_t DISPLAY_VERSION_SIZE = 0x10;
inline constexpr std::size_t OFF_TITLES_DATA_FORMAT = 0x3215;          // 0 = 16 plain entries, 1 = deflated (21.0.0+)
inline constexpr std::size_t LANG_COUNT = 16;
inline constexpr std::size_t LANG_ENTRY_SIZE = 0x300;                  // name[0x200] + author[0x100]
inline constexpr std::size_t NAME_SIZE = 0x200;
inline constexpr std::size_t AUTHOR_SIZE = 0x100;

struct State {
    bool linked_account_required{};
    bool screenshots_allowed{};
    bool video_allowed{};
    bool titles_editable{}; // plain title entries; a deflated table is not rewritten
    std::string version{};
};

// the icon producer gets the smallest icon slot of the NCA and returns a JPEG that fits it
// (empty = it cannot); called only when the patch replaces the icon.
using IconFn = std::function<std::vector<std::uint8_t>(std::size_t max_size)>;

struct Patch {
    std::optional<bool> linked_account_required{};
    std::optional<bool> screenshots_allowed{};
    std::optional<bool> video_allowed{};
    std::optional<std::string> name{};
    std::optional<std::string> author{};
    std::optional<std::string> version{};
    IconFn icon{};

    bool EditsTitle() const { return name || author || version || icon; }
};

inline auto ReadText(const std::uint8_t* src, std::size_t size) -> std::string {
    return {reinterpret_cast<const char*>(src), strnlen(reinterpret_cast<const char*>(src), size)};
}

inline auto ReadState(std::span<const std::uint8_t> nacp) -> State {
    State s;
    s.linked_account_required = nacp[OFF_STARTUP_USER_ACCOUNT] == 2 || (nacp[OFF_NETWORK_LICENSE_ON_LAUNCH] & 1);
    s.screenshots_allowed = nacp[OFF_SCREENSHOT] == 0;
    s.video_allowed = nacp[OFF_VIDEO_CAPTURE] != 0;
    s.titles_editable = nacp[OFF_TITLES_DATA_FORMAT] == 0;
    s.version = ReadText(nacp.data() + OFF_DISPLAY_VERSION, DISPLAY_VERSION_SIZE);
    return s;
}

// zero-filled field, text cut to fit with a NUL, never in the middle of a UTF-8 character.
inline void PutText(std::uint8_t* dst, std::size_t size, std::string_view s) {
    auto n = std::min(s.size(), size - 1);
    while (n && n < s.size() && (static_cast<std::uint8_t>(s[n]) & 0xC0) == 0x80) {
        n--;
    }
    std::memset(dst, 0, size);
    std::memcpy(dst, s.data(), n);
}

// returns true when a byte changed.
inline bool Apply(std::span<std::uint8_t> nacp, const Patch& p) {
    std::vector<std::uint8_t> before(nacp.begin(), nacp.end());
    // only the languages the game has a name in: the console falls back to those anyway.
    if ((p.name || p.author) && nacp[OFF_TITLES_DATA_FORMAT] == 0) {
        bool any = false;
        for (std::size_t i = 0; i < LANG_COUNT; i++) {
            any = any || nacp[i * LANG_ENTRY_SIZE];
        }
        for (std::size_t i = 0; i < LANG_COUNT; i++) {
            auto* e = nacp.data() + i * LANG_ENTRY_SIZE;
            if (any ? !e[0] : i != 0) {
                continue;
            }
            if (p.name) PutText(e, NAME_SIZE, *p.name);
            if (p.author) PutText(e + NAME_SIZE, AUTHOR_SIZE, *p.author);
        }
    }
    if (p.version) {
        PutText(nacp.data() + OFF_DISPLAY_VERSION, DISPLAY_VERSION_SIZE, *p.version);
    }
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

struct RomfsFile {
    std::string_view name{};
    std::uint64_t entry_off{}; // the file entry in the meta table (size field at +0x10)
    std::uint64_t data_off{};
    std::uint64_t size{};
    std::uint64_t capacity{};  // room up to the next file or table: a new file may be this big
};

// root-level files of a RomFS image; offsets from the start of the image.
inline auto ListRomfsFiles(std::span<const std::uint8_t> romfs) -> std::vector<RomfsFile> {
    std::vector<RomfsFile> out;
    if (romfs.size() < 0x50) {
        return out;
    }
    const auto file_meta_off = ReadLe<std::uint64_t>(romfs, 0x38);
    const auto file_meta_size = ReadLe<std::uint64_t>(romfs, 0x40);
    const auto data_off = ReadLe<std::uint64_t>(romfs, 0x48);
    if (file_meta_off > romfs.size() || file_meta_size > romfs.size() - file_meta_off) {
        return out;
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
        if (parent == 0) {
            out.push_back({{reinterpret_cast<const char*>(romfs.data() + e + 0x20), name_size}, e, data_off + offset, size});
        }
        pos += (0x20 + name_size + 3) & ~std::uint64_t{3};
    }
    // the slot ends where the next thing starts (a file, one of the four tables, the image end),
    // so a shorter file written before does not shrink it.
    std::vector<std::uint64_t> starts;
    for (const std::size_t t : {0x08, 0x18, 0x28, 0x38}) {
        starts.push_back(ReadLe<std::uint64_t>(romfs, t));
    }
    for (const auto& f : out) {
        starts.push_back(f.data_off);
    }
    for (auto& f : out) {
        std::uint64_t end = romfs.size();
        for (const auto s : starts) {
            if (s > f.data_off && s < end) {
                end = s;
            }
        }
        f.capacity = f.data_off < end ? std::max(f.size, end - f.data_off) : f.size;
    }
    return out;
}

// offset and size of a root-level file in a RomFS image.
inline auto FindRomfsFile(std::span<const std::uint8_t> romfs, std::string_view name) -> std::optional<std::pair<std::uint64_t, std::uint64_t>> {
    for (const auto& f : ListRomfsFiles(romfs)) {
        if (f.name == name) {
            return std::make_pair(f.data_off, f.size);
        }
    }
    return std::nullopt;
}

inline bool IsIconFile(std::string_view name) {
    return name.starts_with("icon_") && name.ends_with(".dat");
}

// writes `data` over a file in place, up to its capacity: the rest of the slot is zeroed and
// the entry gets the new size. False when it does not fit.
inline bool ReplaceRomfsFile(std::span<std::uint8_t> romfs, const RomfsFile& f, std::span<const std::uint8_t> data) {
    if (data.size() > f.capacity || f.data_off + f.capacity > romfs.size() || f.entry_off + 0x18 > romfs.size()) {
        return false;
    }
    std::memcpy(romfs.data() + f.data_off, data.data(), data.size());
    std::memset(romfs.data() + f.data_off + data.size(), 0, f.capacity - data.size());
    const std::uint64_t size = data.size();
    std::memcpy(romfs.data() + f.entry_off + 0x10, &size, sizeof(size));
    return true;
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
