#include "account/nand_transfer.hpp"
#include "account/account_restore.hpp"

#include "account/account_user.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"
#include "path_util.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::nand_transfer {
namespace {

auto ZipWriteBuffer(zipFile zf, const char* filename_in_zip, const void* data, size_t size, int compress_level = Z_DEFAULT_COMPRESSION) -> Result {
    zip_fileinfo zi{};
    if (ZIP_OK != zipOpenNewFileInZip(zf, filename_in_zip, &zi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, compress_level)) {
        return Result_FsInvalidType;
    }
    ON_SCOPE_EXIT(zipCloseFileInZip(zf));
    if (size > 0 && data) {
        if (ZIP_OK != zipWriteInFileInZip(zf, data, size)) {
            return Result_FsInvalidType;
        }
    }
    R_SUCCEED();
}

auto ReadZipFileEntry(unzFile zf, const char* filename, std::vector<u8>& out, u64 max_size = 1024 * 1024) -> bool {
    if (UNZ_OK != unzLocateFile(zf, filename, 0)) {
        return false;
    }
    unz_file_info64 info{};
    if (UNZ_OK != unzGetCurrentFileInfo64(zf, &info, nullptr, 0, nullptr, 0, nullptr, 0)) {
        return false;
    }
    if (info.uncompressed_size > max_size) {
        return false;
    }
    if (UNZ_OK != unzOpenCurrentFile(zf)) {
        return false;
    }
    ON_SCOPE_EXIT(unzCloseCurrentFile(zf));
    out.resize(info.uncompressed_size);
    if (info.uncompressed_size > 0) {
        u64 total_read = 0;
        while (total_read < info.uncompressed_size) {
            int read_bytes = unzReadCurrentFile(zf, out.data() + total_read, static_cast<unsigned int>(info.uncompressed_size - total_read));
            if (read_bytes <= 0) {
                out.clear();
                return false;
            }
            total_read += read_bytes;
        }
    }
    return true;
}

struct SaveSpec {
    u64 id;
    const char* hex;
    const char* label;
};

constexpr SaveSpec kSaves[] = {
    {0x8000000000000010ULL, "8000000000000010", "Profiles"},
    {0x8000000000000011ULL, "8000000000000011", "User ID generator"},
    {0x80000000000000F0ULL, "80000000000000F0", "Play hours"},
    {0x8000000000000041ULL, "8000000000000041", "HOME icons"},
};

auto Join(const std::string& dir, const char* name) -> std::string {
    if (dir.empty() || dir == "/") {
        return std::string("/") + name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

auto BaseName(const std::string& path) -> std::string {
    auto p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    const auto slash = p.find_last_of("/\\");
    if (slash == std::string::npos) {
        return p;
    }
    return p.substr(slash + 1);
}

auto ParentDir(const std::string& path) -> std::string {
    auto p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    const auto slash = p.find_last_of("/\\");
    if (slash == std::string::npos || slash == 0) {
        return "/";
    }
    return p.substr(0, slash);
}

auto PackLooksRight(fs::Fs& sd, const std::string& dir) -> bool {
    return sd.FileExists((dir + "/manifest.json").c_str()) ||
           sd.DirExists((dir + "/80000000000000F0").c_str()) ||
           sd.DirExists((dir + "/8000000000000010").c_str());
}

auto TryOpen(u64 id) -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = id;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, true);
}

void WipeRoot(fs::Fs& f) {
    fs::Dir d;
    auto rc = f.OpenDirectory("/", FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d);
    if (R_FAILED(rc)) {
        rc = f.OpenDirectory("", FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d);
    }
    if (R_FAILED(rc)) {
        return;
    }
    std::vector<FsDirectoryEntry> ents;
    if (R_FAILED(d.ReadAll(ents))) {
        return;
    }
    for (const auto& e : ents) {
        const auto path = Join("/", e.name);
        if (e.type == FsDirEntryType_Dir) {
            f.DeleteDirectoryRecursively(path.c_str());
        } else {
            f.DeleteFile(path.c_str());
        }
    }
}

auto ReadRomfsTe(const char* path, std::vector<u8>& te) -> bool {
    if (R_FAILED(romfsInit())) {
        log_write("[NAND] romfsInit failed for %s\n", path);
        return false;
    }
    ON_SCOPE_EXIT(romfsExit());
    return R_SUCCEEDED(fs::read_entire_file(path, te)) && !te.empty();
}

void CopyRomfsScript(fs::Fs& sd, const char* romfs_name, const std::string& pack_name) {
    std::vector<u8> te;
    const auto romfs = std::string("romfs:/tegra/") + romfs_name;
    if (!ReadRomfsTe(romfs.c_str(), te)) {
        log_write("[NAND] %s missing from romfs\n", romfs_name);
        return;
    }
    sd.CreateDirectoryRecursively("/config/kefir/nand_transfer");
    sd.write_entire_file((std::string("/config/kefir/nand_transfer/") + pack_name).c_str(), te);
    sd.CreateDirectoryRecursively("/TegraExplorer/scripts");
    sd.write_entire_file((std::string("/TegraExplorer/scripts/") + pack_name).c_str(), te);
}

void WriteScripts(fs::Fs& sd, const std::string& pack_dir) {
    CopyRomfsScript(sd, "nand_transfer_restore.te", "restore.te");
    CopyRomfsScript(sd, "nand_transfer_dump.te", "dump.te");
    std::vector<u8> restore;
    if (ReadRomfsTe("romfs:/tegra/nand_transfer_restore.te", restore)) {
        sd.write_entire_file((pack_dir + "/restore.te").c_str(), restore);
    }
    std::vector<u8> dump;
    if (ReadRomfsTe("romfs:/tegra/nand_transfer_dump.te", dump)) {
        sd.write_entire_file((pack_dir + "/dump.te").c_str(), dump);
    }
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

void WriteReadme(fs::Fs& sd, const std::string& pack_dir) {
    const char* text =
        "Kefir Hub NAND transfer pack\n"
        "\n"
        "Decrypted inner files of system saves (not raw SYSTEM:/save blobs).\n"
        "\n"
        "Backup user (Tools → Users) is a different thing: per-user backup of\n"
        "metadata, avatar, Nintendo link and that user's play hours. Restore Backup\n"
        "creates NEW user(s) and does not replace the whole play log. This pack\n"
        "keeps original UIDs and replaces destination 0010/00F0 content.\n"
        "\n"
        "Restore on the DESTINATION (preferred):\n"
        "  Kefir Hub → Users → Restore profiles & play hours → pick this folder.\n"
        "  Hub stages the pack, then TegraExplorer writes/signs (auto script).\n"
        "\n"
        "If Hub could not open saves here, it launches TegraExplorer itself.\n"
        "Manual fallback on destination: restore.te (menu) in this pack or scripts/.\n"
        "Do NOT copy raw SYSTEM:/save blobs — that bricks.\n"
        "Back up the destination SYSTEM partition first.\n";
    const std::vector<u8> body(text, text + std::strlen(text));
    sd.write_entire_file((pack_dir + "/README.txt").c_str(), body);
}

void WriteManifest(fs::Fs& sd, const std::string& pack_dir, const Report& report) {
    char json[512]{};
    std::snprintf(json, sizeof(json),
        "{\"kind\":\"kefir-nand-transfer\",\"version\":1,"
        "\"save_0010\":%s,\"save_0011\":%s,\"save_00F0\":%s,\"save_0041\":%s}\n",
        report.save_0010 ? "true" : "false",
        report.save_0011 ? "true" : "false",
        report.save_00F0 ? "true" : "false",
        report.save_0041 ? "true" : "false");
    const std::string s = json;
    sd.write_entire_file((pack_dir + "/manifest.json").c_str(),
        std::vector<u8>(s.begin(), s.end()));
}

void SetFlag(Report& report, const char* hex, bool ok) {
    if (!std::strcmp(hex, "8000000000000010")) {
        report.save_0010 = ok;
    } else if (!std::strcmp(hex, "8000000000000011")) {
        report.save_0011 = ok;
    } else if (!std::strcmp(hex, "80000000000000F0")) {
        report.save_00F0 = ok;
    } else if (!std::strcmp(hex, "8000000000000041")) {
        report.save_0041 = ok;
    }
}

} // namespace

auto Export(ui::ProgressBox* pbox, Report& out) -> Result {
    out = {};
    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    const std::string staging_dir = paths::DATA_ROOT + "/nand_transfer/_staging_" + stamp;
    out.dir = staging_dir;
    R_TRY(sd.CreateDirectoryRecursively(out.dir.c_str()));

    u32 dumped{};
    for (const auto& spec : kSaves) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
            pbox->NewTransfer(i18n::get(spec.label));
        }
        log_write("[NAND] opening %s (%s)\n", spec.label, spec.hex);
        auto save = TryOpen(spec.id);
        if (R_FAILED(save.GetFsOpenResult())) {
            log_write("[NAND] %s in use 0x%X (no process kill; use dump.te if this is play hours)\n",
                spec.label, save.GetFsOpenResult());
            SetFlag(out, spec.hex, false);
            continue;
        }
        u32 files{};
        const auto dst = out.dir + "/" + spec.hex;
        const auto rc = CopyTree(pbox, save, "/", sd, dst, files);
        if (R_FAILED(rc) || !files) {
            log_write("[NAND] dump %s failed rc=0x%X files=%u\n", spec.hex, rc, files);
            SetFlag(out, spec.hex, false);
            continue;
        }
        SetFlag(out, spec.hex, true);
        dumped++;
        log_write("[NAND] dump %s files=%u\n", spec.hex, files);
    }

    WriteScripts(sd, out.dir);
    WriteReadme(sd, out.dir);
    WriteManifest(sd, out.dir, out);

    R_UNLESS(dumped > 0, Result_FsEmpty);
    R_UNLESS(out.save_0010 || out.save_00F0, Result_FsEmpty);
    out.complete = out.save_0010 && out.save_00F0;
    log_write("[NAND] pack %s 0010=%d 0011=%d F0=%d 41=%d complete=%d\n",
        out.dir.c_str(), out.save_0010, out.save_0011, out.save_00F0, out.save_0041,
        out.complete ? 1 : 0);

    if (out.complete) {
        std::string final_archive;
        const auto arc_rc = FinalizePackArchive(out.dir, final_archive, pbox);
        if (R_SUCCEEDED(arc_rc)) {
            out.dir = final_archive;
            log_write("[NAND] export finalized archive: %s\n", out.dir.c_str());
        } else {
            log_write("[NAND] export finalize archive failed 0x%X; keeping staging %s\n",
                arc_rc, out.dir.c_str());
            out.complete = false;
            return arc_rc;
        }
    }
    R_SUCCEED();
}

auto ValidatePackArchiveFile(const std::string& path) -> bool {
    if (path.empty()) {
        return false;
    }
    fs::FsNativeSd sd;
    if (!sd.FileExists(path.c_str())) {
        return false;
    }
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    unzFile zf = unzOpen2_64(path.c_str(), &file_func);
    if (!zf) {
        return false;
    }
    ON_SCOPE_EXIT(unzClose(zf));

    std::vector<u8> manifest_bytes;
    if (!ReadZipFileEntry(zf, "manifest.json", manifest_bytes, 64 * 1024)) {
        return false;
    }
    const std::string manifest_str(manifest_bytes.begin(), manifest_bytes.end());
    if (manifest_str.find("kefir-nand-transfer") == std::string::npos) {
        return false;
    }

    unz_global_info64 ginfo{};
    if (UNZ_OK != unzGetGlobalInfo64(zf, &ginfo) || ginfo.number_entry == 0) {
        return false;
    }

    if (UNZ_OK != unzGoToFirstFile(zf)) {
        return false;
    }

    bool has_save_tree = false;
    for (u64 i = 0; i < ginfo.number_entry; ++i) {
        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zf)) {
                return false;
            }
        }
        unz_file_info64 info{};
        char name_buf[512]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zf, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            return false;
        }
        if (info.size_filename == 0 || info.size_filename >= sizeof(name_buf)) {
            return false;
        }
        std::string_view name{name_buf, info.size_filename};
        if (!path::IsSafeArchiveEntry(name)) {
            return false;
        }
        if (name.ends_with("manifest.json") && name != "manifest.json") {
            return false;
        }
        if (name.starts_with("8000000000000010") || name.starts_with("80000000000000F0")) {
            has_save_tree = true;
        }
    }

    return has_save_tree;
}

auto IsPackArchive(const std::string& path) -> bool {
    if (path.empty()) {
        return false;
    }
    std::string_view sv{path};
    if (!sv.ends_with(".zip") && !sv.ends_with(".ZIP")) {
        return false;
    }
    return ValidatePackArchiveFile(path);
}

auto IsPack(const std::string& dir) -> bool {
    if (dir.empty()) {
        return false;
    }
    fs::FsNativeSd sd;
    if (sd.FileExists(dir.c_str())) {
        return IsPackArchive(dir);
    }
    if (PackLooksRight(sd, dir)) {
        return true;
    }
    const auto base = BaseName(dir);
    if (base.rfind("80000000", 0) == 0) {
        return PackLooksRight(sd, ParentDir(dir));
    }
    return false;
}

namespace {

auto StemName(std::string_view name) -> std::string {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos) {
        return std::string{name};
    }
    return std::string{name.substr(0, dot)};
}

auto NormalizeUidKey(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '-') {
            continue;
        }
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

auto UidHexBytes(const u8* bytes16) -> std::string {
    char buf[33]{};
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf + i * 2, 3, "%02X", bytes16[i]);
    }
    return buf;
}

auto UidHexRawFromBytes(const u8* bytes16) -> std::string {
    AccountUid uid{};
    std::memcpy(&uid, bytes16, sizeof(AccountUid));
    char buf[33]{};
    std::snprintf(buf, sizeof(buf), "%016llX%016llX",
        static_cast<unsigned long long>(uid.uid[0]),
        static_cast<unsigned long long>(uid.uid[1]));
    return buf;
}

auto ApplyProfilesNicknamesData(const std::vector<u8>& data, std::vector<PackUser>& users) -> void {
    constexpr size_t kHeader = 0x10;
    constexpr size_t kBlock = 0xC8;
    constexpr size_t kMaxUsers = 8;
    if (data.size() < kHeader + kBlock) {
        return;
    }
    const size_t max_i = std::min(kMaxUsers, (data.size() - kHeader) / kBlock);
    for (size_t i = 0; i < max_i; ++i) {
        const size_t base = kHeader + i * kBlock;
        if (base + 0x28 + 0x20 > data.size()) {
            break;
        }
        const u8* uid_bytes = data.data() + base;
        bool all_zero = true;
        for (int b = 0; b < 16; ++b) {
            if (uid_bytes[b] != 0) {
                all_zero = false;
                break;
            }
        }
        if (all_zero) {
            continue;
        }

        const char* nick_ptr = reinterpret_cast<const char*>(data.data() + base + 0x28);
        size_t nick_len = 0;
        while (nick_len < 0x20 && nick_ptr[nick_len] != '\0') {
            nick_len++;
        }
        if (nick_len == 0) {
            continue;
        }
        const std::string nickname(nick_ptr, nick_len);
        const auto hex_bytes = UidHexBytes(uid_bytes);
        const auto hex_raw = UidHexRawFromBytes(uid_bytes);
        const auto key_bytes = NormalizeUidKey(hex_bytes);
        const auto key_raw = NormalizeUidKey(hex_raw);

        bool matched = false;
        for (auto& u : users) {
            const auto key = NormalizeUidKey(u.uid);
            if (key == key_bytes || key == key_raw) {
                u.nickname = nickname;
                matched = true;
                break;
            }
        }
        if (!matched) {
            PackUser u;
            u.uid = hex_raw;
            u.nickname = nickname;
            users.push_back(std::move(u));
        }
    }
}

auto ApplyProfilesNicknames(fs::Fs& sd, const std::string& pack_dir, std::vector<PackUser>& users) -> void {
    const auto path = pack_dir + "/8000000000000010/su/avators/profiles.dat";
    std::vector<u8> data;
    if (R_FAILED(sd.read_entire_file(path.c_str(), data))) {
        return;
    }
    ApplyProfilesNicknamesData(data, users);
}

auto CollectArchivePackUsers(const std::string& pack_path) -> std::vector<PackUser> {
    std::vector<PackUser> users;
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    unzFile zf = unzOpen2_64(pack_path.c_str(), &file_func);
    if (!zf) {
        return users;
    }
    ON_SCOPE_EXIT(unzClose(zf));

    auto upsert = [&](const std::string& uid, const std::string& avatar) {
        if (uid.empty() || uid == "profiles") {
            return;
        }
        for (auto& u : users) {
            if (u.uid == uid) {
                if (u.avatar_path.empty() && !avatar.empty()) {
                    u.avatar_path = avatar;
                }
                return;
            }
        }
        PackUser u;
        u.uid = uid;
        u.nickname = uid;
        u.avatar_path = avatar;
        users.push_back(std::move(u));
    };

    unz_global_info64 ginfo{};
    if (UNZ_OK == unzGetGlobalInfo64(zf, &ginfo) && ginfo.number_entry > 0 && UNZ_OK == unzGoToFirstFile(zf)) {
        for (u64 i = 0; i < ginfo.number_entry; ++i) {
            if (i > 0) {
                if (UNZ_OK != unzGoToNextFile(zf)) break;
            }
            unz_file_info64 info{};
            char name_buf[512]{};
            if (UNZ_OK != unzGetCurrentFileInfo64(zf, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
                continue;
            }
            std::string_view entry{name_buf, info.size_filename};
            if (entry.starts_with("8000000000000010/su/avators/")) {
                auto fname = entry.substr(std::string_view("8000000000000010/su/avators/").size());
                if (fname != "profiles.dat" && (fname.ends_with(".jpg") || fname.ends_with(".JPG"))) {
                    upsert(StemName(fname), std::string(entry));
                }
            } else if (entry.starts_with("8000000000000010/su/baas/")) {
                auto fname = entry.substr(std::string_view("8000000000000010/su/baas/").size());
                if (fname.ends_with(".dat") || fname.ends_with(".DAT")) {
                    upsert(StemName(fname), {});
                }
            }
        }
    }

    std::vector<u8> prof_data;
    if (ReadZipFileEntry(zf, "8000000000000010/su/avators/profiles.dat", prof_data, 64 * 1024)) {
        ApplyProfilesNicknamesData(prof_data, users);
    }

    std::sort(users.begin(), users.end(), [](const PackUser& a, const PackUser& b) {
        return a.uid < b.uid;
    });
    return users;
}

auto CollectPackUsers(fs::Fs& sd, const std::string& pack_dir) -> std::vector<PackUser> {
    std::vector<PackUser> users;
    const auto avators = pack_dir + "/8000000000000010/su/avators";
    const auto baas = pack_dir + "/8000000000000010/su/baas";

    auto upsert = [&](const std::string& uid, const std::string& avatar) {
        if (uid.empty() || uid == "profiles") {
            return;
        }
        for (auto& u : users) {
            if (u.uid == uid) {
                if (u.avatar_path.empty() && !avatar.empty()) {
                    u.avatar_path = avatar;
                }
                return;
            }
        }
        PackUser u;
        u.uid = uid;
        u.nickname = uid;
        u.avatar_path = avatar;
        users.push_back(std::move(u));
    };

    auto scan_dir = [&](const std::string& path, bool avatars) {
        if (!sd.DirExists(path.c_str())) {
            return;
        }
        fs::Dir d;
        if (R_FAILED(sd.OpenDirectory(path.c_str(), FsDirOpenMode_ReadFiles, &d))) {
            return;
        }
        std::vector<FsDirectoryEntry> ents;
        if (R_FAILED(d.ReadAll(ents))) {
            return;
        }
        for (const auto& e : ents) {
            if (e.type != FsDirEntryType_File) {
                continue;
            }
            std::string_view name{e.name};
            if (name == "profiles.dat") {
                continue;
            }
            if (avatars) {
                if (!name.ends_with(".jpg") && !name.ends_with(".JPG")) {
                    continue;
                }
                const auto uid = StemName(name);
                upsert(uid, path + "/" + e.name);
            } else {
                if (!name.ends_with(".dat") && !name.ends_with(".DAT")) {
                    continue;
                }
                upsert(StemName(name), {});
            }
        }
    };

    scan_dir(avators, true);
    scan_dir(baas, false);
    ApplyProfilesNicknames(sd, pack_dir, users);
    std::sort(users.begin(), users.end(), [](const PackUser& a, const PackUser& b) {
        return a.uid < b.uid;
    });
    return users;
}

auto MakePackInfo(fs::Fs& sd, const std::string& dir) -> PackInfo {
    PackInfo info;
    info.dir = dir;
    info.name = BaseName(dir);
    info.created_label = account_user::FormatPackCreated(info.name, {});
    info.save_0010 = sd.DirExists((dir + "/8000000000000010").c_str());
    info.save_00F0 = sd.DirExists((dir + "/80000000000000F0").c_str());
    info.accounts = static_cast<u32>(CollectPackUsers(sd, dir).size());
    info.is_archive = false;
    return info;
}

auto MakeArchivePackInfo(const std::string& path) -> PackInfo {
    PackInfo info;
    info.dir = path;
    info.name = BaseName(path);
    info.created_label = account_user::FormatPackCreated(info.name, {});
    info.is_archive = true;

    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    unzFile zf = unzOpen2_64(path.c_str(), &file_func);
    if (zf) {
        ON_SCOPE_EXIT(unzClose(zf));
        unz_global_info64 ginfo{};
        if (UNZ_OK == unzGetGlobalInfo64(zf, &ginfo) && UNZ_OK == unzGoToFirstFile(zf)) {
            for (u64 i = 0; i < ginfo.number_entry; ++i) {
                if (i > 0) {
                    if (UNZ_OK != unzGoToNextFile(zf)) break;
                }
                char name_buf[256]{};
                unz_file_info64 fi{};
                if (UNZ_OK == unzGetCurrentFileInfo64(zf, &fi, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
                    std::string_view sv{name_buf, fi.size_filename};
                    if (sv.starts_with("8000000000000010")) info.save_0010 = true;
                    if (sv.starts_with("80000000000000F0")) info.save_00F0 = true;
                }
            }
        }
    }

    info.accounts = static_cast<u32>(CollectArchivePackUsers(path).size());
    return info;
}

auto CollectAllFilesRecursively(fs::Fs& sd, const std::string& base, const std::string& rel, std::vector<std::string>& files) -> Result {
    const auto full = rel.empty() ? base : (base + "/" + rel);
    fs::Dir d;
    R_TRY(sd.OpenDirectory(full.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d));
    std::vector<FsDirectoryEntry> ents;
    R_TRY(d.ReadAll(ents));
    for (const auto& e : ents) {
        const auto child_rel = rel.empty() ? std::string(e.name) : (rel + "/" + e.name);
        if (e.type == FsDirEntryType_Dir) {
            R_TRY(CollectAllFilesRecursively(sd, base, child_rel, files));
        } else if (e.type == FsDirEntryType_File) {
            if (std::string_view{e.name}.ends_with(".part")) {
                continue;
            }
            files.push_back(child_rel);
        }
    }
    R_SUCCEED();
}

} // namespace

auto ReadPackUserAvatar(const PackInfo& pack, const PackUser& user, std::vector<u8>& out_jpeg) -> bool {
    out_jpeg.clear();
    if (user.avatar_path.empty()) {
        return false;
    }
    fs::FsNativeSd sd;
    if (!pack.is_archive) {
        return R_SUCCEEDED(sd.read_entire_file(user.avatar_path.c_str(), out_jpeg)) && !out_jpeg.empty();
    }
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    unzFile zf = unzOpen2_64(pack.dir.c_str(), &file_func);
    if (!zf) {
        return false;
    }
    ON_SCOPE_EXIT(unzClose(zf));
    return ReadZipFileEntry(zf, user.avatar_path.c_str(), out_jpeg, 1024 * 1024) && !out_jpeg.empty();
}

auto FinalizePackArchive(const std::string& staging_dir, std::string& out_archive_path, ui::ProgressBox* pbox) -> Result {
    out_archive_path.clear();
    fs::FsNativeSd sd;
    R_UNLESS(path::IsSafeBackupStagingDir(staging_dir), Result_FsInvalidType);
    R_UNLESS(sd.DirExists(staging_dir.c_str()), Result_FsInvalidType);

    const auto base = BaseName(staging_dir);
    R_UNLESS(!base.empty(), Result_FsInvalidType);

    const auto parent = ParentDir(staging_dir);
    std::string archive_stem = base;
    if (archive_stem.starts_with("_staging_")) {
        archive_stem = archive_stem.substr(std::string_view("_staging_").size());
    }
    std::string base_archive = parent + "/" + archive_stem + ".kefir-nand.zip";
    std::string final_path = base_archive;
    int suffix = 1;
    while (sd.FileExists(final_path.c_str()) || sd.DirExists(final_path.c_str())) {
        final_path = parent + "/" + archive_stem + "_" + std::to_string(suffix++) + ".kefir-nand.zip";
    }

    const std::string part_path = final_path + ".part";
    sd.DeleteFile(part_path.c_str());

    std::vector<std::string> rel_files;
    R_TRY(CollectAllFilesRecursively(sd, staging_dir, "", rel_files));
    R_UNLESS(!rel_files.empty(), Result_FsEmpty);

    bool has_manifest = false;
    for (const auto& f : rel_files) {
        if (f == "manifest.json") {
            has_manifest = true;
            break;
        }
    }
    R_UNLESS(has_manifest, Result_FsInvalidType);

    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    zipFile zf = zipOpen2_64(part_path.c_str(), APPEND_STATUS_CREATE, nullptr, &file_func);
    R_UNLESS(zf, Result_FsInvalidType);

    bool zip_closed = false;
    bool archive_success = false;
    ON_SCOPE_EXIT({
        if (zf && !zip_closed) {
            zipClose(zf, nullptr);
        }
        if (!archive_success) {
            sd.DeleteFile(part_path.c_str());
        }
    });

    for (const auto& rel : rel_files) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
            pbox->NewTransfer(rel.c_str());
        }
        std::vector<u8> data;
        const auto src_path = staging_dir + "/" + rel;
        R_TRY(sd.read_entire_file(src_path.c_str(), data));
        R_TRY(ZipWriteBuffer(zf, rel.c_str(), data.data(), data.size()));
    }

    if (zipClose(zf, "KefirHub NAND transfer backup") != ZIP_OK) {
        zip_closed = true;
        log_write("[NAND] zipClose failed for %s\n", part_path.c_str());
        return Result_FsInvalidType;
    }
    zip_closed = true;

    if (!ValidatePackArchiveFile(part_path)) {
        log_write("[NAND] validation failed for %s\n", part_path.c_str());
        return Result_FsInvalidType;
    }

    R_TRY(sd.RenameFile(part_path.c_str(), final_path.c_str()));
    archive_success = true;

    sd.DeleteDirectoryRecursively(staging_dir.c_str());
    out_archive_path = final_path;
    log_write("[NAND] finalized archive %s (from %s)\n", final_path.c_str(), staging_dir.c_str());
    R_SUCCEED();
}

auto StagePackArchiveForRestore(const std::string& archive_path, std::string& out_staging_dir, ui::ProgressBox* pbox) -> Result {
    out_staging_dir.clear();
    fs::FsNativeSd sd;
    R_UNLESS(sd.FileExists(archive_path.c_str()) && IsPackArchive(archive_path), Result_FsInvalidType);

    const auto stem = StemName(BaseName(archive_path));
    R_UNLESS(!stem.empty(), Result_FsInvalidType);

    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    std::string staging_dir = paths::DATA_ROOT + "/nand_transfer/_restore_" + stem + "_" + stamp;
    if (sd.DirExists(staging_dir.c_str())) {
        int suffix = 1;
        while (sd.DirExists((staging_dir + "_" + std::to_string(suffix)).c_str())) {
            suffix++;
        }
        staging_dir += "_" + std::to_string(suffix);
    }
    account_restore::CleanRestoreStagingDir(staging_dir);
    R_TRY(sd.CreateDirectoryRecursively(staging_dir.c_str()));

    bool stage_success = false;
    ON_SCOPE_EXIT({
        if (!stage_success) {
            account_restore::CleanRestoreStagingDir(staging_dir);
        }
    });

    R_TRY(thread::TransferUnzipAll(pbox, archive_path.c_str(), &sd, staging_dir.c_str()));

    if (!PackLooksRight(sd, staging_dir) || !sd.FileExists((staging_dir + "/manifest.json").c_str())) {
        log_write("[NAND] extracted archive failed validation: %s\n", staging_dir.c_str());
        return Result_FsInvalidType;
    }

    stage_success = true;
    out_staging_dir = staging_dir;
    log_write("[NAND] staged archive %s to %s\n", archive_path.c_str(), staging_dir.c_str());
    R_SUCCEED();
}

auto ListPacks() -> std::vector<PackInfo> {
    std::vector<PackInfo> packs;
    const auto root = paths::DATA_ROOT + "/nand_transfer";
    fs::FsNativeSd sd;
    if (!sd.DirExists(root.c_str())) {
        return packs;
    }
    if (PackLooksRight(sd, root)) {
        packs.push_back(MakePackInfo(sd, root));
        return packs;
    }

    fs::Dir d;
    if (R_FAILED(sd.OpenDirectory(root.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) {
        return packs;
    }
    std::vector<FsDirectoryEntry> ents;
    if (R_FAILED(d.ReadAll(ents))) {
        return packs;
    }
    for (const auto& e : ents) {
        std::string_view name{e.name};
        if (name.empty() || name[0] == '.' || name.starts_with("_restore_") || name.starts_with("_staging_")) {
            continue;
        }
        if (name == "dump.te" || name == "restore.te" || name.ends_with(".part") || name.ends_with(".te")) {
            continue;
        }
        const auto path = root + "/" + e.name;
        if (e.type == FsDirEntryType_File) {
            if (name.ends_with(".kefir-nand.zip") || name.ends_with(".zip")) {
                if (IsPackArchive(path)) {
                    packs.push_back(MakeArchivePackInfo(path));
                }
            }
            continue;
        }
        if (e.type != FsDirEntryType_Dir) {
            continue;
        }
        if (!PackLooksRight(sd, path) && !IsPack(path)) {
            continue;
        }
        packs.push_back(MakePackInfo(sd, path));
    }
    std::sort(packs.begin(), packs.end(), [](const PackInfo& a, const PackInfo& b) {
        return a.name > b.name;
    });
    return packs;
}

auto ListPackUsers(const std::string& pack_dir) -> std::vector<PackUser> {
    if (pack_dir.empty()) {
        return {};
    }
    fs::FsNativeSd sd;
    if (sd.FileExists(pack_dir.c_str()) && IsPackArchive(pack_dir)) {
        return CollectArchivePackUsers(pack_dir);
    }
    auto dir = pack_dir;
    if (!PackLooksRight(sd, dir)) {
        const auto parent = ParentDir(dir);
        if (PackLooksRight(sd, parent)) {
            dir = parent;
        }
    }
    if (!PackLooksRight(sd, dir)) {
        return {};
    }
    return CollectPackUsers(sd, dir);
}

auto Import(ui::ProgressBox* pbox, const std::string& dir, Report& out) -> Result {
    out = {};
    fs::FsNativeSd sd;
    auto pack = dir;
    if (!PackLooksRight(sd, pack)) {
        const auto parent = ParentDir(pack);
        if (PackLooksRight(sd, parent)) {
            pack = parent;
        }
    }
    R_UNLESS(PackLooksRight(sd, pack), Result_FsInvalidType);
    out.dir = pack;

    u32 restored{};
    for (const auto& spec : kSaves) {
        const auto src = pack + "/" + spec.hex;
        if (!sd.DirExists(src.c_str())) {
            SetFlag(out, spec.hex, false);
            continue;
        }
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
            pbox->NewTransfer(i18n::get(spec.label));
        }
        log_write("[NAND] restore opening %s (%s)\n", spec.label, spec.hex);
        auto save = TryOpen(spec.id);
        if (R_FAILED(save.GetFsOpenResult())) {
            log_write("[NAND] restore open %s failed 0x%X\n", spec.hex, save.GetFsOpenResult());
            SetFlag(out, spec.hex, false);
            continue;
        }
        WipeRoot(save);
        u32 files{};
        const auto rc = CopyTree(pbox, sd, src, save, "/", files);
        if (R_FAILED(rc) || !files) {
            log_write("[NAND] restore %s failed rc=0x%X files=%u\n", spec.hex, rc, files);
            SetFlag(out, spec.hex, false);
            continue;
        }
        if (R_FAILED(save.Commit())) {
            log_write("[NAND] restore commit %s failed\n", spec.hex);
            SetFlag(out, spec.hex, false);
            continue;
        }
        SetFlag(out, spec.hex, true);
        restored++;
        log_write("[NAND] restore %s files=%u\n", spec.hex, files);
    }

    R_UNLESS(restored > 0, Result_FsEmpty);
    R_UNLESS(out.save_0010 || out.save_00F0, Result_FsEmpty);
    log_write("[NAND] restored %s 0010=%d 0011=%d F0=%d 41=%d\n",
        pack.c_str(), out.save_0010, out.save_0011, out.save_00F0, out.save_0041);
    R_SUCCEED();
}

} // namespace sphaira::nand_transfer
