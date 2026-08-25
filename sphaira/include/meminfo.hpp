#pragma once

#include "ui/types.hpp"

#include <cstdint>

namespace sphaira::meminfo {

struct RamPool {
    u64 used{};
    u64 total{};
};

struct RamBreakdown {
    RamPool application;
    RamPool applet;
    RamPool system;
    RamPool system_unsafe;
    u64 this_app_bytes{};
};

auto QueryPool(u64 pool) -> RamPool;
void QueryBreakdown(RamBreakdown& out);
auto MeasurePid(u64 pid) -> u64;

void DrawBreakdown(NVGcontext* vg, Theme* theme, float x, float y,
    const RamBreakdown& ram, u64 system_slice = 0, const char* system_extra = nullptr);

} // namespace sphaira::meminfo
