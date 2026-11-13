#pragma once

// Console saves from DBI Backend Qt: a queue entry "<game> - <option> <16 hex title id>.kefir-cache.tar"
// holds the game's cache storages, members "<index>/<path>" (the PC folder SD_Cache.0003 is cache
// storage 3); "... .kefir-save.tar" holds files for one user's save, members "<path>". The first
// member ".kefir/sizes" has "<index><TAB><bytes>" lines: what each cache storage must hold, every
// file rounded up to a 16 KiB block. The title id is not in [brackets]: the queue's "already
// installed" check reads those. Pure logic, host-tested in tests/test_save_bundle_rules.cpp.

#include "sd_bundle_rules.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace sphaira::save_bundle {

inline constexpr std::string_view CACHE_SUFFIX = ".kefir-cache.tar";
inline constexpr std::string_view SAVE_SUFFIX = ".kefir-save.tar";
inline constexpr std::string_view SIZES = ".kefir/sizes";
inline constexpr int64_t BLOCK = 0x4000;
// a cache storage this Hub creates gets at least this journal: it bounds the bytes per commit.
inline constexpr int64_t MIN_JOURNAL = 64 * 1024 * 1024;

enum class Kind { None, Cache, Account };

inline auto KindOf(std::string_view name) -> Kind {
    if (name.size() > CACHE_SUFFIX.size() && name.ends_with(CACHE_SUFFIX)) return Kind::Cache;
    if (name.size() > SAVE_SUFFIX.size() && name.ends_with(SAVE_SUFFIX)) return Kind::Account;
    return Kind::None;
}

// the title id just before the suffix, after a space; 0 when the name has none.
inline auto TitleOf(std::string_view name) -> uint64_t {
    const auto kind = KindOf(name);
    if (kind == Kind::None) return 0;
    name.remove_suffix(kind == Kind::Cache ? CACHE_SUFFIX.size() : SAVE_SUFFIX.size());
    if (name.size() < 17 || name[name.size() - 17] != ' ') return 0;
    uint64_t v = 0;
    for (const char c : name.substr(name.size() - 16)) {
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) return 0;
        v = (v << 4) | static_cast<uint64_t>(d);
    }
    return v;
}

inline auto ParseIndex(std::string_view s) -> std::optional<uint16_t> {
    if (s.empty() || s.size() > 5) return std::nullopt;
    uint32_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    if (v > 0xFFFF) return std::nullopt;
    return static_cast<uint16_t>(v);
}

inline auto ParseSizes(std::string_view text) -> std::map<uint16_t, int64_t> {
    std::map<uint16_t, int64_t> out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == text.npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto tab = line.find('\t');
        if (tab == line.npos) continue;
        const auto index = ParseIndex(line.substr(0, tab));
        const auto bytes = line.substr(tab + 1);
        if (!index || bytes.empty() || bytes.size() > 18) continue;
        int64_t v = 0;
        bool ok = true;
        for (const char c : bytes) {
            if (c < '0' || c > '9') { ok = false; break; }
            v = v * 10 + (c - '0');
        }
        if (ok) out[*index] = v;
    }
    return out;
}

struct CacheEntry {
    uint16_t index{};
    std::string path; // inside the storage, never empty, never a folder
};

inline auto ParseCacheEntry(std::string_view entry) -> std::optional<CacheEntry> {
    const auto clean = sd_bundle::CleanEntry(entry);
    if (!clean || clean->ends_with('/')) return std::nullopt;
    const auto slash = clean->find('/');
    if (slash == std::string::npos || slash + 1 >= clean->size()) return std::nullopt;
    const auto index = ParseIndex(std::string_view{*clean}.substr(0, slash));
    if (!index) return std::nullopt;
    return CacheEntry{*index, clean->substr(slash + 1)};
}

constexpr auto AlignUp(int64_t v) -> int64_t {
    return (v + BLOCK - 1) / BLOCK * BLOCK;
}

// data size for a storage that must hold `payload` bytes.
// ponytail: 1/32 plus 16 MiB for the save's own tables is a guess, not measured on a console;
// the write fails with "out of space" if it is short, and a rerun extends the storage further.
constexpr auto StorageSize(int64_t payload) -> int64_t {
    if (payload < 0) payload = 0;
    if (payload > (INT64_MAX / 2)) return 0;
    return AlignUp(payload + payload / 32 + 16 * 1024 * 1024);
}

// "which user gets the save", asked once per session; the install thread reads the answer.
// choice: -1 not shown yet, -2 shown, -3 closed without a choice, 0.. the user's index.
// The first pass never waits: the file goes after the queue and the user answers meanwhile.
enum class PickStep { Use, Skip, Defer, Wait };
constexpr auto NextPickStep(int choice, bool final_pass, bool interrupted) -> PickStep {
    if (choice >= 0) return PickStep::Use;
    if (choice == -3 || interrupted) return PickStep::Skip;
    return final_pass ? PickStep::Wait : PickStep::Defer;
}

// bytes written between two commits: half the journal, at least one block.
constexpr auto CommitBudget(int64_t journal) -> int64_t {
    return journal / 2 > BLOCK ? journal / 2 : BLOCK;
}

} // namespace sphaira::save_bundle
