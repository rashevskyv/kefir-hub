#pragma once

#include "ui/types.hpp"

#include <cstdint>

namespace sphaira::meminfo {

struct RamPool {
    u64 used{};
    u64 total{};

    auto Free() const -> u64 {
        return total > used ? total - used : 0;
    }
};

auto QueryPool(u64 pool) -> RamPool;
auto QuerySystem() -> RamPool;

void DrawSystemPool(NVGcontext* vg, Theme* theme, float x, float y, const RamPool& pool);

} // namespace sphaira::meminfo
