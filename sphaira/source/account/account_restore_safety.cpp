#include "account/account_restore.hpp"
#include "account_restore_internal.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace sphaira::account_restore {

auto RemoveLegacyUnpackedSnapshot(fs::FsNativeSd& sd) -> void {
    if (sd.DirExists(LegacySnapshotDir())) {
        sd.DeleteDirectoryRecursively(LegacySnapshotDir());
        log_write("[RESTORE] removed legacy unpacked snapshot dir\n");
    }
    sd.DeleteFile((std::string(PendingDir()) + "/files.txt").c_str());
}

namespace {

constexpr u64 ACCOUNT_SAVE_ID = 0x8000000000000010ULL;
constexpr u64 ACCOUNT_EXTRA_SAVE_ID = 0x8000000000000011ULL;
constexpr u64 PLAYTIME_SAVE_ID = 0x80000000000000F0ULL;

auto CopyRawBisSave(ui::ProgressBox* pbox, fs::FsNativeBis& bis, fs::FsNativeSd& sd,
    u64 id, const char* dest) -> bool {
    char src[64]{};
    std::snprintf(src, sizeof(src), "/save/%016llX", static_cast<unsigned long long>(id));
    if (!bis.FileExists(src)) {
        log_write("[RESTORE] raw %016llX missing on BIS\n", static_cast<unsigned long long>(id));
        return false;
    }

    fs::File f_src;
    s64 src_size = 0;
    if (R_FAILED(bis.OpenFile(src, FsOpenMode_Read, &f_src)) || R_FAILED(f_src.GetSize(&src_size)) || src_size < 0x200) {
        log_write("[RESTORE] raw %016llX src open/size failed (%lld)\n",
            static_cast<unsigned long long>(id), static_cast<long long>(src_size));
        f_src.Close();
        return false;
    }
    f_src.Close();

    if (sd.FileExists(dest)) {
        sd.DeleteFile(dest);
    }
    Result rc = 0;
    if (pbox) {
        char label[32]{};
        std::snprintf(label, sizeof(label), "%016llX", static_cast<unsigned long long>(id));
        pbox->NewTransfer(label);
        rc = pbox->CopyFile(&bis, &sd, src, dest);
    } else {
        std::vector<u8> data;
        rc = bis.read_entire_file(src, data);
        if (R_SUCCEEDED(rc) && !data.empty()) {
            rc = sd.write_entire_file(dest, data);
        } else if (R_SUCCEEDED(rc)) {
            rc = Result_FsEmpty;
        }
    }
    if (R_FAILED(rc)) {
        log_write("[RESTORE] raw %016llX copy failed 0x%X\n",
            static_cast<unsigned long long>(id), rc);
        sd.DeleteFile(dest);
        return false;
    }

    fs::File f_dst;
    s64 dst_size = 0;
    if (R_FAILED(sd.OpenFile(dest, FsOpenMode_Read, &f_dst)) || R_FAILED(f_dst.GetSize(&dst_size))) {
        log_write("[RESTORE] raw %016llX dst open failed\n", static_cast<unsigned long long>(id));
        sd.DeleteFile(dest);
        return false;
    }
    f_dst.Close();

    if (dst_size < 0x200 || dst_size != src_size) {
        log_write("[RESTORE] raw %016llX size mismatch src=%lld dst=%lld\n",
            static_cast<unsigned long long>(id), static_cast<long long>(src_size), static_cast<long long>(dst_size));
        sd.DeleteFile(dest);
        return false;
    }
    log_write("[RESTORE] raw %016llX snapshot bytes=%lld (verified src size)\n",
        static_cast<unsigned long long>(id), static_cast<long long>(dst_size));
    return true;
}

} // namespace

auto FindMatchingSafetyDir(const std::string& target_pack, std::string& out_dir) -> bool {
    out_dir.clear();
    if (target_pack.empty()) {
        return false;
    }
    fs::FsNativeSd sd;
    if (!sd.DirExists(SafetyBackupRootDir())) {
        return false;
    }
    fs::Dir d;
    if (R_FAILED(sd.OpenDirectory(SafetyBackupRootDir(), FsDirOpenMode_ReadDirs, &d))) {
        return false;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(d.ReadAll(entries))) {
        return false;
    }
    const std::string expected_nand = App::IsEmummc() ? "emu" : "sys";
    for (const auto& e : entries) {
        if (e.type != FsDirEntryType_Dir) continue;
        const std::string dir_path = std::string(SafetyBackupRootDir()) + "/" + e.name;
        if (!sd.FileExists(SafetySnapshotOkPath(dir_path).c_str())) continue;

        std::vector<u8> nand_bytes;
        if (R_FAILED(sd.read_entire_file(SafetyNandFlagPath(dir_path).c_str(), nand_bytes)) || nand_bytes.empty()) continue;
        const std::string s_nand(nand_bytes.begin(), nand_bytes.end());
        if (s_nand != expected_nand) continue;

        std::vector<u8> pack_bytes;
        if (R_FAILED(sd.read_entire_file(SafetyTargetPackPath(dir_path).c_str(), pack_bytes)) || pack_bytes.empty()) continue;
        const std::string s_pack(pack_bytes.begin(), pack_bytes.end());
        // Must match exact target pack path (never basename only)
        if (s_pack != target_pack) continue;

        if (!sd.FileExists(SafetySnapshotPath(dir_path).c_str())) continue;
        fs::File f;
        s64 sz = 0;
        if (R_FAILED(sd.OpenFile(SafetySnapshotPath(dir_path).c_str(), FsOpenMode_Read, &f)) || R_FAILED(f.GetSize(&sz)) || sz < 0x200) {
            continue;
        }

        out_dir = dir_path;
        return true;
    }
    return false;
}

auto GetOrCreateSafetyDir(const std::string& target_pack) -> std::string {
    std::string existing;
    if (FindMatchingSafetyDir(target_pack, existing)) {
        return existing;
    }
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    return std::string(SafetyBackupRootDir()) + "/" + stamp;
}

auto IsSafetyBackupComplete(const std::string& safety_dir, const std::string& target_pack, bool restore_play_hours) -> bool {
    if (safety_dir.empty()) {
        return false;
    }
    fs::FsNativeSd sd;
    if (!sd.FileExists(SafetySnapshotOkPath(safety_dir).c_str())) {
        return false;
    }

    std::vector<u8> nand_bytes;
    if (R_FAILED(sd.read_entire_file(SafetyNandFlagPath(safety_dir).c_str(), nand_bytes)) || nand_bytes.empty()) {
        return false;
    }
    const std::string expected_nand = App::IsEmummc() ? "emu" : "sys";
    const std::string s_nand(nand_bytes.begin(), nand_bytes.end());
    if (s_nand != expected_nand) {
        return false;
    }

    if (!target_pack.empty()) {
        std::vector<u8> pack_bytes;
        if (R_FAILED(sd.read_entire_file(SafetyTargetPackPath(safety_dir).c_str(), pack_bytes)) || pack_bytes.empty()) {
            return false;
        }
        const std::string s_pack(pack_bytes.begin(), pack_bytes.end());
        // Must match exact target pack path (never basename only)
        if (s_pack != target_pack) {
            return false;
        }
    }

    // 0010 is mandatory for all profile restores
    if (!sd.FileExists(SafetySnapshotPath(safety_dir).c_str())) {
        return false;
    }
    {
        fs::File f;
        s64 sz = 0;
        if (R_FAILED(sd.OpenFile(SafetySnapshotPath(safety_dir).c_str(), FsOpenMode_Read, &f)) || R_FAILED(f.GetSize(&sz)) || sz < 0x200) {
            return false;
        }
    }

    // 0011 if present in safety dir must be sane
    if (sd.FileExists(SafetySnapshot0011Path(safety_dir).c_str())) {
        fs::File f;
        s64 sz = 0;
        if (R_FAILED(sd.OpenFile(SafetySnapshot0011Path(safety_dir).c_str(), FsOpenMode_Read, &f)) || R_FAILED(f.GetSize(&sz)) || sz < 0x200) {
            return false;
        }
    }

    // 00F0 mandatory if restore_play_hours requested
    if (restore_play_hours) {
        if (!sd.FileExists(SafetySnapshot00F0Path(safety_dir).c_str())) {
            return false;
        }
        fs::File f;
        s64 sz = 0;
        if (R_FAILED(sd.OpenFile(SafetySnapshot00F0Path(safety_dir).c_str(), FsOpenMode_Read, &f)) || R_FAILED(f.GetSize(&sz)) || sz < 0x200) {
            return false;
        }
    }

    return true;
}

auto Dump0010ReadOnly(ui::ProgressBox* pbox) -> Result {
    fs::FsNativeBis bis{FsBisPartitionId_System};
    const auto bis_rc = bis.GetFsOpenResult();
    if (R_FAILED(bis_rc)) {
        log_write("[RESTORE] SYSTEM BIS open 0x%X\n", bis_rc);
        return bis_rc;
    }

    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(PendingDir()));
    RemoveLegacyUnpackedSnapshot(sd);
    R_TRY(WriteNandFlag());

    if (!CopyRawBisSave(pbox, bis, sd, ACCOUNT_SAVE_ID, SnapshotPath())) {
        return Result_FsEmpty;
    }
    log_write("[RESTORE] raw 0010 snapshot nand=%s\n", App::IsEmummc() ? "emu" : "sys");
    R_SUCCEED();
}

auto TrySnapshotRawSystemSaves(ui::ProgressBox* pbox, RawSnapshotReport& out, const std::string& target_pack, bool restore_play_hours) -> bool {
    out = {};
    fs::FsNativeSd sd;

    const std::string safety_dir = GetOrCreateSafetyDir(target_pack);
    out.safety_dir = safety_dir;

    sd.CreateDirectoryRecursively(PendingDir());
    sd.write_entire_file(SafetyDirRecordPath(), std::vector<u8>(safety_dir.begin(), safety_dir.end()));

    if (IsSafetyBackupComplete(safety_dir, target_pack, restore_play_hours)) {
        out.save_0010 = true;
        out.save_0011 = sd.FileExists(SafetySnapshot0011Path(safety_dir).c_str());
        out.save_00F0 = !restore_play_hours || sd.FileExists(SafetySnapshot00F0Path(safety_dir).c_str());
        out.complete = true;
        log_write("[RESTORE] Reusing verified safety snapshot in %s for %s\n",
            safety_dir.c_str(), target_pack.c_str());
        return true;
    }

    fs::FsNativeBis bis{FsBisPartitionId_System};
    const auto bis_rc = bis.GetFsOpenResult();
    if (R_FAILED(bis_rc)) {
        log_write("[RESTORE] TrySnapshot SYSTEM BIS open 0x%X\n", bis_rc);
        return false;
    }

    sd.CreateDirectoryRecursively(safety_dir.c_str());
    sd.DeleteFile(SafetySnapshotOkPath(safety_dir).c_str());

    RemoveLegacyUnpackedSnapshot(sd);

    out.save_0010 = CopyRawBisSave(pbox, bis, sd, ACCOUNT_SAVE_ID, SafetySnapshotPath(safety_dir).c_str());
    if (out.save_0010) {
        CopyRawBisSave(nullptr, bis, sd, ACCOUNT_SAVE_ID, SnapshotPath());
    } else {
        log_write("[RESTORE] snapshot of 0010 failed!\n");
        return false;
    }

    char src0011[64]{};
    std::snprintf(src0011, sizeof(src0011), "/save/%016llX", static_cast<unsigned long long>(ACCOUNT_EXTRA_SAVE_ID));
    if (bis.FileExists(src0011)) {
        out.save_0011 = CopyRawBisSave(pbox, bis, sd, ACCOUNT_EXTRA_SAVE_ID, SafetySnapshot0011Path(safety_dir).c_str());
        if (!out.save_0011) {
            log_write("[RESTORE] snapshot of existing 0011 failed!\n");
            return false;
        }
    }

    if (restore_play_hours) {
        char src00F0[64]{};
        std::snprintf(src00F0, sizeof(src00F0), "/save/%016llX", static_cast<unsigned long long>(PLAYTIME_SAVE_ID));
        if (bis.FileExists(src00F0)) {
            out.save_00F0 = CopyRawBisSave(pbox, bis, sd, PLAYTIME_SAVE_ID, SafetySnapshot00F0Path(safety_dir).c_str());
            if (out.save_00F0) {
                CopyRawBisSave(nullptr, bis, sd, PLAYTIME_SAVE_ID, Snapshot00F0Path());
            } else {
                log_write("[RESTORE] snapshot of existing 00F0 failed!\n");
                return false;
            }
        }
    }

    const char* tag = App::IsEmummc() ? "emu" : "sys";
    sd.write_entire_file(SafetyNandFlagPath(safety_dir).c_str(), std::vector<u8>(tag, tag + std::strlen(tag)));
    sd.write_entire_file(SafetyTargetPackPath(safety_dir).c_str(), std::vector<u8>(target_pack.begin(), target_pack.end()));

    const char ok = '1';
    sd.write_entire_file(SafetySnapshotOkPath(safety_dir).c_str(), std::vector<u8>{static_cast<u8>(ok)});
    out.complete = true;

    log_write("[RESTORE] TrySnapshot COMPLETE 0010=%d 0011=%d 00F0=%d dir=%s pack=%s\n",
        out.save_0010 ? 1 : 0, out.save_0011 ? 1 : 0, out.save_00F0 ? 1 : 0,
        safety_dir.c_str(), target_pack.c_str());
    return true;
}

} // namespace sphaira::account_restore
