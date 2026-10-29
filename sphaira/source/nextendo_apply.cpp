#include "nextendo_apply.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "nextendo_patches.hpp"
#include "nextendo_refresh.hpp"
#include "option.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
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
constexpr const char* PATCH_ROOTS[] = {"atmosphere/exefs_patches/", "atmosphere/nro_patches/"};
constexpr const char* ROMFS_SD = "romfs:/nextendo/sd/"; // the Hub's own copy of the patch files
constexpr const char* STASH = "/config/semistock/nextendo"; // where the Semi-stock package puts them
constexpr u64 SSL_PROGRAM_ID = 0x0100000000000024;
constexpr std::size_t MAX_HOSTS_SIZE = 64 * 1024; // a hosts file bigger than this is surely not ours

option::OptionBool g_emu{"nextendo", "emu_enabled", true};
option::OptionBool g_sys{"nextendo", "sysnand_enabled", false};
option::OptionString g_ip_main{"nextendo", "server_ip", ""};   // refreshed from the newest release
option::OptionString g_ip_nncs2{"nextendo", "server_ip_nncs2", ""};
option::OptionString g_patches_tag{"nextendo", "patches_tag", ""}; // tag of the patch files on the card
std::atomic<bool> g_restart_needed{false};
std::atomic<int> g_ssl_state{-1}; // SslState, read once per run: the firmware does not change while the Hub runs
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

// nextendo_* folders on the card under one patch root.
auto OurFolders(const char* root) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (auto& name : ListDir(("/" + std::string{root}).c_str())) {
        if (name.rfind(kPatchPrefix, 0) == 0) {
            out.push_back(std::move(name));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

auto IsInPatchRoot(std::string_view path) -> bool {
    for (const auto* root : PATCH_ROOTS) {
        if (path.rfind(root, 0) == 0) {
            return true;
        }
    }
    return false;
}

auto FileSizeIs(const std::string& path, unsigned size) -> bool {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && static_cast<unsigned>(st.st_size) == size;
}

auto PatchesPresent() -> bool {
    for (const auto* root : PATCH_ROOTS) {
        if (!OurFolders(root).empty()) {
            return true;
        }
    }
    for (const auto& f : kPatchFiles) {
        if (!IsInPatchRoot(f.path) && fs::FileExists(("/" + std::string{f.path}).c_str())) {
            return true; // browser certs
        }
    }
    return false;
}

// The files of this Hub, nothing more in the nextendo_* folders, written for this tag.
auto PatchesCurrent() -> bool {
    if (g_patches_tag.Get() != kPinnedTag) {
        return false;
    }
    for (const auto* root : PATCH_ROOTS) {
        if (OurFolders(root) != ShippedFolders(root)) {
            return false;
        }
    }
    for (const auto& f : kPatchFiles) {
        if (!FileSizeIs("/" + std::string{f.path}, f.size)) {
            return false;
        }
    }
    return true;
}

auto RemovePatches() -> Result {
    for (const auto* root : PATCH_ROOTS) {
        for (const auto& name : OurFolders(root)) {
            R_TRY(DeletePath("/" + std::string{root} + name));
        }
    }
    std::vector<std::string> dirs;
    for (const auto& f : kPatchFiles) {
        if (IsInPatchRoot(f.path)) {
            continue;
        }
        const auto path = "/" + std::string{f.path};
        R_TRY(DeletePath(path));
        for (auto d = ParentPath(path); d.size() > std::strlen("/atmosphere/contents/"); d = ParentPath(d)) {
            dirs.push_back(d);
        }
    }
    // the browser cert folders, deepest first; rmdir keeps a folder that still holds a file of someone else
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    for (const auto& d : dirs) {
        rmdir(d.c_str());
    }
    g_patches_tag.Set("");
    R_SUCCEED();
}

auto InstallPatches() -> Result {
    R_TRY(RemovePatches()); // also drops nextendo_* folders that a newer Prelude no longer has
    for (const auto& f : kPatchFiles) {
        R_TRY(CopyFileSimple(ROMFS_SD + std::string{f.path}, "/" + std::string{f.path}));
    }
    g_patches_tag.Set(kPinnedTag);
    log_write("[nextendo] patch files of %s written (%zu)\n", kPinnedTag, std::size(kPatchFiles));
    R_SUCCEED();
}

enum class SslState { Unknown, Patched, NotPatched };

// Build IDs of the modules of the running ssl process (pm:dmnt + ldr:dmnt). Empty when either call fails.
auto SslModules() -> std::vector<BuildId> {
    std::vector<BuildId> out;
    if (R_FAILED(pmdmntInitialize())) {
        return out;
    }
    u64 pid{};
    const auto rc = pmdmntGetProcessId(&pid, SSL_PROGRAM_ID);
    pmdmntExit();
    if (R_FAILED(rc) || R_FAILED(ldrDmntInitialize())) {
        return out;
    }
    ON_SCOPE_EXIT(ldrDmntExit());
    LoaderModuleInfo mods[8]{};
    s32 count{};
    if (R_SUCCEEDED(ldrDmntGetProcessModuleInfo(pid, mods, std::size(mods), &count))) {
        for (s32 i = 0; i < count; i++) {
            BuildId id;
            std::memcpy(id.data(), mods[i].build_id, id.size());
            out.push_back(id);
        }
    }
    return out;
}

auto ReadSslState() -> SslState {
    const auto mods = SslModules();
    if (mods.empty()) {
        log_write("[nextendo] ssl build id not readable; the firmware check is skipped\n");
        return SslState::Unknown;
    }
    const bool ok = SslPatched(mods);
    log_write("[nextendo] ssl %s: %s\n", BuildIdHex(mods.front()).c_str(), ok ? "patch found" : "no patch in this Hub");
    return ok ? SslState::Patched : SslState::NotPatched;
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
    auto state = g_ssl_state.load();
    if (state < 0) {
        state = static_cast<int>(ReadSslState());
        g_ssl_state = state;
    }
    // Unknown keeps Nextendo on: without the patch the games only fail to connect, Nintendo is never reached
    return static_cast<SslState>(state) != SslState::NotPatched;
}

auto SslBuildIdText() -> std::string {
    const auto mods = SslModules();
    return mods.empty() ? std::string{} : BuildIdHex(mods.front());
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
    in.patches_current = in.patches_present && PatchesCurrent();
    in.stash_present = fs::DirExists(STASH);

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
            case Step::InstallPatches: R_TRY(InstallPatches()); changed = true; break;
            case Step::RemovePatches: R_TRY(RemovePatches()); changed = true; break;
            case Step::DropStash: R_TRY(DeletePath(STASH)); break;
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
