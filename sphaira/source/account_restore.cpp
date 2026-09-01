#include "account_restore.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"
#include "utils/utils.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::account_restore {
namespace {

constexpr u64 ACCOUNT_SAVE_ID = 0x8000000000000010ULL;

auto ReadJsonField(const std::string& json, const char* key) -> std::string {
    const auto needle = std::string{"\""} + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) {
        const auto needle_raw = std::string{"\""} + key + "\":";
        pos = json.find(needle_raw);
        if (pos == std::string::npos) {
            return {};
        }
        auto i = pos + needle_raw.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
            i++;
        }
        if (i < json.size() && (json[i] == 't' || json[i] == 'f')) {
            return json.substr(i, json[i] == 't' ? 4 : 5);
        }
        return {};
    }
    auto i = pos + needle.size();
    std::string out;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            i++;
        }
        out += json[i++];
    }
    return out;
}

auto ReadRomfsTe(const char* path, std::vector<u8>& te) -> bool {
    if (R_FAILED(romfsInit())) {
        log_write("[RESTORE] romfsInit failed for %s\n", path);
        return false;
    }
    ON_SCOPE_EXIT(romfsExit());
    return R_SUCCEEDED(fs::read_entire_file(path, te)) && !te.empty();
}

auto CopyTe(fs::FsNativeSd& sd, const char* romfs_name, const char* dest_name) -> void {
    std::vector<u8> te;
    const auto romfs = std::string("romfs:/tegra/") + romfs_name;
    if (!ReadRomfsTe(romfs.c_str(), te)) {
        log_write("[RESTORE] %s missing from romfs\n", romfs_name);
        return;
    }
    sd.CreateDirectoryRecursively("/TegraExplorer/scripts");
    sd.CreateDirectoryRecursively(PendingDir());
    sd.write_entire_file((std::string("/TegraExplorer/scripts/") + dest_name).c_str(), te);
    sd.write_entire_file((std::string(PendingDir()) + "/" + dest_name).c_str(), te);
}

auto RemoveLegacyUnpackedSnapshot(fs::FsNativeSd& sd) -> void {
    if (sd.DirExists(LegacySnapshotDir())) {
        sd.DeleteDirectoryRecursively(LegacySnapshotDir());
        log_write("[RESTORE] removed legacy unpacked snapshot dir\n");
    }
    sd.DeleteFile((std::string(PendingDir()) + "/files.txt").c_str());
}

auto SnapshotFileOk(fs::FsNativeSd& sd) -> bool {
    if (!sd.FileExists(SnapshotPath())) {
        return false;
    }
    fs::File f;
    if (R_FAILED(sd.OpenFile(SnapshotPath(), FsOpenMode_Read, &f))) {
        return false;
    }
    s64 size = 0;
    if (R_FAILED(f.GetSize(&size)) || size < 0x200) {
        return false;
    }
    return true;
}

// Size from directory entry when OpenFile/FileExists fails for TE-written blobs.
auto SnapshotSizeFromDir(fs::FsNativeSd& sd, s64* out_size, std::string* listing) -> bool {
    if (out_size) {
        *out_size = -1;
    }
    if (!sd.DirExists(PendingDir())) {
        if (listing) {
            *listing = "(pending dir missing)";
        }
        return false;
    }
    fs::Dir d;
    if (R_FAILED(sd.OpenDirectory(PendingDir(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) {
        if (listing) {
            *listing = "(opendir failed)";
        }
        return false;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(d.ReadAll(entries))) {
        if (listing) {
            *listing = "(readdir failed)";
        }
        return false;
    }
    bool found = false;
    s64 found_size = -1;
    if (listing) {
        listing->clear();
    }
    for (const auto& e : entries) {
        if (listing) {
            if (!listing->empty()) {
                *listing += ", ";
            }
            *listing += e.name;
            if (e.type == FsDirEntryType_File) {
                *listing += ":" + std::to_string(static_cast<long long>(e.file_size));
            } else {
                *listing += "/";
            }
        }
        if (e.type == FsDirEntryType_File && !std::strcmp(e.name, SnapshotFileName()) && e.file_size >= 0x200) {
            found = true;
            found_size = e.file_size;
        }
    }
    if (listing && listing->empty()) {
        *listing = "(empty)";
    }
    if (found && out_size) {
        *out_size = found_size;
    }
    return found;
}

} // namespace

auto SnapshotOk() -> bool {
    fs::FsNativeSd sd;
    if (SnapshotFileOk(sd)) {
        return true;
    }

    const bool dumped_ok = sd.FileExists(DumpedOkPath());
    const bool path_exists = sd.FileExists(SnapshotPath());
    s64 open_size = -1;
    Result open_rc = 0xFFFFFFFF;
    Result size_rc = 0xFFFFFFFF;
    if (path_exists) {
        fs::File f;
        open_rc = sd.OpenFile(SnapshotPath(), FsOpenMode_Read, &f);
        if (R_SUCCEEDED(open_rc)) {
            size_rc = f.GetSize(&open_size);
        }
    }

    FsTimeStampRaw ts{};
    s64 st_size = -1;
    const auto st_rc = sd.FileGetSizeAndTimestamp(SnapshotPath(), &ts, &st_size);
    if (R_SUCCEEDED(st_rc) && st_size >= 0x200) {
        log_write("[RESTORE] SnapshotOk via stat size=%lld dumped.ok=%d exists=%d\n",
            static_cast<long long>(st_size), dumped_ok ? 1 : 0, path_exists ? 1 : 0);
        return true;
    }

    s64 listed_size = -1;
    std::string listing;
    const bool listed_ok = SnapshotSizeFromDir(sd, &listed_size, &listing);
    if (listed_ok) {
        log_write("[RESTORE] SnapshotOk via dir listing size=%lld dumped.ok=%d exists=%d open=0x%X getsize=0x%X/%lld stat=0x%X/%lld dir=[%s]\n",
            static_cast<long long>(listed_size),
            dumped_ok ? 1 : 0,
            path_exists ? 1 : 0,
            open_rc,
            size_rc, static_cast<long long>(open_size),
            st_rc, static_cast<long long>(st_size),
            listing.c_str());
        return true;
    }

    // TE success marker + any size probe that reached >= 0x200.
    if (dumped_ok && ((R_SUCCEEDED(st_rc) && st_size >= 0x200) ||
                      (R_SUCCEEDED(size_rc) && open_size >= 0x200))) {
        log_write("[RESTORE] SnapshotOk via dumped.ok + size probe\n");
        return true;
    }

    log_write("[RESTORE] SnapshotOk FAILED dumped.ok=%d exists=%d open=0x%X getsize=0x%X/%lld stat=0x%X/%lld listed=%d/%lld dir=[%s]\n",
        dumped_ok ? 1 : 0,
        path_exists ? 1 : 0,
        open_rc,
        size_rc, static_cast<long long>(open_size),
        st_rc, static_cast<long long>(st_size),
        listed_ok ? 1 : 0, static_cast<long long>(listed_size),
        listing.c_str());
    return false;
}

auto WriteNandFlag() -> Result {
    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(PendingDir()));
    RemoveLegacyUnpackedSnapshot(sd);
    const char* tag = App::IsEmummc() ? "emu" : "sys";
    const std::vector<u8> bytes(tag, tag + std::strlen(tag));
    R_TRY(sd.write_entire_file(NandFlagPath(), bytes));
    log_write("[RESTORE] nand flag=%s\n", tag);
    R_SUCCEED();
}

auto LoadPending() -> Pending {
    Pending p;
    fs::FsNativeSd sd;
    if (sd.FileExists(RolledBackPath())) {
        p.present = true;
        p.rolled_back = true;
        return p;
    }
    if (!sd.FileExists(StatePath())) {
        return p;
    }
    std::vector<u8> raw;
    if (R_FAILED(sd.read_entire_file(StatePath(), raw)) || raw.empty()) {
        return p;
    }
    const std::string s(raw.begin(), raw.end());
    p.present = true;
    p.phase = ReadJsonField(s, "phase");
    const auto has = ReadJsonField(s, "has_0010");
    // Keep on-disk phase as-is; OfferPendingRestore owns wait_dump→ready + auto-continue.
    p.snapshot_ok = (has == "true") || SnapshotOk();
    const auto packs = ReadJsonField(s, "packs");
    std::string cur;
    for (char c : packs) {
        if (c == '|') {
            if (!cur.empty()) {
                p.pack_dirs.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        p.pack_dirs.push_back(cur);
    }
    return p;
}

auto HasUnfinishedRestore() -> bool {
    const auto p = LoadPending();
    if (p.rolled_back) {
        return true;
    }
    return p.present && p.phase != "applied";
}

auto SavePending(const std::vector<std::string>& pack_dirs, const std::string& phase, bool snapshot_ok) -> Result {
    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(PendingDir()));
    std::string packs;
    for (size_t i = 0; i < pack_dirs.size(); i++) {
        if (i) {
            packs += '|';
        }
        packs += pack_dirs[i];
    }
    const auto json = std::string{"{\"kind\":\"kefir-restore-pending\",\"phase\":\""} + phase +
        "\",\"has_0010\":" + (snapshot_ok ? "true" : "false") +
        ",\"packs\":\"" + packs + "\"}";
    R_TRY(sd.write_entire_file(StatePath(), std::vector<u8>(json.begin(), json.end())));
    log_write("[RESTORE] pending phase=%s snapshot=%d packs=%zu\n",
        phase.c_str(), snapshot_ok ? 1 : 0, pack_dirs.size());
    R_SUCCEED();
}

auto ClearPending() -> Result {
    fs::FsNativeSd sd;
    if (sd.DirExists(PendingDir())) {
        sd.DeleteDirectoryRecursively(PendingDir());
    }
    sd.DeleteFile((std::string("/TegraExplorer/scripts/") + DumpTeName()).c_str());
    sd.DeleteFile((std::string("/TegraExplorer/scripts/") + RollbackTeName()).c_str());
    sd.DeleteFile("/TegraExplorer/scripts/account_0010_rollback.te");
    R_SUCCEED();
}

auto Dump0010ReadOnly(ui::ProgressBox* pbox) -> Result {
    fs::FsNativeBis bis{FsBisPartitionId_System};
    const auto bis_rc = bis.GetFsOpenResult();
    if (R_FAILED(bis_rc)) {
        log_write("[RESTORE] SYSTEM BIS open 0x%X\n", bis_rc);
        return bis_rc;
    }

    char src[64]{};
    std::snprintf(src, sizeof(src), "/save/%016llX", static_cast<unsigned long long>(ACCOUNT_SAVE_ID));
    if (!bis.FileExists(src)) {
        log_write("[RESTORE] raw 0010 missing on BIS (%s)\n", src);
        return Result_FsInvalidType;
    }

    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(PendingDir()));
    RemoveLegacyUnpackedSnapshot(sd);

    if (sd.FileExists(SnapshotPath())) {
        sd.DeleteFile(SnapshotPath());
    }

    R_TRY(WriteNandFlag());

    if (pbox) {
        pbox->NewTransfer("8000000000000010");
        R_TRY(pbox->CopyFile(&bis, &sd, src, SnapshotPath()));
    } else {
        std::vector<u8> data;
        R_TRY(bis.read_entire_file(src, data));
        R_UNLESS(!data.empty(), Result_FsEmpty);
        R_TRY(sd.write_entire_file(SnapshotPath(), data));
    }

    R_UNLESS(SnapshotFileOk(sd), Result_FsEmpty);
    fs::File f;
    s64 size = 0;
    if (R_SUCCEEDED(sd.OpenFile(SnapshotPath(), FsOpenMode_Read, &f))) {
        f.GetSize(&size);
    }
    log_write("[RESTORE] raw 0010 snapshot bytes=%lld nand=%s\n",
        static_cast<long long>(size), App::IsEmummc() ? "emu" : "sys");
    R_SUCCEED();
}

auto InstallRestoreTeScripts() -> void {
    fs::FsNativeSd sd;
    CopyTe(sd, RollbackTeName(), RollbackTeName());
    sd.DeleteFile((std::string("/TegraExplorer/scripts/") + DumpTeName()).c_str());
    sd.DeleteFile("/TegraExplorer/scripts/account_0010_rollback.te");
    sd.DeleteFile((std::string(PendingDir()) + "/" + DumpTeName()).c_str());
    sd.DeleteFile((std::string(PendingDir()) + "/account_0010_rollback.te").c_str());
}

auto LaunchTegraRomfs(const char* romfs_name) -> bool {
    fs::FsPath te_bin;
    if (!utils::findTegraExplorerPayload(te_bin)) {
        log_write("[RESTORE] TegraExplorer payload not found in /bootloader/payloads\n");
        return false;
    }

    fs::FsNativeSd sd;
    std::vector<u8> te;
    const auto romfs = std::string("romfs:/tegra/") + romfs_name;
    if (!ReadRomfsTe(romfs.c_str(), te)) {
        log_write("[RESTORE] %s missing from romfs\n", romfs_name);
        return false;
    }
    sd.DeleteFile("/startup.te");
    if (R_FAILED(sd.write_entire_file("/startup.te", te))) {
        log_write("[RESTORE] write /startup.te failed\n");
        return false;
    }
    fsdevCommitDevice("sdmc");
    log_write("[RESTORE] wrote /startup.te from %s, launching %s\n",
        romfs_name, static_cast<const char*>(te_bin));

    if (utils::rebootToPayload(static_cast<const char*>(te_bin))) {
        return true;
    }
    log_write("[RESTORE] rebootToPayload failed, hekate autoboot fallback\n");
    if (!utils::setHekateAutobootPayload(static_cast<const char*>(te_bin))) {
        log_write("[RESTORE] setHekateAutobootPayload failed\n");
        return false;
    }
    if (R_FAILED(utils::requestForcedReboot())) {
        log_write("[RESTORE] requestForcedReboot failed after autoboot\n");
        return false;
    }
    return true;
}

auto LaunchTegraDump() -> bool {
    return LaunchTegraRomfs("account_0010_dump.te");
}

} // namespace sphaira::account_restore
