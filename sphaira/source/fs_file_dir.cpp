#include "fs.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace fs {

Result OpenFile(fs::Fs* fs, const fs::FsPath& path, u32 mode, File* f) {
    f->m_fs = fs;
    f->m_mode = mode;

    if (f->m_fs->IsNative()) {
        auto nfs = (fs::FsNative*)f->m_fs;
        R_TRY(fsFsOpenFile(&nfs->m_fs, path, mode, &f->m_native));
    } else if (f->m_fs->IsVirtual()) {
        R_TRY(f->m_fs->vOpenFile(path, mode, f));
    } else {
        if ((mode & FsOpenMode_Read) && (mode & FsOpenMode_Write)) {
            f->m_stdio = std::fopen(path, "rb+");
        } else if (mode & FsOpenMode_Read) {
            f->m_stdio = std::fopen(path, "rb");
        } else if (mode & FsOpenMode_Write) {
            // not possible to open file with just write and not append
            // or create or truncate. So rw it is!
            f->m_stdio = std::fopen(path, "rb+");
        }

        R_UNLESS(f->m_stdio, Result_FsUnknownStdioError);

        // stdio mounts here are devoptab-backed (mtp / network / usb drive)
        // and pay a full round trip per read call. newlib ignores st_blksize
        // and hands out a 1 KiB buffer, which splits every large read into a
        // tiny refill plus a direct read -- twice the transactions.
        std::setvbuf(f->m_stdio, nullptr, _IOFBF, 1024 * 512);
    }

    R_SUCCEED();
}

File::~File() {
    Close();
}

Result File::Read( s64 off, void* buf, u64 read_size, u32 option, u64* bytes_read) {
    *bytes_read = 0;
    R_UNLESS(m_fs, Result_FsNotActive);

    if (m_fs->IsNative()) {
        R_TRY(fsFileRead(&m_native, off, buf, read_size, option, bytes_read));
    } else if (m_fs->IsVirtual()) {
        R_TRY(m_fs->vReadFile(this, off, buf, read_size, option, bytes_read));
    } else {
        if (m_stdio_off != off) {
            m_stdio_off = off;
            std::fseek(m_stdio, off, SEEK_SET);
        }

        *bytes_read = std::fread(buf, 1, read_size, m_stdio);

        // if we read less bytes than expected, check if there was an error (ignoring eof).
        if (*bytes_read < read_size) {
            if (!std::feof(m_stdio) && std::ferror(m_stdio)) {
                R_THROW(Result_FsUnknownStdioError);
            }
        }

        m_stdio_off += *bytes_read;
    }

    R_SUCCEED();
}

Result File::Write(s64 off, const void* buf, u64 write_size, u32 option) {
    R_UNLESS(m_fs, Result_FsNotActive);

    if (m_fs->IsNative()) {
        R_TRY(fsFileWrite(&m_native, off, buf, write_size, option));
    } else if (m_fs->IsVirtual()) {
        R_THROW(FsError_NotImplemented); // virtual filesystems are read-only.
    } else {
        if (m_stdio_off != off) {
            log_write("[FS] diff seek\n");
            m_stdio_off = off;
            std::fseek(m_stdio, off, SEEK_SET);
        }

        const auto result = std::fwrite(buf, 1, write_size, m_stdio);
        // log_write("[FS] fwrite res: %zu vs %zu\n", result, write_size);
        R_UNLESS(result == write_size, Result_FsUnknownStdioError);

        m_stdio_off += write_size;
    }

    R_SUCCEED();
}

Result File::SetSize(s64 sz) {
    R_UNLESS(m_fs, Result_FsNotActive);

    if (m_fs->IsNative()) {
        R_TRY(fsFileSetSize(&m_native, sz));
    } else if (m_fs->IsVirtual()) {
        R_THROW(FsError_NotImplemented); // virtual filesystems are read-only.
    } else {
        const auto fd = fileno(m_stdio);
        R_UNLESS(fd > 0, Result_FsUnknownStdioError);
        R_UNLESS(!ftruncate(fd, sz), Result_FsUnknownStdioError);
    }

    R_SUCCEED();
}

Result File::GetSize(s64* out) {
    R_UNLESS(m_fs, Result_FsNotActive);

    if (m_fs->IsNative()) {
        R_TRY(fsFileGetSize(&m_native, out));
    } else if (m_fs->IsVirtual()) {
        R_TRY(m_fs->vGetFileSize(this, out));
    } else {
        struct stat st;
        R_UNLESS(!fstat(fileno(m_stdio), &st), Result_FsUnknownStdioError);
        *out = st.st_size;
    }

    R_SUCCEED();
}

void File::Close() {
    // Close a native handle without asking the parent Fs first. File viewer
    // stores a raw Fs* into the file browser; if that browser is already
    // gone, IsNative() is a virtual call into freed memory.
    if (serviceIsActive(&m_native.s)) {
        fsFileClose(&m_native);
        m_native = {};
        if (m_fs && (m_mode & FsOpenMode_Write)) {
            m_fs->Commit();
        }
        m_fs = {};
        return;
    }

    if (!m_fs) {
        if (m_stdio) {
            std::fclose(m_stdio);
            m_stdio = {};
        }
        return;
    }

    if (m_fs->IsVirtual()) {
        m_fs->vCloseFile(this);
    } else if (m_stdio) {
        std::fclose(m_stdio);
        m_stdio = {};
    }
    m_fs = {};
}

namespace {

// newlib fills d_type from the st_mode that the devoptab driver reports in its
// dirnext(), so a driver that leaves st_mode blank hands back DT_UNKNOWN and
// the entry would be dropped from the listing entirely. Fall back to a stat()
// on the full device qualified path in that case; the extra call also brings
// back the size, which readdir() has nowhere to put.
u8 ClassifyStdioEntry(const fs::FsPath& dir_path, const struct dirent* d, s64* out_size) {
    if (out_size) {
        *out_size = 0;
    }

    if (d->d_type == DT_DIR || d->d_type == DT_REG) {
        return d->d_type;
    }

    fs::FsPath full_path;
    std::snprintf(full_path, sizeof(full_path), "%s%s%s",
        dir_path.s,
        (dir_path.size() && dir_path.s[dir_path.size() - 1] != '/') ? "/" : "",
        d->d_name);

    struct stat st{};
    if (stat(full_path, &st)) {
        return DT_UNKNOWN;
    }

    if (S_ISDIR(st.st_mode)) {
        return DT_DIR;
    }

    if (out_size) {
        *out_size = st.st_size;
    }
    return DT_REG;
}

} // namespace

Result OpenDirectory(fs::Fs* fs, const fs::FsPath& path, u32 mode, Dir* d) {
    d->m_fs = fs;
    d->m_mode = mode;

    if (d->m_fs->IsNative()) {
        auto nfs = (fs::FsNative*)d->m_fs;
        R_TRY(fsFsOpenDirectory(&nfs->m_fs, path, mode, &d->m_native));
    } else if (d->m_fs->IsVirtual()) {
        R_TRY(d->m_fs->vOpenDir(path, mode, d));
    } else {
        d->m_stdio = opendir(path);
        R_UNLESS(d->m_stdio, Result_FsUnknownStdioError);
        d->m_path = path;
    }

    R_SUCCEED();
}

Result DirGetEntryCount(fs::Fs* m_fs, const fs::FsPath& path, s64* count, u32 mode) {
    s64 file_count, dir_count;
    R_TRY(DirGetEntryCount(m_fs, path, &file_count, &dir_count, mode));
    *count = file_count + dir_count;
    R_SUCCEED();
}

Result DirGetEntryCount(fs::Fs* m_fs, const fs::FsPath& path, s64* file_count, s64* dir_count, u32 mode) {
    *file_count = *dir_count = 0;

    if (m_fs->IsNative() || m_fs->IsVirtual()) {
        if (mode & FsDirOpenMode_ReadDirs){
            fs::Dir dir;
            R_TRY(m_fs->OpenDirectory(path, FsDirOpenMode_ReadDirs|FsDirOpenMode_NoFileSize, &dir));
            R_TRY(dir.GetEntryCount(dir_count));
        }
        if (mode & FsDirOpenMode_ReadFiles){
            fs::Dir dir;
            R_TRY(m_fs->OpenDirectory(path, FsDirOpenMode_ReadFiles|FsDirOpenMode_NoFileSize, &dir));
            R_TRY(dir.GetEntryCount(file_count));
        }
    } else {
        fs::Dir dir;
        R_TRY(m_fs->OpenDirectory(path, mode, &dir));

        while (auto d = readdir(dir.m_stdio)) {
            if (!std::strcmp(d->d_name, ".") || !std::strcmp(d->d_name, "..")) {
                continue;
            }

            const auto entry_type = ClassifyStdioEntry(path, d, nullptr);

            if (entry_type == DT_DIR) {
                if (!(mode & FsDirOpenMode_ReadDirs)) {
                    continue;
                }
                (*dir_count)++;
            } else if (entry_type == DT_REG) {
                if (!(mode & FsDirOpenMode_ReadFiles)) {
                    continue;
                }
                (*file_count)++;
            }
        }
    }

    R_SUCCEED();
}

Dir::~Dir() {
    Close();
}

Result Dir::GetEntryCount(s64* out) {
    *out = 0;
    R_UNLESS(m_fs, Result_FsNotActive);

    if (m_fs->IsNative()) {
        R_TRY(fsDirGetEntryCount(&m_native, out));
    } else if (m_fs->IsVirtual()) {
        R_TRY(m_fs->vReadDirCount(this, out));
    } else {
        while (auto d = readdir(m_stdio)) {
            if (!std::strcmp(d->d_name, ".") || !std::strcmp(d->d_name, "..")) {
                continue;
            }
            (*out)++;
        }

        // NOTE: this will *not* work for native mounted folders!!!
        rewinddir(m_stdio);
    }

    R_SUCCEED();
}

Result Dir::Read(s64 *total_entries, size_t max_entries, FsDirectoryEntry *buf) {
    R_UNLESS(m_fs, Result_FsNotActive);
    *total_entries = 0;

    if (m_fs->IsNative()) {
        R_TRY(fsDirRead(&m_native, total_entries, max_entries, buf));
    } else if (m_fs->IsVirtual()) {
        R_TRY(m_fs->vReadDir(this, total_entries, max_entries, buf));
    } else {
        while (auto d = readdir(m_stdio)) {
            if (!std::strcmp(d->d_name, ".") || !std::strcmp(d->d_name, "..")) {
                continue;
            }

            FsDirectoryEntry entry{};
            s64 stat_size{};
            const auto entry_type = ClassifyStdioEntry(m_path, d, &stat_size);

            if (entry_type == DT_DIR) {
                if (!(m_mode & FsDirOpenMode_ReadDirs)) {
                    continue;
                }
                entry.type = FsDirEntryType_Dir;
            } else if (entry_type == DT_REG) {
                if (!(m_mode & FsDirOpenMode_ReadFiles)) {
                    continue;
                }
                entry.type = FsDirEntryType_File;
                entry.file_size = stat_size;
            } else {
                log_write("[FS] WARNING: unknown type when reading dir: %u for '%s'\n", d->d_type, d->d_name);
                continue;
            }

            std::strcpy(entry.name, d->d_name);
            std::memcpy(&buf[*total_entries], &entry, sizeof(*buf));
            *total_entries = *total_entries + 1;
            if (*total_entries >= max_entries) {
                break;
            }
        }
    }

    R_SUCCEED();
}

Result Dir::ReadAll(std::vector<FsDirectoryEntry>& buf) {
    buf.clear();
    R_UNLESS(m_fs, Result_FsNotActive);

    if (m_fs->IsNative()) {
        s64 count;
        R_TRY(GetEntryCount(&count));

        buf.resize(count);
        R_TRY(fsDirRead(&m_native, &count, buf.size(), buf.data()));
        buf.resize(count);
    } else if (m_fs->IsVirtual()) {
        s64 count{};
        R_TRY(m_fs->vReadDirCount(this, &count));
        buf.resize(count);
        if (count) {
            s64 read{};
            R_TRY(m_fs->vReadDir(this, &read, buf.size(), buf.data()));
            buf.resize(read);
        }
    } else {
        buf.reserve(1000);

        while (auto d = readdir(m_stdio)) {
            if (!std::strcmp(d->d_name, ".") || !std::strcmp(d->d_name, "..")) {
                continue;
            }

            FsDirectoryEntry entry{};
            s64 stat_size{};
            const auto entry_type = ClassifyStdioEntry(m_path, d, &stat_size);

            if (entry_type == DT_DIR) {
                if (!(m_mode & FsDirOpenMode_ReadDirs)) {
                    continue;
                }
                entry.type = FsDirEntryType_Dir;
            } else if (entry_type == DT_REG) {
                if (!(m_mode & FsDirOpenMode_ReadFiles)) {
                    continue;
                }
                entry.type = FsDirEntryType_File;
                entry.file_size = stat_size;
            } else {
                log_write("[FS] WARNING: unknown d_type when reading dir: %u name='%s'\n", d->d_type, d->d_name);
                continue;
            }

            std::strcpy(entry.name, d->d_name);
            buf.emplace_back(entry);
        }
    }

    R_SUCCEED();
}

void Dir::Close() {
    if (!m_fs) {
        return;
    }

    if (m_fs->IsNative()) {
        if (serviceIsActive(&m_native.s)) {
            fsDirClose(&m_native);
            m_native = {};
        }
    } else if (m_fs->IsVirtual()) {
        m_fs->vCloseDir(this);
    } else {
        if (m_stdio) {
            closedir(m_stdio);
            m_stdio = {};
        }
    }
}

} // namespace fs
