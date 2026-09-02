#include "account/account_restore.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
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
constexpr u64 PLAYTIME_SAVE_ID = 0x80000000000000F0ULL;

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
    sd.DeleteFile((std::string("/TegraExplorer/scripts/") + ApplyLinkTeName()).c_str());
    sd.DeleteFile((std::string("/TegraExplorer/scripts/") + NandRestoreTeName()).c_str());
    sd.DeleteFile((std::string("/TegraExplorer/scripts/") + RollbackTeName()).c_str());
    sd.DeleteFile("/TegraExplorer/scripts/account_0010_rollback.te");
    R_SUCCEED();
}

auto CopyRawBisSave(ui::ProgressBox* pbox, fs::FsNativeBis& bis, fs::FsNativeSd& sd,
    u64 id, const char* dest) -> bool {
    char src[64]{};
    std::snprintf(src, sizeof(src), "/save/%016llX", static_cast<unsigned long long>(id));
    if (!bis.FileExists(src)) {
        log_write("[RESTORE] raw %016llX missing on BIS\n", static_cast<unsigned long long>(id));
        return false;
    }
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
    fs::File f;
    s64 size = 0;
    if (R_SUCCEEDED(sd.OpenFile(dest, FsOpenMode_Read, &f))) {
        f.GetSize(&size);
    }
    if (size < 0x200) {
        log_write("[RESTORE] raw %016llX too small (%lld)\n",
            static_cast<unsigned long long>(id), static_cast<long long>(size));
        sd.DeleteFile(dest);
        return false;
    }
    log_write("[RESTORE] raw %016llX snapshot bytes=%lld\n",
        static_cast<unsigned long long>(id), static_cast<long long>(size));
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

auto TrySnapshotRawSystemSaves(ui::ProgressBox* pbox, RawSnapshotReport& out) -> void {
    out = {};
    fs::FsNativeBis bis{FsBisPartitionId_System};
    const auto bis_rc = bis.GetFsOpenResult();
    if (R_FAILED(bis_rc)) {
        log_write("[RESTORE] TrySnapshot SYSTEM BIS open 0x%X (skip raw undo snaps)\n", bis_rc);
        return;
    }
    fs::FsNativeSd sd;
    if (R_FAILED(sd.CreateDirectoryRecursively(PendingDir()))) {
        return;
    }
    RemoveLegacyUnpackedSnapshot(sd);
    out.save_0010 = CopyRawBisSave(pbox, bis, sd, ACCOUNT_SAVE_ID, SnapshotPath());
    out.save_00F0 = CopyRawBisSave(pbox, bis, sd, PLAYTIME_SAVE_ID, Snapshot00F0Path());
    log_write("[RESTORE] TrySnapshot 0010=%d 00F0=%d nand=%s\n",
        out.save_0010 ? 1 : 0, out.save_00F0 ? 1 : 0, App::IsEmummc() ? "emu" : "sys");
}

auto InstallRestoreTeScripts() -> void {
    fs::FsNativeSd sd;
    CopyTe(sd, RollbackTeName(), RollbackTeName());
    CopyTe(sd, NandRestoreTeName(), NandRestoreTeName());
    sd.DeleteFile((std::string("/TegraExplorer/scripts/") + DumpTeName()).c_str());
    sd.DeleteFile("/TegraExplorer/scripts/account_0010_rollback.te");
    sd.DeleteFile((std::string(PendingDir()) + "/" + DumpTeName()).c_str());
    sd.DeleteFile((std::string(PendingDir()) + "/account_0010_rollback.te").c_str());
}

namespace {

constexpr const char* kBootPackagePath = "/switch/.packages/boot_package.ini";
constexpr const char* kNotifyJsonPath = "/config/ultrahand/notifications/kefir-reopen.notify";
constexpr const char* kHookBegin = "; kefir-hub-reopen-begin";
constexpr const char* kHookEnd = "; kefir-hub-reopen-end";

auto SanitizeNotifyText(std::string s) -> std::string {
    for (char& c : s) {
        if (c == '"' || c == '\n' || c == '\r' || c == '\\') {
            c = ' ';
        }
    }
    return s;
}

auto UpsertUltrahandBootHook(const std::string& message) -> void {
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively("/switch/.packages");

    const auto block = std::string(kHookBegin) + "\ntry:\n"
        "path_exists /config/kefir/reopen_hub.flag\n"
        "notify-now \"" + message + "\" 26 center word 0 \"Kefir Hub\" false kefir\n"
        "delete /config/kefir/reopen_hub.flag\n"
        + kHookEnd + "\n";

    std::string text;
    std::vector<u8> raw;
    if (R_SUCCEEDED(sd.read_entire_file(kBootPackagePath, raw)) && !raw.empty()) {
        text.assign(raw.begin(), raw.end());
    }

    const auto begin = text.find(kHookBegin);
    const auto end = text.find(kHookEnd);
    if (begin != std::string::npos && end != std::string::npos && end > begin) {
        auto after = end + std::strlen(kHookEnd);
        while (after < text.size() && (text[after] == '\n' || text[after] == '\r')) {
            after++;
        }
        text.replace(begin, after - begin, block);
    } else {
        auto on_boot = text.find("[on-boot]");
        if (on_boot == std::string::npos) {
            if (!text.empty() && text.back() != '\n') {
                text += '\n';
            }
            text += "[on-boot]\n";
            text += block;
        } else {
            auto nl = text.find('\n', on_boot);
            if (nl == std::string::npos) {
                text += '\n';
                text += block;
            } else {
                text.insert(nl + 1, block);
            }
        }
    }

    sd.write_entire_file(kBootPackagePath, std::vector<u8>(text.begin(), text.end()));
}

} // namespace

auto ArmReopenHubHint() -> void {
    const auto msg = SanitizeNotifyText("Open Kefir Hub to finish."_i18n);
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively("/config/kefir");
    sd.CreateDirectoryRecursively("/config/ultrahand/notifications");
    const std::vector<u8> flag(msg.begin(), msg.end());
    sd.write_entire_file(ReopenHubFlagPath(), flag);

    const auto json = std::string{"{\"text\":\""} + msg +
        "\",\"duration\":0,\"title\":\"Kefir Hub\",\"priority\":20}\n";
    sd.write_entire_file(kNotifyJsonPath, std::vector<u8>(json.begin(), json.end()));
    UpsertUltrahandBootHook(msg);
    log_write("[RESTORE] armed Ultrahand reopen-hub hint\n");
}

auto ClearReopenHubHint() -> void {
    fs::FsNativeSd sd;
    sd.DeleteFile(ReopenHubFlagPath());
    sd.DeleteFile(kNotifyJsonPath);
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
    ArmReopenHubHint();
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
