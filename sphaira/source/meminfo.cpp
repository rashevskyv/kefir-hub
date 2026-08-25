#include "meminfo.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cstdio>
#include <switch.h>

namespace sphaira::meminfo {

auto QueryPool(u64 pool) -> RamPool {
    RamPool out{};
    svcGetSystemInfo(&out.total, SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, pool);
    svcGetSystemInfo(&out.used, SystemInfoType_UsedPhysicalMemorySize, INVALID_HANDLE, pool);
    return out;
}

auto QuerySystem() -> RamPool {
    return QueryPool(PhysicalMemorySystemInfo_System);
}

void DrawSystemPool(NVGcontext* vg, Theme* theme, float x, float y, const RamPool& pool) {
    const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto text = theme->GetColour(ThemeEntryID_TEXT);
    const float bar_x = x;
    const float bar_y = y + 20.f;
    const float bar_w = 1120.f;
    const float bar_h = 10.f;

    ui::gfx::drawTextArgs(vg, x, y + 8.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text,
        "%s  %s / %s",
        "Sysmodule RAM"_i18n.c_str(),
        utils::formatSizeStorage(pool.used).c_str(),
        utils::formatSizeStorage(pool.total).c_str());
    ui::gfx::drawTextArgs(vg, x + bar_w, y + 8.f, 14.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info,
        "%s %s",
        utils::formatSizeStorage(pool.Free()).c_str(),
        "free"_i18n.c_str());

    ui::gfx::drawRect(vg, bar_x, bar_y, bar_w, bar_h, nvgRGBA(255, 255, 255, 28), 3.f);
    if (pool.total) {
        const float used_w = bar_w * std::min(1.f, static_cast<float>(pool.used) / static_cast<float>(pool.total));
        ui::gfx::drawRect(vg, bar_x, bar_y, used_w, bar_h, nvgRGBA(80, 160, 230, 220), 3.f);
    }
}

} // namespace sphaira::meminfo
