#include "meminfo.hpp"
#include "defines.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cstdio>
#include <switch.h>

namespace sphaira::meminfo {
namespace {

auto WalkPrivateMemory(Handle debug) -> u64 {
    u64 addr{};
    u64 total{};
    for (;;) {
        MemoryInfo mem{};
        u32 page{};
        if (R_FAILED(svcQueryDebugProcessMemory(&mem, &page, debug, addr))) {
            break;
        }
        switch (mem.type & MemState_Type) {
            case MemType_Unmapped:
                break;
            default:
                if (mem.size && mem.perm) {
                    total += mem.size;
                }
                break;
        }
        const u64 next = mem.addr + mem.size;
        if (!mem.size || next <= addr) {
            break;
        }
        addr = next;
    }
    return total;
}

auto FillFromHandle(Handle h, RamPool& out) -> void {
    svcGetInfo(&out.total, InfoType_TotalMemorySize, h, 0);
    svcGetInfo(&out.used, InfoType_UsedMemorySize, h, 0);
}

} // namespace

auto QueryPool(u64 pool) -> RamPool {
    RamPool out{};
    svcGetSystemInfo(&out.total, SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, pool);
    svcGetSystemInfo(&out.used, SystemInfoType_UsedPhysicalMemorySize, INVALID_HANDLE, pool);
    return out;
}

auto QuerySystem() -> RamPool {
    return QueryPool(PhysicalMemorySystemInfo_System);
}

auto QueryProcess(u64 pid) -> RamPool {
    RamPool out{};
    if (!pid) {
        return out;
    }

    u64 self{};
    svcGetProcessId(&self, CUR_PROCESS_HANDLE);
    if (pid == self) {
        FillFromHandle(CUR_PROCESS_HANDLE, out);
        return out;
    }

    Handle debug{};
    if (R_FAILED(svcDebugActiveProcess(&debug, pid))) {
        return out;
    }
    ON_SCOPE_EXIT(svcCloseHandle(debug));

    DebugEventInfo ev{};
    while (R_SUCCEEDED(svcGetDebugEvent(&ev, debug))) {}

    FillFromHandle(debug, out);
    if (!out.used) {
        out.used = WalkPrivateMemory(debug);
    }
    if (!out.total) {
        out.total = out.used;
    }
    return out;
}

auto MeasurePid(u64 pid) -> u64 {
    return QueryProcess(pid).used;
}

void DrawRamBars(NVGcontext* vg, Theme* theme, float x, float y,
    const RamPool& system, const RamPool& overlay, bool overlay_running) {
    const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto text = theme->GetColour(ThemeEntryID_TEXT);
    const float bar_w = 1120.f;
    const float bar_h = 10.f;

    const auto row = [&](float yy, const char* label, const RamPool& pool, NVGcolor fill, bool show) {
        ui::gfx::drawTextArgs(vg, x, yy, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s", label);
        if (show && pool.total) {
            ui::gfx::drawTextArgs(vg, x + 280.f, yy, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info,
                "%s / %s",
                utils::formatSizeStorage(pool.used).c_str(),
                utils::formatSizeStorage(pool.total).c_str());
            ui::gfx::drawTextArgs(vg, x + bar_w, yy, 14.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info,
                "%s %s",
                utils::formatSizeStorage(pool.Free()).c_str(),
                "free"_i18n.c_str());
        } else {
            ui::gfx::drawTextArgs(vg, x + bar_w, yy, 13.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info,
                "%s", "not running"_i18n.c_str());
        }

        ui::gfx::drawRect(vg, x, yy + 12.f, bar_w, bar_h, nvgRGBA(255, 255, 255, 28), 3.f);
        if (show && pool.total) {
            const float used_w = bar_w * std::min(1.f, static_cast<float>(pool.used) / static_cast<float>(pool.total));
            ui::gfx::drawRect(vg, x, yy + 12.f, used_w, bar_h, fill, 3.f);
        }
    };

    row(y + 8.f, "Sysmodule RAM"_i18n.c_str(), system, nvgRGBA(80, 160, 230, 220), system.total != 0);

    const bool overlay_ok = overlay_running && overlay.total;
    ui::gfx::drawTextArgs(vg, x, y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s", "Overlay memory"_i18n.c_str());
    if (overlay_ok) {
        ui::gfx::drawTextArgs(vg, x + 280.f, y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info,
            "%s / %s",
            utils::formatSizeStorage(overlay.used).c_str(),
            utils::formatSizeStorage(overlay.total).c_str());
        ui::gfx::drawTextArgs(vg, x + 1120.f, y + 40.f, 14.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info,
            "%s %s",
            utils::formatSizeStorage(overlay.Free()).c_str(),
            "free"_i18n.c_str());
    } else {
        ui::gfx::drawTextArgs(vg, x + 1120.f, y + 40.f, 13.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info,
            "%s", overlay_running ? "cannot measure"_i18n.c_str() : "not running"_i18n.c_str());
    }
    ui::gfx::drawRect(vg, x, y + 52.f, 1120.f, 10.f, nvgRGBA(255, 255, 255, 28), 3.f);
    if (overlay_ok) {
        const float used_w = 1120.f * std::min(1.f, static_cast<float>(overlay.used) / static_cast<float>(overlay.total));
        ui::gfx::drawRect(vg, x, y + 52.f, used_w, 10.f, nvgRGBA(160, 120, 220, 220), 3.f);
    }
}

} // namespace sphaira::meminfo
