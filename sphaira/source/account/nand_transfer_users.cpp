#include "account/nand_transfer_internal.hpp"
#include "account/account_user.hpp"
#include "app_paths.hpp"
#include "fs.hpp"
#include "minizip_helper.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <minizip/unzip.h>

namespace sphaira::nand_transfer {
namespace {

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

} // namespace

auto StemName(std::string_view name) -> std::string {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos) {
        return std::string{name};
    }
    return std::string{name.substr(0, dot)};
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

} // namespace sphaira::nand_transfer
