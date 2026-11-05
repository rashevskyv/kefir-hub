#include "fs_zip.hpp"
#include "minizip_helper.hpp"
#include "log.hpp"

#include <minizip/unzip.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

namespace fs {
namespace {

// per-open-file state: the fully decompressed entry, served from memory.
struct ZipOpenFile {
    std::vector<u8> data;
};

auto MakeTimestamp(const unz_file_info64& info) -> FsTimeStampRaw {
    std::tm tm{};
    tm.tm_sec = info.tmu_date.tm_sec;
    tm.tm_min = info.tmu_date.tm_min;
    tm.tm_hour = info.tmu_date.tm_hour;
    tm.tm_mday = info.tmu_date.tm_mday;
    tm.tm_mon = info.tmu_date.tm_mon;
    tm.tm_year = static_cast<int>(info.tmu_date.tm_year) - 1900; // tmu_date holds the full year
    tm.tm_isdst = -1;

    FsTimeStampRaw ts{};
    const auto t = std::mktime(&tm);
    if (t != static_cast<std::time_t>(-1)) {
        ts.is_valid = true;
        ts.created = ts.modified = ts.accessed = static_cast<u64>(t);
    }
    return ts;
}

} // namespace

FsZip::FsZip(const FsPath& zip_path)
: m_zip_path{zip_path.s} {
    mutexInit(&m_mutex);

    zlib_filefunc64_def file_func;
    sphaira::mz::FileFuncStdio(&file_func);
    m_zip = unzOpen2_64(m_zip_path.c_str(), &file_func);
    if (!m_zip) {
        log_write("[FS-ZIP] failed to open %s\n", m_zip_path.c_str());
        m_open_result = FsError_PathNotFound;
        return;
    }

    m_open_result = BuildIndex();
}

FsZip::~FsZip() {
    if (m_zip) {
        unzClose(static_cast<unzFile>(m_zip));
        m_zip = nullptr;
    }
}

Result FsZip::BuildIndex() {
    auto zf = static_cast<unzFile>(m_zip);

    unz_global_info64 ginfo;
    R_UNLESS(UNZ_OK == unzGetGlobalInfo64(zf, &ginfo), FsError_PathNotFound);

    AddDir("/"); // root always exists, even for an empty archive.

    if (UNZ_OK != unzGoToFirstFile(zf)) {
        R_SUCCEED();
    }

    do {
        unz_file_info64 info;
        char name_buf[1024];
        if (UNZ_OK != unzGetCurrentFileInfo64(zf, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            log_write("[FS-ZIP] failed to read entry info, skipping\n");
            continue;
        }
        name_buf[sizeof(name_buf) - 1] = '\0';

        const auto len = std::strlen(name_buf);
        const bool is_dir = len && (name_buf[len - 1] == '/' || name_buf[len - 1] == '\\');

        const auto key = Normalize(name_buf);
        if (key == "/") {
            continue;
        }

        if (is_dir) {
            AddDir(key);
        } else {
            unz64_file_pos pos{};
            unzGetFilePos64(zf, &pos);

            m_pos[key] = ZipPos{pos.pos_in_zip_directory, pos.num_of_file};
            AddFile(key, static_cast<s64>(info.uncompressed_size), MakeTimestamp(info));
        }
    } while (UNZ_OK == unzGoToNextFile(zf));

    log_write("[FS-ZIP] indexed %zu files, %zu dirs\n", m_files.size(), m_dirs.size());
    R_SUCCEED();
}

Result FsZip::DecompressFile(const std::string& key, std::vector<u8>& out) {
    out.clear();
    R_UNLESS(m_zip, FsError_NotImplemented);

    const auto it = m_pos.find(key);
    R_UNLESS(it != m_pos.end(), FsError_PathNotFound);
    const auto& rec = it->second;
    const s64 size = m_files.at(key).size;

    auto zf = static_cast<unzFile>(m_zip);

    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    unz64_file_pos pos{};
    pos.pos_in_zip_directory = rec.dir_pos;
    pos.num_of_file = rec.num_file;
    R_UNLESS(UNZ_OK == unzGoToFilePos64(zf, &pos), FsError_PathNotFound);
    R_UNLESS(UNZ_OK == unzOpenCurrentFile(zf), FsError_PathNotFound);
    ON_SCOPE_EXIT(unzCloseCurrentFile(zf));

    out.resize(size);
    s64 done = 0;
    while (done < size) {
        const auto to_read = static_cast<unsigned>(std::min<s64>(size - done, 0x4000000)); // 64 MiB chunks
        const int r = unzReadCurrentFile(zf, out.data() + done, to_read);
        if (r < 0) {
            log_write("[FS-ZIP] read failed for %s (%d)\n", key.c_str(), r);
            out.clear();
            R_THROW(FsError_NotImplemented);
        }
        if (r == 0) {
            break;
        }
        done += r;
    }
    out.resize(done);
    R_SUCCEED();
}

Result FsZip::read_entire_file(const FsPath& path, std::vector<u8>& out) {
    return DecompressFile(Normalize(path), out);
}

Result FsZip::vOpenFile(const FsPath& path, u32 mode, File* f) {
    R_UNLESS(!(mode & FsOpenMode_Write), FsError_NotImplemented);

    auto handle = std::make_unique<ZipOpenFile>();
    R_TRY(DecompressFile(Normalize(path), handle->data));
    f->m_virtual = handle.release();
    R_SUCCEED();
}

Result FsZip::vReadFile(File* f, s64 off, void* buf, u64 read_size, u32 option, u64* bytes_read) {
    *bytes_read = 0;
    auto h = static_cast<ZipOpenFile*>(f->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);

    if (off < 0 || static_cast<u64>(off) >= h->data.size()) {
        R_SUCCEED(); // EOF
    }

    const u64 n = std::min<u64>(read_size, h->data.size() - static_cast<u64>(off));
    std::memcpy(buf, h->data.data() + off, n);
    *bytes_read = n;
    R_SUCCEED();
}

Result FsZip::vGetFileSize(File* f, s64* out) {
    auto h = static_cast<ZipOpenFile*>(f->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);
    *out = static_cast<s64>(h->data.size());
    R_SUCCEED();
}

void FsZip::vCloseFile(File* f) {
    delete static_cast<ZipOpenFile*>(f->m_virtual);
    f->m_virtual = nullptr;
}

} // namespace fs
