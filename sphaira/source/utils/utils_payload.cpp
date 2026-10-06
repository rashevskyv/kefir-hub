#include "utils/utils.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"

#include <cstring>
#include <cstdio>
#include <switch.h>
#include <vector>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <compare>

namespace sphaira::utils {

// Hekate IPL ini manipulation
namespace {
    constexpr const char* HEKATE_INI_PATH = "/bootloader/hekate_ipl.ini";
    constexpr const char* HEKATE_INI_BAK_PATH = "/bootloader/hekate_ipl.ini.bak";
    constexpr const char* LOCKPICK_PAYLOAD_DIR = "/bootloader/payloads";

    bool backupHekateIni() {
        FILE* f_bak_check = fopen(HEKATE_INI_BAK_PATH, "rb");
        if (f_bak_check) {
            fseek(f_bak_check, 0, SEEK_END);
            const long bak_size = ftell(f_bak_check);
            fclose(f_bak_check);
            if (bak_size > 0) {
                log_write("backupHekateIni: backup already exists (%ld bytes), preserving it\n", bak_size);
                return true;
            }
        }

        FILE* f_ini = fopen(HEKATE_INI_PATH, "rb");
        if (!f_ini) {
            log_write("backupHekateIni: original hekate_ipl.ini not found\n");
            return true;
        }

        fseek(f_ini, 0, SEEK_END);
        const long size = ftell(f_ini);
        fseek(f_ini, 0, SEEK_SET);

        if (size <= 0) {
            fclose(f_ini);
            return true;
        }

        std::vector<u8> buffer(size);
        fread(buffer.data(), 1, size, f_ini);
        fclose(f_ini);

        FILE* f_bak = fopen(HEKATE_INI_BAK_PATH, "wb");
        if (!f_bak) {
            log_write("backupHekateIni: failed to create backup\n");
            return false;
        }

        fwrite(buffer.data(), 1, size, f_bak);
        fflush(f_bak);
        fsdevCommitDevice("sdmc");
        fclose(f_bak);
        log_write("backupHekateIni: created backup (%ld bytes)\n", size);
        return true;
    }

    bool writeHekateAutobootIni(const char* payload_path) {
        if (!payload_path || !*payload_path) {
            log_write("writeHekateAutobootIni: invalid empty payload path\n");
            return false;
        }

        std::string_view p{payload_path};
        if (p.starts_with("sdmc:/")) p.remove_prefix(6);
        else if (p.starts_with("sdmc:")) p.remove_prefix(5);
        else if (p.starts_with("sd:/")) p.remove_prefix(4);
        else if (p.starts_with("sd:")) p.remove_prefix(3);
        while (!p.empty() && p.front() == '/') {
            p.remove_prefix(1);
        }
        std::string rel_p{p};

        FILE* f_out = fopen(HEKATE_INI_PATH, "wb");
        if (!f_out) {
            log_write("writeHekateAutobootIni: failed to open %s for writing\n", HEKATE_INI_PATH);
            return false;
        }

        const int written = std::fprintf(
            f_out,
            "[config]\n"
            "autoboot=1\n"
            "autoboot_list=0\n"
            "bootwait=0\n"
            "verification=1\n"
            "backlight=100\n"
            "autohosoff=2\n"
            "autonogc=1\n"
            "updater2p=1\n"
            "\n"
            "[HATS Payload]\n"
            "payload=%s\n",
            rel_p.c_str()
        );

        fclose(f_out);
        fsdevCommitDevice("sdmc");
        return written > 0;
    }

    std::string toLower(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool isLockpickPayloadName(const std::string& name) {
        const auto lower = toLower(name);
        return lower == "lockpick_rcm_pro.bin" || lower == "lockpick_rcm.bin";
    }

    bool findLockpickPayloadInDir(const char* dir_path, fs::FsPath& out) {
        fs::FsNativeSd fs;
        fs::Dir dir;
        if (R_FAILED(fs.OpenDirectory(dir_path, FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &dir))) {
            return false;
        }

        std::vector<FsDirectoryEntry> entries;
        if (R_FAILED(dir.ReadAll(entries))) {
            return false;
        }

        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return toLower(a.name) < toLower(b.name);
        });

        for (const auto& entry : entries) {
            const auto lower = toLower(entry.name);
            if (lower == "lockpick_rcm_pro.bin") {
                out = fs::AppendPath(dir_path, entry.name);
                return true;
            }
        }

        for (const auto& entry : entries) {
            if (isLockpickPayloadName(entry.name)) {
                out = fs::AppendPath(dir_path, entry.name);
                return true;
            }
        }

        return false;
    }
}

// Set hekate_ipl.ini to auto-boot a payload through hekate.
// Backs up the original hekate_ipl.ini to .bak, then writes a temporary
// autoboot entry that points to payload_path.
bool setHekateAutobootPayload(const char* payload_path) {
    log_write("setHekateAutobootPayload: setting up autoboot for %s\n", payload_path);

    if (!backupHekateIni()) {
        log_write("setHekateAutobootPayload: failed to backup hekate ini\n");
        return false;
    }

    if (!writeHekateAutobootIni(payload_path)) {
        log_write("setHekateAutobootPayload: failed to write autoboot ini\n");
        return false;
    }

    fsdevCommitDevice("sdmc");
    log_write("setHekateAutobootPayload: hekate_ipl.ini updated successfully\n");
    return true;
}

bool findLockpickPayload(fs::FsPath& out) {
    return findLockpickPayloadInDir(LOCKPICK_PAYLOAD_DIR, out);
}

namespace {

bool findExistingTegraExplorerPayloadOnSd(fs::FsPath& out) {
    fs::FsNativeSd fs;
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(LOCKPICK_PAYLOAD_DIR, FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &dir))) {
        log_write("findExistingTegraExplorerPayloadOnSd: cannot open %s\n", LOCKPICK_PAYLOAD_DIR);
        return false;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) {
        return false;
    }
    fs::FsPath exact;
    fs::FsPath any;
    for (const auto& entry : entries) {
        if (!isTegraExplorerPayload(entry.name)) {
            continue;
        }
        const auto path = fs::AppendPath(LOCKPICK_PAYLOAD_DIR, entry.name);
        const auto lower = toLower(entry.name);
        if (lower == "tegraexplorer.bin") {
            exact = path;
            break;
        }
        if (any.empty()) {
            any = path;
        }
    }
    out = !exact.empty() ? exact : any;
    return !out.empty();
}

bool safeWritePayloadFile(fs::FsNativeSd& sd, const char* target_path, const std::vector<u8>& data) {
    if (!target_path || !*target_path || data.empty()) {
        log_write("safeWritePayloadFile: invalid target path or empty data\n");
        return false;
    }

    Result rc = sd.CreateDirectoryRecursivelyWithPath(target_path);
    if (R_FAILED(rc)) {
        log_write("safeWritePayloadFile: CreateDirectoryRecursivelyWithPath failed for %s (0x%x)\n", target_path, rc);
        return false;
    }

    const std::string tmp_path = std::string(target_path) + ".kefir-te.tmp";
    const std::string backup_path = std::string(target_path) + ".kefir-te.bak";
    if (tmp_path.size() >= sizeof(fs::FsPath{}.s) || backup_path.size() >= sizeof(fs::FsPath{}.s)) {
        log_write("safeWritePayloadFile: target path too long for staging\n");
        return false;
    }
    if (sd.FileExists(tmp_path.c_str()) || sd.FileExists(backup_path.c_str())) {
        log_write("safeWritePayloadFile: unfinished replacement exists for %s\n", target_path);
        return false;
    }
    rc = sd.write_entire_file(tmp_path.c_str(), data);
    std::vector<u8> verified;
    if (R_FAILED(rc) || R_FAILED(sd.Commit()) ||
        R_FAILED(sd.read_entire_file(tmp_path.c_str(), verified)) || verified != data) {
        log_write("safeWritePayloadFile: staging or verification failed for %s\n", target_path);
        sd.DeleteFile(tmp_path.c_str());
        return false;
    }

    const bool had_target = sd.FileExists(target_path);
    if (had_target && R_FAILED(sd.RenameFile(target_path, backup_path.c_str()))) {
        log_write("safeWritePayloadFile: cannot preserve old payload %s\n", target_path);
        sd.DeleteFile(tmp_path.c_str());
        return false;
    }
    rc = sd.RenameFile(tmp_path.c_str(), target_path);
    if (R_FAILED(rc)) {
        // Keep the previous payload recoverable if replacing the file fails.
        if (had_target && !sd.FileExists(target_path) &&
            R_FAILED(sd.RenameFile(backup_path.c_str(), target_path))) {
            log_write("safeWritePayloadFile: old payload retained at %s\n", backup_path.c_str());
        }
        log_write("safeWritePayloadFile: replacement failed for %s (0x%x)\n", target_path, rc);
        sd.DeleteFile(tmp_path.c_str());
        return false;
    }
    if (R_FAILED(sd.Commit())) {
        log_write("safeWritePayloadFile: commit failed for %s; backup retained\n", target_path);
        return false;
    }
    if (had_target && R_FAILED(sd.DeleteFile(backup_path.c_str()))) {
        log_write("safeWritePayloadFile: backup cleanup failed for %s\n", target_path);
        return false;
    }

    return true;
}

} // namespace

bool ensureTegraExplorerPayload(fs::FsPath& out, const char* target_path) {
    std::vector<u8> romfs_data;
    bool has_romfs = false;
    if (R_SUCCEEDED(romfsInit())) {
        has_romfs = R_SUCCEEDED(fs::read_entire_file("romfs:/tegra/TegraExplorer.bin", romfs_data)) && !romfs_data.empty();
        romfsExit();
    }

    TegraExplorerPayloadState romfs_state{};
    if (!has_romfs) {
        romfs_state.status = PayloadMetaStatus::Missing;
    } else {
        romfs_state.status = inspectTegraExplorerPayloadData(
            romfs_data.data(), romfs_data.size(), romfs_state.version);
    }

    fs::FsNativeSd sd;
    fs::FsPath sd_path;
    const bool explicit_target = (target_path != nullptr && *target_path != '\0');
    bool has_sd_file = false;

    if (explicit_target) {
        std::string target_norm = target_path;
        if (target_norm.starts_with("sdmc:/")) {
            target_norm.erase(0, 5);
        } else if (target_norm.starts_with("sdmc:")) {
            target_norm.erase(0, 5);
        }
        if (!target_norm.starts_with("/")) {
            target_norm = "/" + target_norm;
        }
        sd_path = target_norm;
        has_sd_file = sd.FileExists(sd_path);
    } else {
        has_sd_file = findExistingTegraExplorerPayloadOnSd(sd_path);
        if (!has_sd_file) {
            sd_path = "/bootloader/payloads/TegraExplorer.bin";
        }
    }

    TegraExplorerPayloadState sd_state{};
    std::vector<u8> sd_data;
    if (!has_sd_file) {
        sd_state.status = PayloadMetaStatus::Missing;
    } else if (R_FAILED(sd.read_entire_file(sd_path, sd_data))) {
        sd_state.status = PayloadMetaStatus::Unreadable;
    } else {
        sd_state.status = inspectTegraExplorerPayloadData(
            sd_data.data(), sd_data.size(), sd_state.version);
    }

    const auto action = decideTegraExplorerPayloadAction(sd_state, romfs_state);

    switch (action) {
        case TegraExplorerSelectionAction::FailNoPayload:
            log_write("ensureTegraExplorerPayload: no usable TegraExplorer payload in romfs or on SD\n");
            return false;

        case TegraExplorerSelectionAction::UseExistingSd:
            out = sd_path;
            if (romfs_state.status != PayloadMetaStatus::Valid && romfs_state.status != PayloadMetaStatus::Unrecognized) {
                log_write("ensureTegraExplorerPayload: romfs unavailable, preserving readable SD fallback: %s\n",
                    static_cast<const char*>(out));
            } else if (romfs_state.status != PayloadMetaStatus::Valid) {
                log_write("ensureTegraExplorerPayload: romfs metadata unrecognized, preserving known SD payload (%s, v%u.%u.%u.%u)\n",
                    static_cast<const char*>(sd_path),
                    sd_state.version.major, sd_state.version.minor, sd_state.version.patch, sd_state.version.kefir);
            } else {
                log_write("ensureTegraExplorerPayload: keeping existing SD payload (%s, v%u.%u.%u.%u >= RomFS v%u.%u.%u.%u)\n",
                    static_cast<const char*>(sd_path),
                    sd_state.version.major, sd_state.version.minor, sd_state.version.patch, sd_state.version.kefir,
                    romfs_state.version.major, romfs_state.version.minor, romfs_state.version.patch, romfs_state.version.kefir);
            }
            return true;

        case TegraExplorerSelectionAction::InstallSdFromRomfs:
            log_write("ensureTegraExplorerPayload: installing RomFS TegraExplorer v%u.%u.%u.%u to %s\n",
                romfs_state.version.major, romfs_state.version.minor, romfs_state.version.patch, romfs_state.version.kefir,
                static_cast<const char*>(sd_path));
            if (!safeWritePayloadFile(sd, sd_path, romfs_data)) {
                log_write("ensureTegraExplorerPayload: failed to install RomFS payload to %s\n",
                    static_cast<const char*>(sd_path));
                return false;
            }
            out = sd_path;
            return true;

        case TegraExplorerSelectionAction::UpgradeSdFromRomfs:
            log_write("ensureTegraExplorerPayload: upgrading %s (v%u.%u.%u.%u) to RomFS v%u.%u.%u.%u\n",
                static_cast<const char*>(sd_path),
                sd_state.version.major, sd_state.version.minor, sd_state.version.patch, sd_state.version.kefir,
                romfs_state.version.major, romfs_state.version.minor, romfs_state.version.patch, romfs_state.version.kefir);
            if (!safeWritePayloadFile(sd, sd_path, romfs_data)) {
                log_write("ensureTegraExplorerPayload: failed to upgrade %s with RomFS payload\n",
                    static_cast<const char*>(sd_path));
                return false;
            }
            out = sd_path;
            return true;
    }

    return false;
}

bool findTegraExplorerPayload(fs::FsPath& out) {
    return ensureTegraExplorerPayload(out);
}

// Restore hekate_ipl.ini from backup
bool restoreHekateIni() {
    FILE* f_bak = fopen(HEKATE_INI_BAK_PATH, "rb");
    if (!f_bak) {
        log_write("restoreHekateIni: no backup found, nothing to restore\n");
        return false;
    }

    fseek(f_bak, 0, SEEK_END);
    long size = ftell(f_bak);
    fseek(f_bak, 0, SEEK_SET);

    if (size <= 0) {
        log_write("restoreHekateIni: backup is empty or invalid\n");
        fclose(f_bak);
        remove(HEKATE_INI_BAK_PATH);
        return false;
    }

    std::vector<u8> backup_data(size);
    fread(backup_data.data(), 1, size, f_bak);
    fclose(f_bak);

    FILE* f_out = fopen(HEKATE_INI_PATH, "wb");
    if (!f_out) {
        log_write("restoreHekateIni: failed to open %s for writing\n", HEKATE_INI_PATH);
        return false;
    }

    fwrite(backup_data.data(), 1, size, f_out);
    fclose(f_out);

    remove(HEKATE_INI_BAK_PATH);

    fsdevCommitDevice("sdmc");

    log_write("restoreHekateIni: hekate_ipl.ini restored from backup (%ld bytes)\n", size);
    return true;
}

} // namespace sphaira::utils
