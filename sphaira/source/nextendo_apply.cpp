#include "nextendo_apply.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "nextendo_refresh.hpp"
#include "option.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"

#include <atomic>
#include <mutex>
#include <cstdio>
#include <dirent.h>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::nextendo {

namespace {

using namespace ui::menu::settings::detail;

constexpr const char* HOSTS_EMU = "/atmosphere/hosts/emummc.txt";
constexpr const char* HOSTS_SYS = "/atmosphere/hosts/sysmmc.txt";
constexpr const char* BACKUP_EMU = "/switch/prelude_hosts_backup/emummc.txt"; // same place Prelude uses
constexpr const char* BACKUP_SYS = "/switch/prelude_hosts_backup/sysmmc.txt";
constexpr const char* SETTINGS_INI = "/atmosphere/config/system_settings.ini";
constexpr const char* EXOSPHERE_INI = "/exosphere.ini";
constexpr const char* SEMISTOCK_BACKUP = "/config/semistock/emummc.ini.bkp";
constexpr const char* PATCHES_DIR = "/atmosphere/exefs_patches";
constexpr const char* PATCH_PREFIX = "nextendo_"; // the Kefir build names Nextendo's patch folders like this
constexpr const char* BROWSER_CERTS = "/atmosphere/contents/0100000000000803";
constexpr const char* STASH = "/config/semistock/nextendo"; // where the Semi-stock package puts them
constexpr const char* STASH_PATCHES = "/config/semistock/nextendo/exefs_patches";
constexpr const char* STASH_BROWSER = "/config/semistock/nextendo/contents/0100000000000803";
constexpr std::size_t MAX_HOSTS_SIZE = 64 * 1024; // a hosts file bigger than this is surely not ours

option::OptionBool g_emu{"nextendo", "emu_enabled", true};
option::OptionBool g_sys{"nextendo", "sysnand_enabled", false};
option::OptionString g_ip_main{"nextendo", "server_ip", ""};   // refreshed from the newest release
option::OptionString g_ip_nncs2{"nextendo", "server_ip_nncs2", ""};
std::atomic<bool> g_restart_needed{false};
std::mutex g_mutex;    // Reconcile runs at start, from the settings page and after an IP refresh
std::mutex g_ip_mutex; // the stored IP options are read by the main thread and written by the refresh

auto ReadHosts(const char* path) -> std::optional<std::string> {
    if (!fs::FileExists(path)) {
        return std::nullopt;
    }
    auto text = ReadTextFile(path);
    if (text.size() > MAX_HOSTS_SIZE) {
        return std::string{"# oversized file, not ours\n"}; // reads as a user file, which is backed up before any write
    }
    return text;
}

auto WriteText(const char* path, const std::string& text) -> Result {
    R_TRY(EnsureParentDirectory(path));
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        R_THROW(fsdevGetLastResult());
    }
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const auto rc = fsdevGetLastResult();
    std::fclose(f);
    if (!ok) {
        R_THROW(R_SUCCEEDED(rc) ? MAKERESULT(Module_Libnx, LibnxError_IoError) : rc);
    }
    R_SUCCEED();
}

// An ini key as `expected` text; returns true when it had to be written.
auto EnsureIni(const char* path, const char* section, const char* key, const char* value, bool& changed) -> Result {
    if (IniValueEquals(path, section, key, value)) {
        R_SUCCEED();
    }
    R_TRY(SetIniValue(path, section, key, value));
    changed = true;
    R_SUCCEED();
}

auto ListDir(const char* path) -> std::vector<std::string> {
    std::vector<std::string> names;
    if (auto* d = opendir(path)) {
        while (const auto* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name != "." && name != "..") {
                names.push_back(name);
            }
        }
        closedir(d);
    }
    return names;
}

auto HasPatchPrefix(const std::string& name) -> bool {
    return name.rfind(PATCH_PREFIX, 0) == 0;
}

auto PatchesPresent() -> bool {
    for (const auto& name : ListDir(PATCHES_DIR)) {
        if (HasPatchPrefix(name)) {
            return true;
        }
    }
    return fs::DirExists(BROWSER_CERTS);
}

auto StashPresent() -> bool {
    return !ListDir(STASH_PATCHES).empty() || fs::DirExists(STASH_BROWSER);
}

auto Stash() -> Result {
    for (const auto& name : ListDir(PATCHES_DIR)) {
        if (HasPatchPrefix(name)) {
            R_TRY(MovePath(std::string{PATCHES_DIR} + "/" + name, std::string{STASH_PATCHES} + "/" + name));
        }
    }
    return MovePath(BROWSER_CERTS, STASH_BROWSER);
}

auto Unstash() -> Result {
    for (const auto& name : ListDir(STASH_PATCHES)) {
        R_TRY(MovePath(std::string{STASH_PATCHES} + "/" + name, std::string{PATCHES_DIR} + "/" + name));
    }
    return MovePath(STASH_BROWSER, BROWSER_CERTS);
}

auto Expected() -> std::string {
    return BuildHosts(ServerIpsInUse());
}

auto Backup(const char* hosts, const char* backup) -> Result {
    if (fs::FileExists(backup)) {
        R_SUCCEED(); // never overwrite an older backup: it holds the user's file from before Nextendo
    }
    return CopyFileSimple(hosts, backup);
}

auto RestoreBackup(const char* hosts, const char* backup) -> Result {
    if (fs::FileExists(hosts) || !fs::FileExists(backup)) {
        R_SUCCEED();
    }
    R_TRY(CopyFileSimple(backup, hosts));
    R_TRY(DeletePath(backup));
    R_SUCCEED();
}

} // namespace

auto ServerIpsInUse() -> ServerIps {
    std::scoped_lock lock{g_ip_mutex};
    ServerIps stored{g_ip_main.Get(), g_ip_nncs2.Get()};
    return ArePublic(stored) ? stored : SnapshotIps();
}

void StoreServerIps(const ServerIps& ips) {
    std::scoped_lock lock{g_ip_mutex};
    if (ArePublic(ips)) {
        g_ip_main.Set(ips.main);
        g_ip_nncs2.Set(ips.nncs2);
    }
}

auto EmuEnabled() -> bool { return g_emu.Get(); }
void SetEmuEnabled(bool enabled) { g_emu.Set(enabled); }
auto SysEnabled() -> bool { return g_sys.Get(); }
void SetSysEnabled(bool enabled) { g_sys.Set(enabled); }

auto RestartNeeded() -> bool { return g_restart_needed.load(); }

auto CurrentMode() -> Mode {
    if (App::IsEmummc()) {
        return Mode::EmuMMC;
    }
    return fs::FileExists(SEMISTOCK_BACKUP) ? Mode::SemiStock : Mode::Other;
}

auto FirmwareOk() -> bool {
    const auto v = hosversionGet();
    return FirmwareSupported(HOSVER_MAJOR(v), HOSVER_MINOR(v), HOSVER_MICRO(v));
}

auto Reconcile(bool& changed) -> Result {
    std::scoped_lock lock{g_mutex};
    const auto expected = Expected();
    R_UNLESS(!expected.empty(), MAKERESULT(Module_Libnx, LibnxError_BadInput));

    Inputs in;
    in.mode = CurrentMode();
    in.firmware_ok = FirmwareOk();
    in.emu_enabled = EmuEnabled();
    in.sys_enabled = SysEnabled();
    in.emu_file = Classify(ReadHosts(HOSTS_EMU), expected);
    in.sys_file = Classify(ReadHosts(HOSTS_SYS), expected);
    in.patches_present = PatchesPresent();
    in.stash_present = StashPresent();

    if (in.mode == Mode::EmuMMC && SysEnabled()) {
        SetSysEnabled(false); // the sysNAND option exists in Semi-stock only
    }

    for (const auto step : MakePlan(in)) {
        switch (step) {
            case Step::BackupEmuUserHosts: R_TRY(Backup(HOSTS_EMU, BACKUP_EMU)); break;
            case Step::BackupSysUserHosts: R_TRY(Backup(HOSTS_SYS, BACKUP_SYS)); break;
            case Step::WriteEmuHosts: R_TRY(WriteText(HOSTS_EMU, expected)); changed = true; break;
            case Step::WriteSysHosts: R_TRY(WriteText(HOSTS_SYS, expected)); changed = true; break;
            case Step::RemoveEmuHosts: R_TRY(DeletePath(HOSTS_EMU)); changed = true; break;
            case Step::RemoveSysHosts: R_TRY(DeletePath(HOSTS_SYS)); changed = true; break;
            case Step::RestoreEmuUserHosts: R_TRY(RestoreBackup(HOSTS_EMU, BACKUP_EMU)); break;
            case Step::RestoreSysUserHosts: R_TRY(RestoreBackup(HOSTS_SYS, BACKUP_SYS)); break;
            case Step::BlankProdinfoEmu0: R_TRY(EnsureIni(EXOSPHERE_INI, "exosphere", "blank_prodinfo_emummc", "0", changed)); break;
            case Step::BlankProdinfoEmu1: R_TRY(EnsureIni(EXOSPHERE_INI, "exosphere", "blank_prodinfo_emummc", "1", changed)); break;
            case Step::BlankProdinfoSys0: R_TRY(EnsureIni(EXOSPHERE_INI, "exosphere", "blank_prodinfo_sysmmc", "0", changed)); break;
            case Step::DnsMitmForNextendo:
                R_TRY(EnsureIni(SETTINGS_INI, "atmosphere", "enable_dns_mitm", "u8!0x1", changed));
                R_TRY(EnsureIni(SETTINGS_INI, "atmosphere", "add_defaults_to_dns_hosts", "u8!0x0", changed));
                break;
            case Step::StashPatches: R_TRY(Stash()); changed = true; break;
            case Step::UnstashPatches: R_TRY(Unstash()); changed = true; break;
            case Step::DnsMitmDefaults:
                R_TRY(EnsureIni(SETTINGS_INI, "atmosphere", "add_defaults_to_dns_hosts", "u8!0x1", changed));
                break;
        }
    }

    if (changed) {
        g_restart_needed = true;
        fsdevCommitDevice("sdmc");
        log_write("[nextendo] card updated; restart needed (mode %d, firmware ok %d)\n", static_cast<int>(in.mode), in.firmware_ok);
    }
    R_SUCCEED();
}

} // namespace sphaira::nextendo
