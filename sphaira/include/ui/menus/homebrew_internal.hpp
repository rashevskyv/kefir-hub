#pragma once

#include "ui/menus/homebrew.hpp"
#include "option.hpp"

#include <string_view>
#include <vector>
#include <switch.h>

namespace sphaira::ui::menu::homebrew {

extern UEvent g_change_uevent;
extern option::OptionBool g_kefir_updater_notice_ack;
inline constexpr const char* KEFIR_UPDATER_STUB_PATH = "/switch/kefir-updater/kefir-updater.nro";

inline auto GenerateStarPath(const fs::FsPath& nro_path) -> fs::FsPath {
    fs::FsPath out{};
    const auto dilem = std::strrchr(nro_path.s, '/');
    std::snprintf(out, sizeof(out), "%.*s.%s.star", int(dilem - nro_path.s + 1), nro_path.s, dilem + 1);
    return out;
}

inline void FreeEntry(NVGcontext* vg, NroEntry& e) {
    nvgDeleteImage(vg, e.image);
    e.image = 0;
}

inline auto GetNroFilename(const NroEntry& e) -> std::string {
    std::string filename;
    if (const auto dilem = std::strrchr(e.path.s, '/')) {
        filename = dilem + 1;
    } else {
        filename = e.path.s;
    }
    return filename;
}

inline auto IsKefirUpdaterStub(const NroEntry& e) -> bool {
    return e.path == KEFIR_UPDATER_STUB_PATH;
}

inline auto IsKefirUpdaterEntry(const NroEntry& e) -> bool {
    return IsKefirUpdaterStub(e) ||
        !strcasecmp(e.GetName(), "Kefir Updater") ||
        !strcasecmp(e.path, KEFIR_UPDATER_STUB_PATH);
}

void ShowKefirUpdaterRemovedDialog();

} // namespace sphaira::ui::menu::homebrew
