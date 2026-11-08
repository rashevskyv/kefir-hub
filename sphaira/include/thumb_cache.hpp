#pragma once

// Size cap of the downloaded game pictures (/config/kefir/cache/thumbs). Pure logic: no libnx, host-testable.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::thumb_cache {

constexpr std::uint64_t CAP_BYTES = 1024ull * 1024ull * 1024ull; // 1 GiB
constexpr std::uint64_t KEEP_PERCENT = 90;                       // a trim goes down to this share of the cap

struct Entry {
    std::string path;
    std::uint64_t size{};
    std::uint64_t mtime{}; // seconds; smaller is older
};

// The small name lists in thumbs/index/ are not pictures: they are never counted and never deleted.
inline bool IsIndexPath(std::string_view rel) {
    return rel == "index" || rel.starts_with("index/");
}

inline std::uint64_t TotalSize(const std::vector<Entry>& files) {
    std::uint64_t total = 0;
    for (const auto& f : files) total += f.size;
    return total;
}

// The files to delete, oldest first. Empty while the total is within `cap`; once above it,
// enough of the oldest files go to bring the total to KEEP_PERCENT of the cap or less.
// `keep` (the picture just saved) is never chosen.
inline std::vector<std::string> PlanTrim(std::vector<Entry> files, std::uint64_t cap, std::string_view keep = {}) {
    std::vector<std::string> out;
    std::uint64_t total = TotalSize(files);
    if (total <= cap) return out;

    const std::uint64_t target = cap / 100 * KEEP_PERCENT + (cap % 100) * KEEP_PERCENT / 100;
    std::stable_sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) {
        return a.mtime != b.mtime ? a.mtime < b.mtime : a.path < b.path;
    });
    for (const auto& f : files) {
        if (total <= target) break;
        if (!keep.empty() && f.path == keep) continue;
        out.push_back(f.path);
        total -= std::min(total, f.size);
    }
    return out;
}

} // namespace sphaira::thumb_cache
