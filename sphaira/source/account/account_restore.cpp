#include "account/account_restore.hpp"
#include "account_restore_internal.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "ui/progress_box.hpp"
#include "utils/utils.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::account_restore {
namespace {

constexpr u64 ACCOUNT_SAVE_ID = 0x8000000000000010ULL;
constexpr u64 ACCOUNT_EXTRA_SAVE_ID = 0x8000000000000011ULL;
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
    te.clear();
    if (R_SUCCEEDED(fs::read_entire_file(path, te)) && !te.empty()) {
        return true;
    }
    const Result rc = romfsInit();
    if (R_SUCCEEDED(rc)) {
        const bool ok = R_SUCCEEDED(fs::read_entire_file(path, te)) && !te.empty();
        romfsExit();
        return ok;
    }
    log_write("[RESTORE] ReadRomfsTe failed for %s (romfsInit rc=0x%X)\n", path, rc);
    return false;
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
    std::vector<u8> sdir_bytes;
    if (R_SUCCEEDED(sd.read_entire_file(SafetyDirRecordPath(), sdir_bytes)) && !sdir_bytes.empty()) {
        const std::string sdir(sdir_bytes.begin(), sdir_bytes.end());
        if (sd.FileExists(SafetySnapshotOkPath(sdir).c_str()) && sd.FileExists(SafetySnapshotPath(sdir).c_str())) {
            return true;
        }
    }
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
    p.staging_dir = ReadJsonField(s, "staging_dir");
    return p;
}

auto HasUnfinishedRestore() -> bool {
    const auto p = LoadPending();
    if (p.rolled_back) {
        return true;
    }
    return p.present && p.phase != "applied";
}

auto SavePending(const std::vector<std::string>& pack_dirs, const std::string& phase, bool snapshot_ok, const std::string& staging_dir) -> Result {
    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(PendingDir()));
    std::string packs;
    for (size_t i = 0; i < pack_dirs.size(); i++) {
        if (i) {
            packs += '|';
        }
        packs += pack_dirs[i];
    }
    auto json = std::string{"{\"kind\":\"kefir-restore-pending\",\"phase\":\""} + phase +
        "\",\"has_0010\":" + (snapshot_ok ? "true" : "false") +
        ",\"packs\":\"" + packs + "\"";
    if (!staging_dir.empty()) {
        json += ",\"staging_dir\":\"" + staging_dir + "\"";
    }
    json += "}";
    R_TRY(sd.write_entire_file(StatePath(), std::vector<u8>(json.begin(), json.end())));
    log_write("[RESTORE] pending phase=%s snapshot=%d packs=%zu staging=%s\n",
        phase.c_str(), snapshot_ok ? 1 : 0, pack_dirs.size(), staging_dir.c_str());
    R_SUCCEED();
}

auto CleanRestoreStagingDir(const std::string& path) -> bool {
    if (!sphaira::path::IsSafeRestoreStagingDir(path)) {
        log_write("[RESTORE] rejected unsafe staging cleanup: %s\n", path.c_str());
        return false;
    }
    fs::FsNativeSd sd;
    if (sd.DirExists(path.c_str())) {
        return R_SUCCEEDED(sd.DeleteDirectoryRecursively(path.c_str()));
    }
    return true;
}

auto LoadCandidateScript(fs::FsNativeSd& sd, const char* name) -> std::string {
    if (!name || !*name) return {};
    std::vector<u8> b;
    if (ReadRomfsTe((std::string("romfs:/tegra/") + name).c_str(), b) && !b.empty())
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    if (R_SUCCEEDED(sd.read_entire_file((std::string("/TegraExplorer/scripts/") + name).c_str(), b)) && !b.empty())
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    if (R_SUCCEEDED(sd.read_entire_file((std::string(PendingDir()) + "/" + name).c_str(), b)) && !b.empty())
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    return {};
}

auto CollectCandidates(fs::FsNativeSd& sd, const char* script_name) -> std::vector<std::string> {
    std::vector<std::string> c;
    auto add = [&](const char* n) { if (n && *n) { auto s = LoadCandidateScript(sd, n); if (!s.empty()) c.push_back(std::move(s)); } };
    if (script_name && *script_name) add(script_name);
    else { add(NandDumpTeName()); add(DumpTeName()); add(NandRestoreTeName()); add(ApplyLinkTeName()); }
    return c;
}

auto CleanStartupTeIfOwned(fs::FsNativeSd& sd, const char* script_name) -> Result {
    if (!sd.FileExists("/startup.te")) return 0;
    std::vector<u8> actual;
    const auto read_rc = sd.read_entire_file("/startup.te", actual);
    if (R_FAILED(read_rc)) {
        log_write("[RESTORE] failed to read /startup.te: 0x%X\n", read_rc);
        return read_rc;
    }
    const std::string_view act(reinterpret_cast<const char*>(actual.data()), actual.size());
    if (!IsStartupTeContentOwned(act, CollectCandidates(sd, script_name))) {
        log_write("[RESTORE] preserving unrelated /startup.te\n");
        return 0;
    }
    log_write("[RESTORE] removing owned /startup.te\n");
    const auto rc = sd.DeleteFile("/startup.te");
    std::remove("/startup.te");
    return (R_FAILED(rc) && sd.FileExists("/startup.te")) ? rc : 0;
}

auto ClearPending() -> Result {
    fs::FsNativeSd sd;
    const auto p = LoadPending();
    AbandonPendingInfo info;
    info.staging_dir = p.staging_dir;
    info.pack_dirs = p.pack_dirs;
    info.pending_dir = PendingDir();
    info.tegra_scripts = {
        DumpTeName(), ApplyLinkTeName(), NandRestoreTeName(), NandDumpTeName(),
        RollbackTeName(), "account_0010_rollback.te"
    };

    struct NativeAbandonFs : public AbandonFsOps {
        fs::FsNativeSd& m_sd;
        explicit NativeAbandonFs(fs::FsNativeSd& s) : m_sd(s) {}
        auto DirExists(std::string_view p) -> bool override { return m_sd.DirExists(p.data()); }
        auto FileExists(std::string_view p) -> bool override { return m_sd.FileExists(p.data()); }
        auto ReadEntireFile(std::string_view p, std::vector<uint8_t>& out) -> int64_t override { return m_sd.read_entire_file(p.data(), out); }
        auto WriteEntireFile(std::string_view p, const std::vector<uint8_t>& d) -> int64_t override { return m_sd.write_entire_file(p.data(), d); }
        auto DeleteFile(std::string_view p) -> int64_t override {
            return m_sd.DeleteFile(p.data());
        }
        auto DeleteDirectoryRecursively(std::string_view p) -> int64_t override { return m_sd.DeleteDirectoryRecursively(p.data()); }
        auto Commit() -> int64_t override {
            const auto sdmc_rc = fsdevCommitDevice("sdmc");
            return (sdmc_rc != 0) ? 0x100 : m_sd.Commit();
        }
    } fs(sd);

    const auto rc = static_cast<Result>(PerformAbandonCleanup(fs, info, CollectCandidates(sd, nullptr)));
    if (R_FAILED(rc)) log_write("[RESTORE] ClearPending failed: 0x%X\n", rc);
    return rc;
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

auto RemoveUltrahandBootHook() -> Result {
    fs::FsNativeSd sd;
    if (!sd.FileExists(kBootPackagePath)) return 0;
    std::vector<u8> raw;
    const auto r_rc = sd.read_entire_file(kBootPackagePath, raw);
    if (R_FAILED(r_rc)) return r_rc;
    if (raw.empty()) return 0;
    const std::string text(raw.begin(), raw.end());
    const auto cleaned = RemoveUltrahandBootHookText(text);
    if (cleaned != text) {
        const auto w_rc = sd.write_entire_file(kBootPackagePath, std::vector<u8>(cleaned.begin(), cleaned.end()));
        if (R_FAILED(w_rc)) return w_rc;
    }
    return 0;
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

auto ClearReopenHubHint() -> Result {
    fs::FsNativeSd sd;
    Result rc = 0;
    auto del = [&](const char* p) {
        if (sd.FileExists(p)) {
            const auto drc = sd.DeleteFile(p);
            if (R_FAILED(drc) && sd.FileExists(p) && R_SUCCEEDED(rc)) rc = drc;
        }
    };
    del(ReopenHubFlagPath());
    del(kNotifyJsonPath);
    const auto hrc = RemoveUltrahandBootHook();
    if (R_FAILED(hrc) && R_SUCCEEDED(rc)) rc = hrc;
    return rc;
}

auto CleanDumpHandshake() -> Result {
    fs::FsNativeSd sd;
    Result rc = 0;
    auto del = [&](const char* p) {
        if (sd.FileExists(p)) {
            const auto drc = sd.DeleteFile(p);
            if (R_FAILED(drc) && sd.FileExists(p) && R_SUCCEEDED(rc)) rc = drc;
        }
    };
    del(DumpedOkPath());
    del(NandPackPath());
    const auto st_rc = CleanStartupTeIfOwned(sd, NandDumpTeName());
    if (R_FAILED(st_rc) && R_SUCCEEDED(rc)) rc = st_rc;
    del((std::string("/TegraExplorer/scripts/") + NandDumpTeName()).c_str());
    del((std::string(PendingDir()) + "/" + NandDumpTeName()).c_str());

    const auto sdmc_rc = fsdevCommitDevice("sdmc");
    if (sdmc_rc != 0 && R_SUCCEEDED(rc)) rc = 0x100;
    const auto commit_rc = sd.Commit();
    if (R_FAILED(commit_rc) && R_SUCCEEDED(rc)) rc = commit_rc;

    log_write("[RESTORE] cleaned dump handshake temps (rc=0x%X)\n", rc);
    return rc;
}

auto WriteStartupTe(const char* romfs_name) -> bool {
    std::vector<u8> te;
    const auto romfs = std::string("romfs:/tegra/") + romfs_name;
    if (!ReadRomfsTe(romfs.c_str(), te)) {
        log_write("[RESTORE] %s missing from romfs\n", romfs_name);
        return false;
    }
    fs::FsNativeSd sd;
    sd.DeleteFile("/startup.te");
    std::remove("/startup.te");

    bool written = false;
    std::FILE* fp = std::fopen("/startup.te", "wb");
    if (fp) {
        if (std::fwrite(te.data(), 1, te.size(), fp) == te.size()) {
            std::fflush(fp);
            written = true;
        }
        std::fclose(fp);
    }
    if (!written) {
        if (R_FAILED(sd.write_entire_file("/startup.te", te))) {
            log_write("[RESTORE] write /startup.te failed\n");
            return false;
        }
    }
    CopyTe(sd, romfs_name, romfs_name);

    fsdevCommitDevice("sdmc");
    sd.Commit();
    log_write("[RESTORE] wrote /startup.te (%zu bytes) from %s\n", te.size(), romfs_name);
    return true;
}

auto LaunchTegraRomfs(const char* romfs_name) -> bool {
    fs::FsPath te_bin;
    if (!utils::findTegraExplorerPayload(te_bin)) {
        log_write("[RESTORE] TegraExplorer payload not found in /bootloader/payloads\n");
        return false;
    }

    if (!WriteStartupTe(romfs_name)) {
        log_write("[RESTORE] failed to write startup.te for %s\n", romfs_name);
        return false;
    }

    log_write("[RESTORE] launching TegraExplorer with %s: %s\n",
        romfs_name, static_cast<const char*>(te_bin));

    return utils::rebootToPayload(static_cast<const char*>(te_bin));
}

auto LaunchTegraDump() -> bool {
    return LaunchTegraRomfs("account_0010_dump.te");
}

} // namespace sphaira::account_restore
