#include "meminfo.hpp"
#include "defines.hpp"
#include "ui/menus/uninstaller_menu.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cstring>
#include <switch.h>

namespace sphaira::meminfo {
namespace {

auto WalkPrivateMemory(Handle debug) -> u64 {
    MemoryInfo mem{};
    u32 page{};
    u64 addr{};
    u64 total{};
    while (R_SUCCEEDED(svcQueryDebugProcessMemory(&mem, &page, debug, addr))) {
        addr = mem.addr + mem.size;
        switch (mem.type) {
            case MemType_CodeStatic:
            case MemType_CodeMutable:
            case MemType_Heap:
            case MemType_Stack:
            case MemType_ThreadLocal:
                total += mem.size;
                break;
            default:
                break;
        }
        if (!addr) {
            break;
        }
    }
    return total;
}

} // namespace

auto QueryPool(u64 pool) -> RamPool {
    RamPool out{};
    svcGetSystemInfo(&out.total, SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, pool);
    svcGetSystemInfo(&out.used, SystemInfoType_UsedPhysicalMemorySize, INVALID_HANDLE, pool);
    return out;
}

void QueryBreakdown(RamBreakdown& out) {
    out.application = QueryPool(PhysicalMemorySystemInfo_Application);
    out.applet = QueryPool(PhysicalMemorySystemInfo_Applet);
    out.system = QueryPool(PhysicalMemorySystemInfo_System);
    out.system_unsafe = QueryPool(PhysicalMemorySystemInfo_SystemUnsafe);
    out.this_app_bytes = 0;
    svcGetInfo(&out.this_app_bytes, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
}

auto MeasurePid(u64 pid) -> u64 {
    u64 self_pid{};
    if (R_SUCCEEDED(svcGetProcessId(&self_pid, CUR_PROCESS_HANDLE)) && pid == self_pid) {
        u64 used{};
        if (R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))) {
            return used;
        }
    }

    Handle debug{};
    if (R_FAILED(svcDebugActiveProcess(&debug, pid))) {
        return 0;
    }
    ON_SCOPE_EXIT(svcCloseHandle(debug));

    u64 used{};
    if (R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, debug, 0)) && used) {
        return used;
    }
    return WalkPrivateMemory(debug);
}

auto ListProcesses() -> std::vector<ProcessRam> {
    std::vector<ProcessRam> out;
    u64 pids[0x80]{};
    s32 count{};
    if (R_FAILED(svcGetProcessList(&count, pids, std::size(pids))) || count <= 0) {
        return out;
    }

    u64 self_pid{};
    svcGetProcessId(&self_pid, CUR_PROCESS_HANDLE);

    for (s32 i = 0; i < count; i++) {
        ProcessRam p;
        p.pid = pids[i];

        if (pids[i] == self_pid) {
            p.memory_bytes = MeasurePid(pids[i]);
            svcGetInfo(&p.program_id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
            if (auto named = ui::menu::hats::GetModuleName(p.program_id); !named.empty()) {
                p.name = std::move(named);
            } else {
                p.name = "Kefir Hub";
            }
            out.push_back(std::move(p));
            continue;
        }

        Handle debug{};
        if (R_FAILED(svcDebugActiveProcess(&debug, pids[i]))) {
            continue;
        }
        ON_SCOPE_EXIT(svcCloseHandle(debug));

        char raw_name[13]{};
        DebugEventInfo ev{};
        while (R_SUCCEEDED(svcGetDebugEvent(&ev, debug))) {
            if (ev.type == DebugEventType_CreateProcess) {
                p.program_id = ev.info.create_process.program_id;
                std::memcpy(raw_name, ev.info.create_process.name, 12);
                break;
            }
        }

        u64 used{};
        if (R_FAILED(svcGetInfo(&used, InfoType_UsedMemorySize, debug, 0)) || !used) {
            used = WalkPrivateMemory(debug);
        }
        p.memory_bytes = used;

        if (auto named = ui::menu::hats::GetModuleName(p.program_id); !named.empty()) {
            p.name = std::move(named);
        } else if (raw_name[0]) {
            p.name = raw_name;
        } else if (p.program_id) {
            char tid[17]{};
            std::snprintf(tid, sizeof(tid), "%016llX", static_cast<unsigned long long>(p.program_id));
            p.name = tid;
        } else {
            continue;
        }
        out.push_back(std::move(p));
    }

    std::sort(out.begin(), out.end(), [](const ProcessRam& a, const ProcessRam& b) {
        return a.memory_bytes > b.memory_bytes;
    });
    return out;
}

void DrawBreakdown(NVGcontext* vg, ui::Theme* theme, float x, float y,
    const RamBreakdown& ram, u64 system_slice, const char* system_extra) {
    const float bar_x = x + 440.f;
    const float bar_w = 680.f;
    const float bar_h = 9.f;
    const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);

    const auto row = [&](float yy, const char* label, const RamPool& pool, NVGcolor fill, u64 slice) {
        gfx::drawTextArgs(vg, x, yy, 13.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info,
            "%s  %s / %s",
            label,
            utils::formatSizeStorage(pool.used).c_str(),
            utils::formatSizeStorage(pool.total).c_str());
        gfx::drawRect(vg, bar_x, yy - bar_h / 2.f, bar_w, bar_h, nvgRGBA(255, 255, 255, 28), 3.f);
        if (pool.total) {
            const float used_w = bar_w * std::min(1.f, static_cast<float>(pool.used) / static_cast<float>(pool.total));
            gfx::drawRect(vg, bar_x, yy - bar_h / 2.f, used_w, bar_h, fill, 3.f);
            if (slice && slice <= pool.used) {
                const float slice_w = bar_w * static_cast<float>(slice) / static_cast<float>(pool.total);
                gfx::drawRect(vg, bar_x, yy - bar_h / 2.f, slice_w, bar_h, nvgRGBA(76, 190, 120, 255), 3.f);
            }
        }
    };

    char system_label[96]{};
    if (system_extra && *system_extra) {
        std::snprintf(system_label, sizeof(system_label), "%s (%s)", "System"_i18n.c_str(), system_extra);
    } else {
        std::snprintf(system_label, sizeof(system_label), "%s", "System"_i18n.c_str());
    }

    row(y + 8.f, "Application"_i18n.c_str(), ram.application, nvgRGBA(80, 160, 230, 220), 0);
    row(y + 24.f, "Applet"_i18n.c_str(), ram.applet, nvgRGBA(160, 120, 220, 220), 0);
    row(y + 40.f, system_label, ram.system, nvgRGBA(90, 90, 90, 220), system_slice);
}

} // namespace sphaira::meminfo
