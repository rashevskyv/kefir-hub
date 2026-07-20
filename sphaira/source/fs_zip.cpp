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

// per-open-dir cursor over a synthesised directory's children.
struct ZipOpenDir {
    std::vector<FsDirectoryEntry> entries;
    size_t index{};
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
: Fs{true}
, m_zip_path{zip_path.s} {
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

auto FsZip::Normalize(const char* path) -> std::string {
    std::vector<std::string> comps;
    std::string cur;
    const auto flush = [&]() {
        if (cur.empty() || cur == ".") {
            cur.clear();
        } else if (cur == "..") {
            if (!comps.empty()) comps.pop_back();
            cur.clear();
        } else {
            comps.emplace_back(std::move(cur));
            cur.clear();
        }
    };

    for (const char* p = path; *p; ++p) {
        const char c = (*p == '\\') ? '/' : *p;
        if (c == '/') {
            flush();
        } else {
            cur.push_back(c);
        }
    }
    flush();

    std::string out = "/";
    for (size_t i = 0; i < comps.size(); ++i) {
        out += comps[i];
        if (i + 1 < comps.size()) out += '/';
    }
    return out;
}

auto FsZip::ParentOf(const std::string& path) -> std::string {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

auto FsZip::LeafOf(const std::string& path) -> std::string {
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

void FsZip::AddChild(const std::string& parent, const std::string& name, FsDirEntryType type, s64 size) {
    auto& vec = m_dirs[parent];
    for (const auto& e : vec) {
        if (!std::strcmp(e.name, name.c_str())) {
            return; // already present
        }
    }

    FsDirectoryEntry e{};
    std::snprintf(e.name, sizeof(e.name), "%s", name.c_str());
    e.type = type;
    e.file_size = size;
    vec.emplace_back(e);
}

void FsZip::EnsureDir(const std::string& path) {
    if (path.empty() || path == "/") {
        m_dirs["/"]; // ensure root exists
        return;
    }
    if (m_dirs.count(path)) {
        return;
    }

    const auto parent = ParentOf(path);
    EnsureDir(parent);
    m_dirs[path]; // create empty child list
    AddChild(parent, LeafOf(path), FsDirEntryType_Dir, 0);
}

Result FsZip::BuildIndex() {
    auto zf = static_cast<unzFile>(m_zip);

    unz_global_info64 ginfo;
    R_UNLESS(UNZ_OK == unzGetGlobalInfo64(zf, &ginfo), FsError_PathNotFound);

    m_dirs["/"]; // root always exists, even for an empty archive.

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
            EnsureDir(key);
        } else {
            unz64_file_pos pos{};
            unzGetFilePos64(zf, &pos);

            FileRec rec{};
            rec.dir_pos = pos.pos_in_zip_directory;
            rec.num_file = pos.num_of_file;
            rec.size = static_cast<s64>(info.uncompressed_size);
            rec.crc = static_cast<u32>(info.crc);
            rec.ts = MakeTimestamp(info);

            const auto parent = ParentOf(key);
            EnsureDir(parent);
            m_files[key] = rec;
            AddChild(parent, LeafOf(key), FsDirEntryType_File, rec.size);
        }
    } while (UNZ_OK == unzGoToNextFile(zf));

    log_write("[FS-ZIP] indexed %zu files, %zu dirs\n", m_files.size(), m_dirs.size());
    R_SUCCEED();
}

Result FsZip::DecompressFile(const std::string& key, std::vector<u8>& out) {
    out.clear();
    R_UNLESS(m_zip, FsError_NotImplemented);

    const auto it = m_files.find(key);
    R_UNLESS(it != m_files.end(), FsError_PathNotFound);
    const auto& rec = it->second;

    auto zf = static_cast<unzFile>(m_zip);

    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    unz64_file_pos pos{};
    pos.pos_in_zip_directory = rec.dir_pos;
    pos.num_of_file = rec.num_file;
    R_UNLESS(UNZ_OK == unzGoToFilePos64(zf, &pos), FsError_PathNotFound);
    R_UNLESS(UNZ_OK == unzOpenCurrentFile(zf), FsError_PathNotFound);
    ON_SCOPE_EXIT(unzCloseCurrentFile(zf));

    out.resize(rec.size);
    s64 done = 0;
    while (done < rec.size) {
        const auto to_read = static_cast<unsigned>(std::min<s64>(rec.size - done, 0x4000000)); // 64 MiB chunks
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

Result FsZip::GetEntryType(const FsPath& path, FsDirEntryType* out) {
    const auto key = Normalize(path);
    if (m_files.count(key)) {
        *out = FsDirEntryType_File;
        R_SUCCEED();
    }
    if (m_dirs.count(key)) {
        *out = FsDirEntryType_Dir;
        R_SUCCEED();
    }
    R_THROW(FsError_PathNotFound);
}

Result FsZip::GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw* out) {
    const auto key = Normalize(path);
    if (const auto it = m_files.find(key); it != m_files.end()) {
        *out = it->second.ts;
        R_SUCCEED();
    }
    if (m_dirs.count(key)) {
        *out = {};
        R_SUCCEED();
    }
    R_THROW(FsError_PathNotFound);
}

bool FsZip::FileExists(const FsPath& path) {
    return m_files.count(Normalize(path)) > 0;
}

bool FsZip::DirExists(const FsPath& path) {
    return m_dirs.count(Normalize(path)) > 0;
}

Result FsZip::vStat(const FsPath& path, s64* size, FsTimeStampRaw* ts) {
    const auto key = Normalize(path);
    if (const auto it = m_files.find(key); it != m_files.end()) {
        if (size) *size = it->second.size;
        if (ts) *ts = it->second.ts;
        R_SUCCEED();
    }
    if (m_dirs.count(key)) {
        if (size) *size = 0;
        if (ts) *ts = {};
        R_SUCCEED();
    }
    R_THROW(FsError_PathNotFound);
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

Result FsZip::vOpenDir(const FsPath& path, u32 mode, Dir* d) {
    const auto key = Normalize(path);
    const auto it = m_dirs.find(key);
    R_UNLESS(it != m_dirs.end(), FsError_PathNotFound);

    auto handle = std::make_unique<ZipOpenDir>();
    for (const auto& e : it->second) {
        if (e.type == FsDirEntryType_Dir && !(mode & FsDirOpenMode_ReadDirs)) continue;
        if (e.type == FsDirEntryType_File && !(mode & FsDirOpenMode_ReadFiles)) continue;
        handle->entries.emplace_back(e);
    }
    d->m_virtual = handle.release();
    R_SUCCEED();
}

Result FsZip::vReadDir(Dir* d, s64* total_entries, size_t max_entries, FsDirectoryEntry* buf) {
    *total_entries = 0;
    auto h = static_cast<ZipOpenDir*>(d->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);

    while (h->index < h->entries.size() && static_cast<size_t>(*total_entries) < max_entries) {
        buf[*total_entries] = h->entries[h->index];
        (*total_entries)++;
        h->index++;
    }
    R_SUCCEED();
}

Result FsZip::vReadDirCount(Dir* d, s64* out) {
    auto h = static_cast<ZipOpenDir*>(d->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);
    *out = static_cast<s64>(h->entries.size());
    R_SUCCEED();
}

void FsZip::vCloseDir(Dir* d) {
    delete static_cast<ZipOpenDir*>(d->m_virtual);
    d->m_virtual = nullptr;
}

} // namespace fs
