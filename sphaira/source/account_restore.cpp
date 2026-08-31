#include "account_restore.hpp"

#include "account_link.hpp"
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

auto Join(const std::string& dir, const char* name) -> std::string {
    if (dir.empty() || dir == "/") {
        return std::string("/") + name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

auto CopyTree(ui::ProgressBox* pbox, fs::Fs& from, const std::string& src, fs::Fs& to, const std::string& dst, u32& files) -> Result {
    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }
    if (dst != "/" && !dst.empty()) {
        R_TRY(to.CreateDirectoryRecursively(dst.c_str()));
    }
    fs::Dir d;
    auto rc = from.OpenDirectory(src.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d);
    if (R_FAILED(rc) && (src.empty() || src == "/")) {
        rc = from.OpenDirectory("", FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d);
    }
    R_TRY(rc);
    std::vector<FsDirectoryEntry> ents;
    R_TRY(d.ReadAll(ents));
    for (const auto& e : ents) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }
        const auto child_src = Join(src, e.name);
        const auto child_dst = dst + "/" + e.name;
        if (e.type == FsDirEntryType_Dir) {
            R_TRY(CopyTree(pbox, from, child_src, to, child_dst, files));
            continue;
        }
        if (pbox) {
            pbox->NewTransfer(e.name);
            R_TRY(pbox->CopyFile(&from, &to, child_src.c_str(), child_dst.c_str()));
        } else {
            std::vector<u8> data;
            R_TRY(from.read_entire_file(child_src.c_str(), data));
            R_TRY(to.write_entire_file(child_dst.c_str(), data));
        }
        files++;
    }
    R_SUCCEED();
}

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

auto Open0010Ro() -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = ACCOUNT_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, true);
}

auto UidDashedLinkalho(const AccountUid& uid) -> std::string {
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%02x%02x-%08x%04x",
        static_cast<unsigned>(uid.uid[0] & 0xffffffffu),
        static_cast<unsigned>((uid.uid[0] >> 32) & 0xffffu),
        static_cast<unsigned>((uid.uid[0] >> 48) & 0xffffu),
        static_cast<unsigned>(uid.uid[1] & 0xffu),
        static_cast<unsigned>((uid.uid[1] >> 8) & 0xffu),
        static_cast<unsigned>((uid.uid[1] >> 32) & 0xffffffffu),
        static_cast<unsigned>((uid.uid[1] >> 16) & 0xffffu));
    return buf;
}

auto UidDashedRfc(const AccountUid& uid) -> std::string {
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
        static_cast<unsigned>(uid.uid[0] & 0xffffffffu),
        static_cast<unsigned>((uid.uid[0] >> 32) & 0xffffu),
        static_cast<unsigned>((uid.uid[0] >> 48) & 0xffffu),
        static_cast<unsigned>(uid.uid[1] & 0xffffu),
        static_cast<unsigned>((uid.uid[1] >> 16) & 0xffffu),
        static_cast<unsigned>((uid.uid[1] >> 32) & 0xffffffffu));
    return buf;
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

} // namespace

auto SnapshotOk() -> bool {
    fs::FsNativeSd sd;
    const auto root = std::string(SnapshotDir());
    if (sd.FileExists((root + "/su/registry.dat").c_str()) ||
        sd.FileExists((root + "/registry.dat").c_str())) {
        return true;
    }
    if (sd.DirExists((root + "/su/baas").c_str()) || sd.DirExists((root + "/baas").c_str())) {
        return true;
    }
    return sd.FileExists((std::string(PendingDir()) + "/dumped.ok").c_str()) && sd.DirExists(root.c_str());
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
    p.snapshot_ok = (has == "true") || SnapshotOk();
    if (p.snapshot_ok && p.phase == "wait_dump") {
        p.phase = "ready";
    }
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
    auto save = Open0010Ro();
    const auto rc = save.GetFsOpenResult();
    if (R_FAILED(rc)) {
        log_write("[RESTORE] 0010 read-only open 0x%X (no terminate)\n", rc);
        return rc;
    }
    fs::FsNativeSd sd;
    const auto dst = std::string(SnapshotDir());
    if (sd.DirExists(dst.c_str())) {
        sd.DeleteDirectoryRecursively(dst.c_str());
    }
    R_TRY(sd.CreateDirectoryRecursively(dst.c_str()));
    u32 files = 0;
    R_TRY(CopyTree(pbox, save, "/", sd, dst, files));
    R_UNLESS(files > 0, Result_FsEmpty);
    log_write("[RESTORE] 0010 snapshot files=%u\n", files);
    R_SUCCEED();
}

auto WriteExpectedFileList() -> Result {
    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(PendingDir()));
    std::string list;
    auto add = [&](const std::string& path) {
        list += path;
        list += '\n';
    };
    add("/su/registry.dat");
    add("/su/avators/profiles.dat");
    add("/registry.dat");
    for (const auto& u : account_link::ListUsers()) {
        const auto l = UidDashedLinkalho(u.uid);
        const auto r = UidDashedRfc(u.uid);
        const auto hex = account_link::UidHex(u.uid);
        add("/su/avators/" + l + ".jpg");
        add("/su/avators/" + r + ".jpg");
        add("/su/baas/" + l + ".dat");
        add("/su/baas/" + r + ".dat");
        add("/su/baas/" + hex + ".dat");
        u64 nas = 0;
        if (R_SUCCEEDED(account_link::QueryNintendoAccountId(u.uid, nas)) && nas) {
            char nbuf[17]{};
            std::snprintf(nbuf, sizeof(nbuf), "%016llx", static_cast<unsigned long long>(nas));
            const std::string n = nbuf;
            add("/su/nas/" + n + ".dat");
            add("/su/nas/" + n + "_id.token");
            add("/su/nas/" + n + "_refresh.token");
            add("/su/nas/" + n + "_user.json");
            char nshort[17]{};
            std::snprintf(nshort, sizeof(nshort), "%llx", static_cast<unsigned long long>(nas));
            add(std::string("/su/nas/") + nshort + ".dat");
            add(std::string("/su/nas/") + nshort + "_id.token");
            add(std::string("/su/nas/") + nshort + "_refresh.token");
            add(std::string("/su/nas/") + nshort + "_user.json");
        }
    }
    R_TRY(sd.write_entire_file(FilesPath(), std::vector<u8>(list.begin(), list.end())));
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

auto LaunchTegraDump() -> bool {
    fs::FsPath te_bin;
    if (!utils::findTegraExplorerPayload(te_bin)) {
        log_write("[RESTORE] TegraExplorer payload not found in /bootloader/payloads\n");
        return false;
    }

    fs::FsNativeSd sd;
    std::vector<u8> te;
    if (!ReadRomfsTe("romfs:/tegra/account_0010_dump.te", te)) {
        log_write("[RESTORE] dump te missing from romfs\n");
        return false;
    }
    sd.DeleteFile("/startup.te");
    if (R_FAILED(sd.write_entire_file("/startup.te", te))) {
        log_write("[RESTORE] write /startup.te failed\n");
        return false;
    }
    fsdevCommitDevice("sdmc");
    log_write("[RESTORE] wrote /startup.te, launching %s\n", static_cast<const char*>(te_bin));

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

} // namespace sphaira::account_restore
