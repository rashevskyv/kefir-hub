#pragma once

#include <algorithm>
#include <cstdint>

namespace sphaira::ui {

// Calculates the used ratio [0.0, 1.0] for storage display safely handling
// zero/negative total space and out-of-bounds free space values.
inline double CalculateStorageUsedRatio(std::int64_t total, std::int64_t free) {
    if (total <= 0) {
        return 0.0;
    }
    const std::int64_t clamped_free = std::clamp(free, static_cast<std::int64_t>(0), total);
    return static_cast<double>(total - clamped_free) / static_cast<double>(total);
}

// Calculates free space in gigabytes safely, returning 0.0 if total <= 0
inline double CalculateStorageFreeGb(std::int64_t total, std::int64_t free) {
    if (total <= 0) {
        return 0.0;
    }
    const std::int64_t clamped_free = std::clamp(free, static_cast<std::int64_t>(0), total);
    return static_cast<double>(clamped_free) / static_cast<double>(0x40000000);
}

} // namespace sphaira::ui
