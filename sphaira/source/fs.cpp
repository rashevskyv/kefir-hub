#include "fs.hpp"
#include "defines.hpp"
#include "ui/nvg_util.hpp"
#include "log.hpp"

#include <switch.h>
#include <sys/statvfs.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string_view>
#include <algorithm>
#include <ranges>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ftw.h>

namespace fs {
namespace {

// these folders and internals cannot be modified
constexpr std::string_view READONLY_ROOT_FOLDERS[]{
    "/atmosphere/automatic_backups",

    "/bootloader/res",
    "/bootloader/sys",

    "/backup", // some people never back this up...

    "/Nintendo/Contents",
    "/Nintendo/save",

    "/emuMMC", // emunand
    "/warmboot_mariko",
};

// these files and folders cannot be modified
constexpr std::string_view READONLY_FILES[]{
    "/", // don't allow deleting root

    "/atmosphere", // don't allow deleting all of /atmosphere
    "/atmosphere/hbl.nsp",
    "/atmosphere/package3",
    "/atmosphere/reboot_payload.bin",
    "/atmosphere/stratosphere.romfs",

    "/bootloader", // don't allow deleting all of /bootloader
    "/bootloader/hekate_ipl.ini",

    "/switch", // don't allow deleting all of /switch
    "/hbmenu.nro", // breaks hbl
    "/payload.bin", // some modchips need this

    "/boot.dat", // sxos
    "/license.dat", // sxos

    "/switch/prod.keys",
    "/switch/title.keys",
    "/switch/reboot_to_payload.nro",
};

bool is_read_only_root(std::string_view path) {
    for (auto p : READONLY_ROOT_FOLDERS) {
        if (path.starts_with(p)) {
            return true;
        }
    }

    return false;
}

bool is_read_only_file(std::string_view path) {
    for (auto p : READONLY_FILES) {
        if (path == p) {
            return true;
        }
    }

    return false;
}

} // namespace

bool is_read_only(std::string_view path) {
    if (is_read_only_root(path)) {
        return true;
    }
    if (is_read_only_file(path)) {
        return true;
    }
    return false;
}

FsPath AppendPath(const FsPath& root_path, const FsPath& _file_path) {
    // strip leading '/' in file path.
    auto file_path = _file_path.s;
    while (file_path[0] == '/') {
        file_path++;
    }

    FsPath path;
    if (root_path[std::strlen(root_path) - 1] != '/') {
        std::snprintf(path, sizeof(path), "%s/%s", root_path.s, file_path);
    } else {
        std::snprintf(path, sizeof(path), "%s%s", root_path.s, file_path);
    }
    return path;
}

Result CreateFile(FsFileSystem* fs, const FsPath& path, u64 size, u32 option, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only_root(path), Result_FsReadOnly);

    if (size >= 1024ULL*1024ULL*1024ULL*4ULL) {
        option |= FsCreateOption_BigFile;
    }

    R_TRY(fsFsCreateFile(fs, path, size, option));
    return fsFsCommit(fs);
}

Result CreateDirectory(FsFileSystem* fs, const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only_root(path), Result_FsReadOnly);

    R_TRY(fsFsCreateDirectory(fs, path));
    return fsFsCommit(fs);
}

Result CreateDirectoryRecursively(FsFileSystem* fs, const FsPath& _path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only_root(_path), Result_FsReadOnly);

    // try and create the directory / see if it already exists before the loop.
    Result rc;
    if (fs) {
        rc = CreateDirectory(fs, _path, ignore_read_only);
    } else {
        rc = CreateDirectory(_path, ignore_read_only);
    }

    if (R_SUCCEEDED(rc) || rc == FsError_PathAlreadyExists) {
        R_SUCCEED();
    }

    auto path_view = std::string_view{_path};
    // todo: fix this for sdmc: and ums0:
    FsPath path{"/"};
    if (auto s = std::strchr(_path.s, ':')) {
        const int len = (s - _path.s) + 1;
        std::snprintf(path, sizeof(path), "%.*s/", len, _path.s);
        path_view = path_view.substr(len);
    }

    for (const auto dir : std::views::split(path_view, '/')) {
        if (dir.empty()) {
            continue;
        }
        path += std::string_view{dir.data(), dir.size()}; // bounded by FsPath
        log_write("[FS] dir creation path is now: %s\n", path.s);

        if (fs) {
            rc = CreateDirectory(fs, path, ignore_read_only);
        } else {
            rc = CreateDirectory(path, ignore_read_only);
        }

        if (R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
            log_write("failed to create folder: %s\n", path.s);
            return rc;
        }

        // log_write("created_directory: %s\n", path);
        std::strncat(path, "/", sizeof(path) - std::strlen(path) - 1);
    }
    R_SUCCEED();
}

Result CreateDirectoryRecursivelyWithPath(FsFileSystem* fs, const FsPath& _path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only_root(_path), Result_FsReadOnly);

    // strip file name form path.
    const auto last_slash = std::strrchr(_path, '/');
    if (!last_slash) {
        R_SUCCEED();
    }

    FsPath new_path{};
    std::snprintf(new_path, sizeof(new_path), "%.*s", (int)(last_slash - _path.s), _path.s);
    R_TRY(CreateDirectoryRecursively(fs, new_path, ignore_read_only));
    R_SUCCEED();
}

Result DeleteFile(FsFileSystem* fs, const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(path), Result_FsReadOnly);
    R_TRY(fsFsDeleteFile(fs, path));
    return fsFsCommit(fs);
}

Result DeleteDirectory(FsFileSystem* fs, const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(path), Result_FsReadOnly);

    R_TRY(fsFsDeleteDirectory(fs, path));
    return fsFsCommit(fs);
}

Result DeleteDirectoryRecursively(FsFileSystem* fs, const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(path), Result_FsReadOnly);

    R_TRY(fsFsDeleteDirectoryRecursively(fs, path));
    return fsFsCommit(fs);
}

Result RenameFile(FsFileSystem* fs, const FsPath& src, const FsPath& dst, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(src), Result_FsReadOnly);
    R_UNLESS(ignore_read_only || !is_read_only(dst), Result_FsReadOnly);

    R_TRY(fsFsRenameFile(fs, src, dst));
    return fsFsCommit(fs);
}

Result RenameDirectory(FsFileSystem* fs, const FsPath& src, const FsPath& dst, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(src), Result_FsReadOnly);
    R_UNLESS(ignore_read_only || !is_read_only(dst), Result_FsReadOnly);

    R_TRY(fsFsRenameDirectory(fs, src, dst));
    return fsFsCommit(fs);
}

Result GetEntryType(FsFileSystem* fs, const FsPath& path, FsDirEntryType* out) {
    return fsFsGetEntryType(fs, path, out);
}

Result GetFileTimeStampRaw(FsFileSystem* fs, const FsPath& path, FsTimeStampRaw *out) {
    return fsFsGetFileTimeStampRaw(fs, path, out);
}

Result SetTimestamp(FsFileSystem* fs, const FsPath& path, const FsTimeStampRaw* ts) {
    // unsuported.
    R_SUCCEED();
}

bool FileExists(FsFileSystem* fs, const FsPath& path) {
    FsDirEntryType type;
    R_TRY_RESULT(GetEntryType(fs, path, &type), false);
    return type == FsDirEntryType_File;
}

bool DirExists(FsFileSystem* fs, const FsPath& path) {
    FsDirEntryType type;
    R_TRY_RESULT(GetEntryType(fs, path, &type), false);
    return type == FsDirEntryType_Dir;
}

Result read_entire_file(FsFileSystem* _fs, const FsPath& path, std::vector<u8>& out) {
    FsNative fs{_fs, false};
    R_TRY(fs.GetFsOpenResult());

    File f;
    R_TRY(fs.OpenFile(path, FsOpenMode_Read, &f));

    s64 size;
    R_TRY(f.GetSize(&size));
    out.resize(size);

    u64 bytes_read;
    R_TRY(f.Read(0, out.data(), out.size(), FsReadOption_None, &bytes_read));
    R_UNLESS(bytes_read == out.size(), 1);

    R_SUCCEED();
}

Result write_entire_file(FsFileSystem* _fs, const FsPath& path, const std::vector<u8>& in, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(path), Result_FsReadOnly);

    FsNative fs{_fs, false, ignore_read_only};
    R_TRY(fs.GetFsOpenResult());

    if (auto rc = fs.CreateFile(path, in.size(), 0); R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
        return rc;
    }

    File f;
    R_TRY(fs.OpenFile(path, FsOpenMode_Write, &f));
    R_TRY(f.SetSize(in.size()));
    R_TRY(f.Write(0, in.data(), in.size(), FsWriteOption_None));

    R_SUCCEED();
}

Result copy_entire_file(FsFileSystem* fs, const FsPath& dst, const FsPath& src, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(dst), Result_FsReadOnly);

    std::vector<u8> data;
    R_TRY(read_entire_file(fs, src, data));
    return write_entire_file(fs, dst, data, ignore_read_only);
}

Result CreateFile(const FsPath& path, u64 size, u32 option, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only_root(path), Result_FsReadOnly);

    auto fd = open(path, O_WRONLY | O_CREAT, DEFFILEMODE);
    if (fd == -1) {
        if (errno == EEXIST) {
            return FsError_PathAlreadyExists;
        }

        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    ON_SCOPE_EXIT(close(fd));

    if (size) {
        R_UNLESS(!ftruncate(fd, size), Result_FsUnknownStdioError);
    }

    R_SUCCEED();
}

Result CreateDirectory(const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only_root(path), Result_FsReadOnly);

    if (mkdir(path, ACCESSPERMS)) {
        if (errno == EEXIST) {
            return FsError_PathAlreadyExists;
        }

        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    R_SUCCEED();
}

Result CreateDirectoryRecursively(const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only_root(path), Result_FsReadOnly);

    return CreateDirectoryRecursively(nullptr, path, ignore_read_only);
}

Result CreateDirectoryRecursivelyWithPath(const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only_root(path), Result_FsReadOnly);

    return CreateDirectoryRecursivelyWithPath(nullptr, path, ignore_read_only);
}

Result DeleteFile(const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(path), Result_FsReadOnly);

    if (unlink(path)) {
        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    R_SUCCEED();
}

Result DeleteDirectory(const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(path), Result_FsReadOnly);

    if (rmdir(path)) {
        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    R_SUCCEED();
}

// ftw / ntfw isn't found by linker...
Result DeleteDirectoryRecursively(const FsPath& path, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(path), Result_FsReadOnly);

    #if 0
    // const auto unlink_cb = [](const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) -> int {
    const auto unlink_cb = [](const char *fpath, const struct stat *sb, int typeflag) -> int {
        return remove(fpath);
    };
    // todo: check for reasonable max fd limit
    // if (nftw(path, unlink_cb, 16, FTW_DEPTH)) {
    if (ftw(path, unlink_cb, 16)) {
        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    R_SUCCEED();
    #else
    R_THROW(0xFFFF);
    #endif
}

Result RenameFile(const FsPath& src, const FsPath& dst, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(src), Result_FsReadOnly);
    R_UNLESS(ignore_read_only || !is_read_only(dst), Result_FsReadOnly);

    if (rename(src, dst)) {
        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    R_SUCCEED();
}

Result RenameDirectory(const FsPath& src, const FsPath& dst, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(src), Result_FsReadOnly);
    R_UNLESS(ignore_read_only || !is_read_only(dst), Result_FsReadOnly);

    return RenameFile(src, dst, ignore_read_only);
}

Result GetEntryType(const FsPath& path, FsDirEntryType* out) {
    struct stat st;
    if (stat(path, &st)) {
        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    *out = S_ISREG(st.st_mode) ? FsDirEntryType_File : FsDirEntryType_Dir;
    R_SUCCEED();
}

Result GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw *out) {
    struct stat st;
    if (stat(path, &st)) {
        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }

    out->is_valid = true;
    out->created = st.st_ctim.tv_sec;
    out->modified = st.st_mtim.tv_sec;
    out->accessed = st.st_atim.tv_sec;
    R_SUCCEED();
}

Result SetTimestamp(const FsPath& path, const FsTimeStampRaw* ts) {
    if (ts->is_valid) {
        timeval val[2]{};
        val[0].tv_sec = ts->accessed;
        val[1].tv_sec = ts->modified;

        if (utimes(path, val)) {
            log_write("utimes() failed: %d %s\n", errno, strerror(errno));
        }
    }

    R_SUCCEED();
}

bool FileExists(const FsPath& path) {
    FsDirEntryType type;
    R_TRY_RESULT(GetEntryType(path, &type), false);
    return type == FsDirEntryType_File;
}

bool DirExists(const FsPath& path) {
    FsDirEntryType type;
    R_TRY_RESULT(GetEntryType(path, &type), false);
    return type == FsDirEntryType_Dir;
}

Result read_entire_file(const FsPath& path, std::vector<u8>& out) {
    auto f = std::fopen(path, "rb");
    if (!f) {
        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    ON_SCOPE_EXIT(std::fclose(f));

    std::fseek(f, 0, SEEK_END);
    const auto size = std::ftell(f);
    std::rewind(f);

    out.resize(size);

    std::fread(out.data(), 1, out.size(), f);
    R_SUCCEED();
}

Result write_entire_file(const FsPath& path, const std::vector<u8>& in, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(path), Result_FsReadOnly);

    auto f = std::fopen(path, "wb");
    if (!f) {
        R_TRY(fsdevGetLastResult());
        return Result_FsUnknownStdioError;
    }
    ON_SCOPE_EXIT(std::fclose(f));

    std::fwrite(in.data(), 1, in.size(), f);
    R_SUCCEED();
}

Result copy_entire_file(const FsPath& dst, const FsPath& src, bool ignore_read_only) {
    R_UNLESS(ignore_read_only || !is_read_only(dst), Result_FsReadOnly);

    std::vector<u8> data;
    R_TRY(read_entire_file(src, data));
    return write_entire_file(dst, data, ignore_read_only);
}

Result FileGetSizeAndTimestamp(fs::Fs* m_fs, const FsPath& path, FsTimeStampRaw* ts, s64* size) {
    *ts = {};
    *size = {};

    if (m_fs->IsNative()) {
        auto fs = (fs::FsNative*)m_fs;
        R_TRY(fs->GetFileTimeStampRaw(path, ts));

        File f;
        R_TRY(m_fs->OpenFile(path, FsOpenMode_Read, &f));
        R_TRY(f.GetSize(size));
    } else if (m_fs->IsVirtual()) {
        R_TRY(m_fs->vStat(path, size, ts));
    } else {
        struct stat st;
        R_UNLESS(!lstat(path, &st), Result_FsFailedStdioStat);

        ts->is_valid = true;
        ts->created = st.st_ctim.tv_sec;
        ts->modified = st.st_mtim.tv_sec;
        ts->accessed = st.st_atim.tv_sec;
        *size = st.st_size;
    }

    R_SUCCEED();
}

Result IsDirEmpty(fs::Fs* m_fs, const fs::FsPath& path, bool* out) {
    *out = true;

    if (m_fs->IsNative() || m_fs->IsVirtual()) {
        s64 count;
        R_TRY(m_fs->DirGetEntryCount(path, &count, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles));
        *out = !count;
    } else {
        auto dir = opendir(path);
        R_UNLESS(dir, Result_FsFailedStdioOpendir);
        ON_SCOPE_EXIT(closedir(dir));

        while (auto d = readdir(dir)) {
            if (!std::strcmp(d->d_name, ".") || !std::strcmp(d->d_name, "..")) {
                continue;
            }

            *out = false;
            break;
        }
    }

    R_SUCCEED();
}

void GetStorageSpaces(s64* nand_free, s64* nand_total, s64* sd_free, s64* sd_total) {
    if (nand_free || nand_total) {
        FsFileSystem nand_fs;
        if (R_SUCCEEDED(fsOpenBisFileSystem(&nand_fs, FsBisPartitionId_User, ""))) {
            if (nand_free) {
                fsFsGetFreeSpace(&nand_fs, "/", nand_free);
            }
            if (nand_total) {
                fsFsGetTotalSpace(&nand_fs, "/", nand_total);
            }
            fsFsClose(&nand_fs);
        } else {
            if (nand_free) *nand_free = 0;
            if (nand_total) *nand_total = 0;
        }
    }
    if (sd_free || sd_total) {
        struct statvfs st{};
        if (statvfs("sdmc:/", &st) == 0) {
            if (sd_free) {
                *sd_free = (s64)st.f_bfree * (s64)st.f_bsize;
            }
            if (sd_total) {
                *sd_total = (s64)st.f_blocks * (s64)st.f_frsize;
            }
        } else {
            if (sd_free) *sd_free = 0;
            if (sd_total) *sd_total = 0;
        }
    }
}

} // namespace fs
