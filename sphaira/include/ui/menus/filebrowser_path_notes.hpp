#pragma once

// Short descriptions for the files and folders a Kefir card carries. The file
// browser draws the note under the entry name (list layout). Pure header: no
// libnx, host-tested in tests/test_filebrowser_path_notes.cpp.
//
// The note strings are i18n keys. They are not "..."_i18n literals, so
// tools/i18n-translate/sync does not pick them up: tools/i18n-translate/add_path_notes.py
// copies them into en.json, then translate.py fills the other languages.

#include <cctype>
#include <cstddef>
#include <string_view>

namespace sphaira::ui::menu::filebrowser {

struct PathNote {
    const char* dir;  // folder that holds the entry, from the card root, no trailing slash; "/" is the root
    const char* name; // entry name, matched case-insensitively
    const char* note; // English text, i18n key
};

inline constexpr PathNote PATH_NOTES[] = {
    // card root
    {"/", "atmosphere", "Atmosphere: the custom firmware"},
    {"/", "bootloader", "hekate: boot menu and its settings"},
    {"/", "config", "Settings of Kefir, Kefir Hub and system modules"},
    {"/", "switch", "Homebrew apps and overlays"},
    {"/", "themes", "HOME menu theme patches"},
    {"/", "games", "Forwarder for Homebrew Menu, install with Kefir Hub or DBI"},
    {"/", "warmboot_mariko", "Sleep firmware for Mariko consoles, hekate needs it"},
    {"/", "emuMMC", "Your emuMMC: copy of the system. Never delete"},
    {"/", "Nintendo", "Games and saves of sysMMC kept on the card"},
    {"/", "backup", "NAND and key backups made in hekate"},
    {"/", "dumps", "Game and save dumps made by Kefir Hub"},
    {"/", "payload.bin", "hekate, the payload the console starts first"},
    {"/", "boot.dat", "SX-type modchip loader"},
    {"/", "boot.ini", "Tells an SX-type modchip to start payload.bin"},
    {"/", "hbmenu.nro", "Homebrew Menu, started from the Album"},
    {"/", "exosphere.ini", "Atmosphere low-level settings: debug mode, blank PRODINFO"},
    {"/", "install.bat", "Windows installer of the Kefir package"},
    {"/", "startup.te", "One-shot TegraExplorer script written by Kefir Hub"},

    // atmosphere
    {"/atmosphere", "package3", "Atmosphere itself: kernel patches and system modules"},
    {"/atmosphere", "stratosphere.romfs", "Part of Atmosphere, shipped with package3"},
    {"/atmosphere", "reboot_payload.bin", "Payload started by Reboot to payload"},
    {"/atmosphere", "hbl.nsp", "Homebrew Loader: runs .nro apps from the Album"},
    {"/atmosphere", "hbl_html", "Files of the Homebrew Loader"},
    {"/atmosphere", "splash.png", "Picture shown while Atmosphere starts"},
    {"/atmosphere", "config", "Atmosphere settings read at boot"},
    {"/atmosphere", "config_templates", "Atmosphere reference settings, not read at boot"},
    {"/atmosphere", "contents", "System modules, game mods and cheats"},
    {"/atmosphere", "exefs_patches", "Patches applied to system modules in memory"},
    {"/atmosphere", "nro_patches", "Patches applied to homebrew in memory"},
    {"/atmosphere", "kips", "Kernel modules loaded at boot"},
    {"/atmosphere", "hosts", "Network host rules: block or redirect a domain"},
    {"/atmosphere", "crash_reports", "Reports written after a crash, safe to delete"},
    {"/atmosphere", "fatal_reports", "Reports written after a fatal error, safe to delete"},
    {"/atmosphere", "erpt_reports", "System error reports, safe to delete"},
    {"/atmosphere", "automatic_backups", "PRODINFO and BIS key backups. Keep them"},
    {"/atmosphere/config", "override_config.ini", "Which button and title start the Homebrew Loader"},
    {"/atmosphere/config", "system_settings.ini", "System switches: 40MB memory, USB 3.0 and more"},
    {"/atmosphere/config", "stratosphere.ini", "Options of Atmosphere modules"},
    {"/atmosphere/config", "system_settings_stock.ini", "Second copy of system_settings.ini kept by Kefir"},
    {"/atmosphere/exefs_patches", "disable_ca_verification", "Allows servers with their own certificates"},
    {"/atmosphere/exefs_patches", "bluetooth_patches", "Needed by MissionControl"},
    {"/atmosphere/exefs_patches", "btm_patches", "Needed by MissionControl"},
    {"/atmosphere/exefs_patches", "fatal_force_extra_info", "Shows full data on a fatal error"},

    // bootloader
    {"/bootloader", "hekate_ipl.ini", "hekate main settings: boot entries, autoboot"},
    {"/bootloader", "hekate_ipl_.ini", "Spare copy of hekate_ipl.ini"},
    {"/bootloader", "ini", "Extra hekate boot entries"},
    {"/bootloader", "nyx.ini", "Settings of Nyx, the hekate touch interface"},
    {"/bootloader", "nyx.ini_", "Spare copy of nyx.ini"},
    {"/bootloader", "payloads", "Payloads listed in hekate"},
    {"/bootloader", "sys", "Parts of hekate"},
    {"/bootloader", "res", "Icons of the boot entries"},
    {"/bootloader", "update.bin", "hekate itself, started when newer than the sent payload"},
    {"/bootloader", "bootlogo_kefir.bmp", "Boot picture"},
    {"/bootloader", "updating.bmp", "Picture shown during a Kefir update"},
    {"/bootloader/payloads", "fusee.bin", "Atmosphere"},
    {"/bootloader/payloads", "TegraExplorer.bin", "Kefir update and NAND tools"},
    {"/bootloader/payloads", "Lockpick_RCM.bin", "Dumps the console keys"},
    {"/bootloader/ini", "atmostock.ini", "Semi-stock boot entry (legacy)"},
    {"/bootloader/ini", "kefir_updater.ini", "Update Kefir boot entry, starts TegraExplorer"},
    {"/bootloader/ini", "!kefir_updater.ini", "Boot entry used by the Kefir auto-update"},
    {"/bootloader/sys", "nyx.bin", "hekate touch interface"},
    {"/bootloader/sys", "res.pak", "Images of the hekate interface"},
    {"/bootloader/sys", "emummc.kipm", "emuMMC support of hekate"},
    {"/bootloader/sys", "libsys_lp0.bso", "Sleep mode support of hekate"},
    {"/bootloader/sys", "libsys_minerva.bso", "RAM training of hekate"},
    {"/bootloader/sys", "l4t", "Linux and Android boot support"},

    // config
    {"/config", "kefir", "Kefir Hub: settings, locations, logs, transfer data"},
    {"/config", "sphaira", "Old Kefir Hub folder, moved to kefir on first start"},
    {"/config", "kefir-updater", "Kefir updater settings"},
    {"/config", ".skip", "Folders the Kefir updater skips"},
    {"/config", "oc", "Overclock files, see Kefir Settings"},
    {"/config", "oc_bkp", "Backup of the overclock files while overclock is off"},
    {"/config", "8gb", "Files for consoles with 8 GB RAM, see Kefir Settings"},
    {"/config", "semistock", "Script that undoes Semi-stock"},
    {"/config", "MissionControl", "Settings of MissionControl (Bluetooth controllers)"},
    {"/config", "sys-con", "Settings of sys-con (USB controllers)"},
    {"/config", "ultrahand", "Settings and themes of the Ultrahand overlay menu"},
    {"/config", "sys-clk", "sys-clk profiles (overclock)"},
    {"/config", "redirect.bin", "Created by Redirect Emunand saves to SD"},

    // config/kefir (the earlier in-code notes keep their text)
    {"/config/kefir", "account_link_rollback", "Safety snapshots created before changing Nintendo Account link data"},
    {"/config/kefir", "account_save_dump", "Legacy read-only dump of account save 0010"},
    {"/config/kefir", "restore_pending", "Staged account restore and rollback state"},
    {"/config/kefir", "account_backups", "KefirHub account backup library"},
    {"/config/kefir", "user_packs", "Legacy KefirHub account backup library"},
    {"/config/kefir", "playtime_pending", "Files awaiting TegraExplorer play time restoration"},
    {"/config/kefir", "nand_transfer", "Temporary files and scripts for NAND transfer"},
    {"/config/kefir", "assoc", "File type associations"},
    {"/config/kefir", "themes", "Custom user interface themes"},
    {"/config/kefir", "github", "Downloaded GitHub packages and repositories"},
    {"/config/kefir", "i18n", "Custom interface translations"},
    {"/config/kefir", "downloads", "Downloaded files and updates"},
    {"/config/kefir", "packages", "Package definitions and metadata"},
    {"/config/kefir", "logo", "Custom startup logo and animation"},
    {"/config/kefir", "cache", "Application cache files"},
    {"/config/kefir", "avatars", "Custom user avatar images"},
    {"/config/kefir", "config.ini", "Kefir Hub settings"},
    {"/config/kefir", "locations.ini", "Network locations from Sources"},
    {"/config/kefir", "playlog.ini", "Play time log"},
    {"/config/kefir", "log.txt", "Kefir Hub log of this start"},
    {"/config/kefir", "log.prev.txt", "Kefir Hub log of the previous start"},
    {"/config/kefir", "errors.txt", "Install errors from every start"},
    {"/config/kefir", "appstore_sources.txt", "Extra App Store sources, one address per line"},

    // switch
    {"/switch", ".overlays", "Tesla and Ultrahand overlays"},
    {"/switch", ".packages", "Ultrahand packages: the Kefir Menu"},
    {"/switch", "kefir-hub.nro", "Kefir Hub"},
    {"/switch", "kefir-updater", "Kefir updater app and script"},
    {"/switch", "DBI", "DBI installer"},
    {"/switch", "daybreak", "Daybreak firmware updater"},
    {"/switch", "linkalho", "Links a user to an account without a network"},
    {"/switch", "NX-Activity-Log", "Play time statistics"},
    {"/switch", "NxThemesInstaller", "Installs HOME menu themes"},
    {"/switch", "appstore", "What the Homebrew App Store installed"},
    {"/switch", "sphaira", "Kefir Hub cache, safe to delete"},
    {"/switch/.overlays", "ovlmenu.ovl", "Ultrahand overlay menu"},
    {"/switch/.overlays", "ovlSysmodules.ovl", "Start and stop system modules"},
    {"/switch/.overlays", "ovlEdiZon.ovl", "Cheats"},
    {"/switch/.overlays", "NX-FanControl.ovl", "Fan curve"},
    {"/switch/.overlays", "sys-patch-overlay.ovl", "sys-patch status"},

    // others
    {"/themes", "systemPatches", "Patches NXThemesInstaller needs, one per firmware"},
    {"/emuMMC", "emummc.ini", "Where the emuMMC lives: partition or files"},
};

namespace detail_notes {
inline auto EqualsIC(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); i++) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}
} // namespace detail_notes

// dir is the folder being listed ("/", "/config/kefir", with or without a trailing slash).
// returns the English note or nullptr.
inline auto FindPathNote(std::string_view dir, std::string_view name) -> const char* {
    while (dir.size() > 1 && dir.ends_with('/')) {
        dir.remove_suffix(1);
    }
    if (dir.empty()) {
        dir = "/";
    }
    for (const auto& n : PATH_NOTES) {
        if (detail_notes::EqualsIC(dir, n.dir) && detail_notes::EqualsIC(name, n.name)) {
            return n.note;
        }
    }
    return nullptr;
}

} // namespace sphaira::ui::menu::filebrowser
