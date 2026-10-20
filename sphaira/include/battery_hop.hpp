#pragma once

// Pure parts of the hop to the other MMC (battery calibration step 5): which hekate boot entry starts the sysMMC or
// the emuMMC, and the one-shot boot request written into the hekate payload before it is run. No libnx here, so the
// host tests can run it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::battery::hop {

namespace detail {

inline auto Trim(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

} // namespace detail

// 1-based position of the hekate boot entry that starts Atmosphère in the sysMMC (want_emu = false) or the emuMMC.
// hekate counts every [section] except [config], in file order; that number is what autoboot= means. 0 = no such entry.
// sysMMC = emummc_force_disable=1; emuMMC = emummcforce=1, or else the first Atmosphère entry that does not force
// the sysMMC (it follows emummc.ini).
inline auto FindEntry(std::string_view ini, bool want_emu) -> int {
    int index = 0;
    int found = 0;
    int fallback = 0;
    bool in_entry = false;
    bool cfw = false, force_sys = false, force_emu = false;

    const auto close = [&]() {
        if (!in_entry || !cfw || found) {
            return;
        }
        if (want_emu) {
            if (force_emu) {
                found = index;
            } else if (!force_sys && !fallback) {
                fallback = index;
            }
        } else if (force_sys) {
            found = index;
        }
    };

    while (!ini.empty()) {
        const auto nl = ini.find('\n');
        const auto line = detail::Trim(ini.substr(0, nl));
        ini = nl == std::string_view::npos ? std::string_view{} : ini.substr(nl + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '{') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            close();
            const auto name = line.substr(1, line.size() - 2);
            in_entry = name != "config";
            if (in_entry) {
                index++;
            }
            cfw = force_sys = force_emu = false;
            continue;
        }
        const auto eq = line.find('=');
        if (!in_entry || eq == std::string_view::npos) {
            continue;
        }
        const auto key = detail::Trim(line.substr(0, eq));
        const auto val = detail::Trim(line.substr(eq + 1));
        if (key == "fss0" || key == "kernel" || (key == "atmosphere" && val == "1")) {
            cfw = true;
        } else if (key == "emummc_force_disable" && val == "1") {
            force_sys = true;
        } else if (key == "emummcforce" && val == "1") {
            force_emu = true;
        } else if (key == "payload") {
            cfw = false;
            force_sys = force_emu = false;
            in_entry = false; // a payload entry is never a CFW entry
        }
    }
    close();
    return found ? found : (want_emu ? fallback : 0);
}

// hekate keeps its boot request (boot_cfg_t, 0x84 bytes) at 0x94 of its payload and reads it at start: byte 0 holds
// the flags (bit 0 = boot at once), byte 1 the entry number, byte 2 the list (0 = hekate_ipl.ini), byte 3 extras, and
// 0x80 bytes of id / emuMMC path. false when the file is too small to be hekate.
inline constexpr std::size_t BOOT_CFG_OFF = 0x94;
inline constexpr std::size_t BOOT_CFG_SIZE = 0x84;

inline auto PatchBootCfg(std::vector<std::uint8_t>& payload, int index) -> bool {
    if (index < 1 || index > 255 || payload.size() < BOOT_CFG_OFF + BOOT_CFG_SIZE + 0x100) {
        return false;
    }
    auto* cfg = payload.data() + BOOT_CFG_OFF;
    for (std::size_t i = 0; i < BOOT_CFG_SIZE; i++) {
        cfg[i] = 0;
    }
    cfg[0] = 1; // BOOT_CFG_AUTOBOOT_EN
    cfg[1] = static_cast<std::uint8_t>(index);
    return true;
}

} // namespace sphaira::battery::hop
