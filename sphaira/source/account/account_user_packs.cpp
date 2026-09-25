#include "account/account_user.hpp"
#include "account/account_link.hpp"
#include "account/account_playtime.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"
#include "path_util.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <minizip/unzip.h>

namespace sphaira::account_user {
namespace {

auto ReadZipFileEntry(unzFile zf, const char* filename, std::vector<u8>& out, u64 max_size = 64 * 1024) -> bool {
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

} // namespace

auto ReadPackAvatar(const Pack& pack, std::vector<u8>& out_jpeg) -> bool {
    out_jpeg.clear();
    fs::FsNativeSd sd;
    if (!pack.is_archive) {
        return R_SUCCEEDED(sd.read_entire_file((pack.dir + "/avatar.jpg").c_str(), out_jpeg)) && !out_jpeg.empty();
    }
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    unzFile zf = unzOpen2_64(pack.dir.c_str(), &file_func);
    if (!zf) {
        return false;
    }
    ON_SCOPE_EXIT(unzClose(zf));
    return ReadZipFileEntry(zf, "avatar.jpg", out_jpeg, 1024 * 1024) && !out_jpeg.empty();
}

auto ExtractPackToDirectory(const Pack& pack, const std::string& out_dir, ui::ProgressBox* pbox) -> Result {
    R_UNLESS(pack.is_archive && std::string_view{pack.dir}.ends_with(".kefir-user.zip"), Result_FsInvalidType);
    const auto validated = FindUserPack(pack.dir);
    R_UNLESS(!validated.dir.empty(), Result_FsInvalidType);

    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(out_dir.c_str()));
    R_TRY(thread::TransferUnzipAll(pbox, pack.dir.c_str(), &sd, out_dir.c_str()));
    if (!sd.FileExists((out_dir + "/profile.json").c_str()) || !sd.FileExists((out_dir + "/manifest.json").c_str())) {
        log_write("[USER] Extracted pack missing profile.json or manifest.json in %s\n", out_dir.c_str());
        return Result_FsInvalidType;
    }
    R_SUCCEED();
}

auto EnsureRootsMigrated() -> void {
    const auto old_root = paths::DATA_ROOT + "/user_packs";
    const auto new_root = paths::DATA_ROOT + "/account_backups";
    fs::FsNativeSd sd;
    if (!sd.DirExists(new_root.c_str()) && !sd.FileExists(new_root.c_str()) && sd.DirExists(old_root.c_str())) {
        const auto rc = sd.RenameDirectory(old_root.c_str(), new_root.c_str());
        log_write("[USER] Migrated legacy user_packs to account_backups: 0x%X\n", rc);
    }
}

auto FindUserPack(const std::string& source_path) -> Pack {
    Pack p;
    fs::FsNativeSd sd;
    auto root = source_path;
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) {
        root.pop_back();
    }

    // Check if path is a .kefir-user.zip file
    std::string_view root_view{root};
    if (root_view.ends_with(".kefir-user.zip")) {
        if (!sd.FileExists(root.c_str())) {
            return p;
        }

        zlib_filefunc64_def file_func;
        mz::FileFuncStdio(&file_func);
        unzFile zf = unzOpen2_64(root.c_str(), &file_func);
        if (!zf) {
            return p;
        }
        ON_SCOPE_EXIT(unzClose(zf));

        unz_global_info64 ginfo{};
        if (UNZ_OK != unzGetGlobalInfo64(zf, &ginfo) || ginfo.number_entry == 0 || ginfo.number_entry > 200) {
            return p;
        }

        if (UNZ_OK != unzGoToFirstFile(zf)) {
            return p;
        }

        std::vector<u8> manifest_bytes;
        std::vector<u8> profile_bytes;
        std::vector<u8> baas_data;
        bool has_manifest = false;
        bool has_profile = false;
        bool has_avatar = false;
        bool has_playtime = false;

        constexpr u64 MAX_META_SIZE = 64 * 1024;

        for (u64 i = 0; i < ginfo.number_entry; i++) {
            if (i > 0) {
                if (UNZ_OK != unzGoToNextFile(zf)) {
                    return p;
                }
            }
            char filename[256]{};
            unz_file_info64 finfo{};
            if (UNZ_OK != unzGetCurrentFileInfo64(zf, &finfo, filename, sizeof(filename), nullptr, 0, nullptr, 0)) {
                return p;
            }
            std::string_view fname{filename};
            if (!sphaira::path::IsSafeArchiveEntry(fname)) {
                log_write("[USER] Unsafe archive entry in %s: %s\n", root.c_str(), filename);
                return p;
            }

            if (fname == "manifest.json") {
                if (finfo.uncompressed_size > MAX_META_SIZE) {
                    return p;
                }
                if (UNZ_OK == unzOpenCurrentFile(zf)) {
                    manifest_bytes.resize(finfo.uncompressed_size);
                    u64 total_read = 0;
                    bool read_ok = true;
                    while (total_read < finfo.uncompressed_size) {
                        int r = unzReadCurrentFile(zf, manifest_bytes.data() + total_read, static_cast<unsigned int>(finfo.uncompressed_size - total_read));
                        if (r <= 0) {
                            read_ok = false;
                            break;
                        }
                        total_read += r;
                    }
                    unzCloseCurrentFile(zf);
                    if (read_ok) {
                        has_manifest = true;
                    }
                }
            } else if (fname == "profile.json") {
                if (finfo.uncompressed_size > MAX_META_SIZE) {
                    return p;
                }
                if (UNZ_OK == unzOpenCurrentFile(zf)) {
                    profile_bytes.resize(finfo.uncompressed_size);
                    u64 total_read = 0;
                    bool read_ok = true;
                    while (total_read < finfo.uncompressed_size) {
                        int r = unzReadCurrentFile(zf, profile_bytes.data() + total_read, static_cast<unsigned int>(finfo.uncompressed_size - total_read));
                        if (r <= 0) {
                            read_ok = false;
                            break;
                        }
                        total_read += r;
                    }
                    unzCloseCurrentFile(zf);
                    if (read_ok) {
                        has_profile = true;
                    }
                }
            } else if (fname == "avatar.jpg") {
                has_avatar = true;
            } else if (fname == "pdm/PlayEvent.dat" || fname == "PlayEvent.dat") {
                has_playtime = true;
            } else if (fname.starts_with("baas/")) {
                if (baas_data.empty() && finfo.uncompressed_size >= 24 && finfo.uncompressed_size <= MAX_META_SIZE) {
                    if (UNZ_OK == unzOpenCurrentFile(zf)) {
                        baas_data.resize(finfo.uncompressed_size);
                        u64 total_read = 0;
                        while (total_read < finfo.uncompressed_size) {
                            int r = unzReadCurrentFile(zf, baas_data.data() + total_read, static_cast<unsigned int>(finfo.uncompressed_size - total_read));
                            if (r <= 0) {
                                baas_data.clear();
                                break;
                            }
                            total_read += r;
                        }
                        unzCloseCurrentFile(zf);
                    }
                }
            }
        }

        if (!has_manifest || !has_profile) {
            log_write("[USER] Archive %s missing manifest.json or profile.json\n", root.c_str());
            return p;
        }

        const std::string man_str(manifest_bytes.begin(), manifest_bytes.end());
        const std::string man_type = ReadJsonField(man_str, "type");
        const auto man_ver = ReadJsonIntField(man_str, "version");
        if (man_type != "account_backup" || !man_ver || *man_ver != 1) {
            log_write("[USER] Archive %s invalid manifest type='%s' version='%lld'\n",
                root.c_str(), man_type.c_str(), man_ver ? static_cast<long long>(*man_ver) : -1LL);
            return p;
        }

        const std::string prof_str(profile_bytes.begin(), profile_bytes.end());
        const std::string nickname = ReadJsonField(prof_str, "nickname");
        const std::string uid_hex = ReadJsonField(prof_str, "uid");
        if (nickname.empty() || uid_hex.empty()) {
            log_write("[USER] Archive %s profile.json missing nickname or uid\n", root.c_str());
            return p;
        }

        p.dir = root;
        p.is_archive = true;
        p.folder_name = root;
        if (const auto slash = p.folder_name.find_last_of("/\\"); slash != std::string::npos) {
            p.folder_name.erase(0, slash + 1);
        }

        p.nickname = nickname;
        p.uid_hex = uid_hex;
        p.created_label = FormatPackCreated(p.folder_name, ReadJsonField(prof_str, "created"));
        if (p.created_label.empty()) {
            p.created_label = FormatPackCreated(p.folder_name, ReadJsonField(man_str, "created"));
        }
        if (p.created_label.empty()) {
            p.created_label = FormatPackCreated(p.folder_name, {});
        }

        p.has_avatar = has_avatar;
        p.has_playtime = has_playtime;

        if (baas_data.size() >= 24) {
            u64 nas = 0;
            std::memcpy(&nas, baas_data.data() + 16, sizeof(u64));
            if (nas != 0) {
                p.nas_id = nas;
                p.link_valid = true;
            }
        }
        if (!p.link_valid) {
            const std::string man_link = ReadJsonField(man_str, "link_status");
            if (man_link == "linked") {
                p.link_valid = true;
            }
        }
        if (!p.link_valid) {
            const std::string prof_link = ReadJsonField(prof_str, "link_status");
            if (prof_link == "complete" || prof_link == "linked") {
                p.link_valid = true;
            }
        }

        return p;
    }

    // Legacy directory-based pack
    if (sd.FileExists((root + "/profile.json").c_str())) {
        p.dir = root;
    } else if (sd.FileExists((root + "/avatar.jpg").c_str()) &&
               (sd.DirExists((root + "/baas").c_str()) || sd.DirExists((root + "/nas").c_str()))) {
        p.dir = root;
    }
    if (p.dir.empty()) {
        return p;
    }

    p.is_archive = false;
    p.folder_name = p.dir;
    while (!p.folder_name.empty() && (p.folder_name.back() == '/' || p.folder_name.back() == '\\')) {
        p.folder_name.pop_back();
    }
    if (const auto slash = p.folder_name.find_last_of("/\\"); slash != std::string::npos) {
        p.folder_name.erase(0, slash + 1);
    }

    std::vector<u8> json;
    if (R_SUCCEEDED(sd.read_entire_file((p.dir + "/profile.json").c_str(), json))) {
        const std::string s(json.begin(), json.end());
        p.nickname = ReadJsonField(s, "nickname");
        p.uid_hex = ReadJsonField(s, "uid");
        p.created_label = FormatPackCreated(p.folder_name, ReadJsonField(s, "created"));
    }
    if (p.created_label.empty()) {
        p.created_label = FormatPackCreated(p.folder_name, {});
    }
    if (p.nickname.empty()) {
        p.nickname = "User";
    }

    p.has_avatar = sd.FileExists((p.dir + "/avatar.jpg").c_str());
    p.has_playtime = account_playtime::PackHasPlayEvents(p.dir);

    account_link::LinkPackage pkg;
    if (R_SUCCEEDED(account_link::LoadUserPackLinkPackage(p.dir, pkg))) {
        p.link_valid = true;
        p.nas_id = pkg.nas_id;
    }

    return p;
}

auto ListUserPacks(const std::string& root) -> std::vector<Pack> {
    std::vector<Pack> packs;
    if (root.empty()) {
        return packs;
    }
    auto base = root;
    while (base.size() > 1 && (base.back() == '/' || base.back() == '\\')) {
        base.pop_back();
    }
    const auto single = FindUserPack(base);
    if (!single.dir.empty()) {
        packs.push_back(single);
        return packs;
    }
    fs::FsNativeSd sd;
    if (!sd.DirExists(base.c_str())) {
        return packs;
    }
    fs::Dir d;
    if (R_FAILED(sd.OpenDirectory(base.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) {
        return packs;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(d.ReadAll(entries))) {
        return packs;
    }
    for (const auto& e : entries) {
        if (e.type == FsDirEntryType_Dir) {
            const auto pack_path = base + "/" + e.name;
            const auto pack = FindUserPack(pack_path);
            if (!pack.dir.empty()) {
                packs.push_back(pack);
            }
        } else if (e.type == FsDirEntryType_File) {
            std::string_view name_view{e.name};
            if (name_view.ends_with(".kefir-user.zip")) {
                const auto pack_path = base + "/" + e.name;
                const auto pack = FindUserPack(pack_path);
                if (!pack.dir.empty()) {
                    packs.push_back(pack);
                }
            }
        }
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) {
        return a.folder_name > b.folder_name;
    });
    return packs;
}

auto ListUserPacks() -> std::vector<Pack> {
    EnsureRootsMigrated();
    const auto new_root = paths::DATA_ROOT + "/account_backups";
    const auto old_root = paths::DATA_ROOT + "/user_packs";
    auto packs = ListUserPacks(new_root);
    fs::FsNativeSd sd;
    if (sd.DirExists(old_root.c_str())) {
        auto legacy_packs = ListUserPacks(old_root);
        for (auto& lp : legacy_packs) {
            if (std::ranges::none_of(packs, [&](const Pack& p) { return p.dir == lp.dir; })) {
                packs.push_back(std::move(lp));
            }
        }
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) {
        return a.folder_name > b.folder_name;
    });
    return packs;
}

auto DeleteUserPack(const std::string& dir) -> Result {
    const auto root_new = paths::DATA_ROOT + "/account_backups/";
    const auto root_old = paths::DATA_ROOT + "/user_packs/";
    R_UNLESS(!dir.empty() && (dir.find(root_new) == 0 || dir.find(root_old) == 0), Result_FsInvalidType);
    fs::FsNativeSd sd;
    if (sd.FileExists(dir.c_str())) {
        R_TRY(sd.DeleteFile(dir.c_str()));
    } else if (sd.DirExists(dir.c_str())) {
        R_TRY(sd.DeleteDirectoryRecursively(dir.c_str()));
    } else {
        return Result_FsInvalidType;
    }
    log_write("[USER] deleted pack %s\n", dir.c_str());
    R_SUCCEED();
}

auto GetUserPacksRoot() -> std::string {
    EnsureRootsMigrated();
    return paths::DATA_ROOT + "/account_backups";
}

auto GetShareableUserBackupRoots() -> std::vector<std::string> {
    EnsureRootsMigrated();
    std::vector<std::string> roots;
    const auto new_root = paths::DATA_ROOT + "/account_backups";
    const auto old_root = paths::DATA_ROOT + "/user_packs";
    roots.push_back(new_root);
    fs::FsNativeSd sd;
    if (sd.DirExists(old_root.c_str())) {
        roots.push_back(old_root);
    }
    return roots;
}

} // namespace sphaira::account_user
