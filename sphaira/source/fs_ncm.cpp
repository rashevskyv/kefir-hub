#include "fs_ncm.hpp"
#include "title_info.hpp"
#include "yati/nx/ncm.hpp"
#include "utils/utils.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstring>

// title::, ncm::, utils:: live under sphaira:: — bring them into scope for this
// top-level fs:: translation unit.
using namespace sphaira;

namespace fs {
namespace {

// per-open-file state: just the content id and size; reads go straight to
// NcmContentStorage at arbitrary offsets (no buffering needed).
struct NcmOpenFile {
    NcmContentId id{};
    s64 size{};
};

struct NcmOpenDir {
    std::vector<FsDirectoryEntry> entries;
    size_t index{};
};

} // namespace

FsNcm::FsNcm(u64 application_id, u8 meta_type, u8 storage_id)
: Fs{true}
, m_storage_id{storage_id} {
    // keep the NCM storages open for our lifetime.
    m_has_title = R_SUCCEEDED(title::Init());
    m_open_result = BuildIndex(application_id, meta_type);
}

FsNcm::~FsNcm() {
    if (m_has_title) {
        title::Exit();
    }
}

auto FsNcm::LeafOf(const char* path) -> std::string {
    const char* p = std::strrchr(path, '/');
    return p ? std::string(p + 1) : std::string(path);
}

Result FsNcm::BuildIndex(u64 application_id, u8 meta_type) {
    auto& db = title::GetNcmDb(m_storage_id);
    const auto app_id = ncm::GetAppId(meta_type, application_id);

    auto id_min = application_id;
    auto id_max = application_id;
    if (m_storage_id == NcmStorageId_None || m_storage_id == NcmStorageId_GameCard) {
        id_min -= 1;
        id_max += 1;
    }

    s32 total{}, written{};
    NcmContentMetaKey key{};
    R_TRY(ncmContentMetaDatabaseList(&db, &total, &written, &key, 1, (NcmContentMetaType)meta_type, app_id, id_min, id_max, NcmContentInstallType_Full));
    R_UNLESS(total == 1 && written == 1, FsError_PathNotFound);

    for (s32 i = 0; ; i++) {
        s32 entries_written{};
        NcmContentInfo info{};
        R_TRY(ncmContentMetaDatabaseListContentInfo(&db, &entries_written, &info, 1, &key, i));
        if (!entries_written) {
            break;
        }

        u64 size{};
        ncmContentInfoSizeToU64(&info, &size);

        char name[64];
        std::snprintf(name, sizeof(name), "%s%s", utils::hexIdToStr(info.content_id).str,
            info.content_type == NcmContentType_Meta ? ".cnmt.nca" : ".nca");

        m_files[name] = FileRec{info.content_id, static_cast<s64>(size)};

        FsDirectoryEntry e{};
        std::snprintf(e.name, sizeof(e.name), "%s", name);
        e.type = FsDirEntryType_File;
        e.file_size = static_cast<s64>(size);
        m_root_entries.emplace_back(e);
    }

    log_write("[FS-NCM] indexed %zu NCA files (storage %u)\n", m_files.size(), m_storage_id);
    R_SUCCEED();
}

static bool IsRootPath(const char* path) {
    for (const char* p = path; *p; ++p) {
        if (*p != '/') {
            return false;
        }
    }
    return true;
}

Result FsNcm::GetEntryType(const FsPath& path, FsDirEntryType* out) {
    if (IsRootPath(path)) {
        *out = FsDirEntryType_Dir;
        R_SUCCEED();
    }
    if (m_files.count(LeafOf(path))) {
        *out = FsDirEntryType_File;
        R_SUCCEED();
    }
    R_THROW(FsError_PathNotFound);
}

Result FsNcm::GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw* out) {
    *out = {};
    if (IsRootPath(path) || m_files.count(LeafOf(path))) {
        R_SUCCEED();
    }
    R_THROW(FsError_PathNotFound);
}

bool FsNcm::FileExists(const FsPath& path) {
    return m_files.count(LeafOf(path)) > 0;
}

bool FsNcm::DirExists(const FsPath& path) {
    return IsRootPath(path);
}

Result FsNcm::vStat(const FsPath& path, s64* size, FsTimeStampRaw* ts) {
    if (ts) *ts = {};
    if (const auto it = m_files.find(LeafOf(path)); it != m_files.end()) {
        if (size) *size = it->second.size;
        R_SUCCEED();
    }
    if (IsRootPath(path)) {
        if (size) *size = 0;
        R_SUCCEED();
    }
    R_THROW(FsError_PathNotFound);
}

Result FsNcm::read_entire_file(const FsPath& path, std::vector<u8>& out) {
    out.clear();
    const auto it = m_files.find(LeafOf(path));
    R_UNLESS(it != m_files.end(), FsError_PathNotFound);

    out.resize(it->second.size);
    if (it->second.size) {
        auto& cs = title::GetNcmCs(m_storage_id);
        R_TRY(ncmContentStorageReadContentIdFile(&cs, out.data(), out.size(), &it->second.id, 0));
    }
    R_SUCCEED();
}

Result FsNcm::vOpenFile(const FsPath& path, u32 mode, File* f) {
    R_UNLESS(!(mode & FsOpenMode_Write), FsError_NotImplemented);
    const auto it = m_files.find(LeafOf(path));
    R_UNLESS(it != m_files.end(), FsError_PathNotFound);

    f->m_virtual = new NcmOpenFile{it->second.id, it->second.size};
    R_SUCCEED();
}

Result FsNcm::vReadFile(File* f, s64 off, void* buf, u64 read_size, u32 option, u64* bytes_read) {
    *bytes_read = 0;
    auto h = static_cast<NcmOpenFile*>(f->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);

    if (off < 0 || off >= h->size) {
        R_SUCCEED(); // EOF
    }

    const s64 n = std::min<s64>(static_cast<s64>(read_size), h->size - off);
    auto& cs = title::GetNcmCs(m_storage_id);
    R_TRY(ncmContentStorageReadContentIdFile(&cs, buf, n, &h->id, off));
    *bytes_read = n;
    R_SUCCEED();
}

Result FsNcm::vGetFileSize(File* f, s64* out) {
    auto h = static_cast<NcmOpenFile*>(f->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);
    *out = h->size;
    R_SUCCEED();
}

void FsNcm::vCloseFile(File* f) {
    delete static_cast<NcmOpenFile*>(f->m_virtual);
    f->m_virtual = nullptr;
}

Result FsNcm::vOpenDir(const FsPath& path, u32 mode, Dir* d) {
    R_UNLESS(IsRootPath(path), FsError_PathNotFound); // flat: only the root exists.

    auto handle = std::make_unique<NcmOpenDir>();
    if (mode & FsDirOpenMode_ReadFiles) {
        handle->entries = m_root_entries; // all entries are files.
    }
    d->m_virtual = handle.release();
    R_SUCCEED();
}

Result FsNcm::vReadDir(Dir* d, s64* total_entries, size_t max_entries, FsDirectoryEntry* buf) {
    *total_entries = 0;
    auto h = static_cast<NcmOpenDir*>(d->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);

    while (h->index < h->entries.size() && static_cast<size_t>(*total_entries) < max_entries) {
        buf[*total_entries] = h->entries[h->index];
        (*total_entries)++;
        h->index++;
    }
    R_SUCCEED();
}

Result FsNcm::vReadDirCount(Dir* d, s64* out) {
    auto h = static_cast<NcmOpenDir*>(d->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);
    *out = static_cast<s64>(h->entries.size());
    R_SUCCEED();
}

void FsNcm::vCloseDir(Dir* d) {
    delete static_cast<NcmOpenDir*>(d->m_virtual);
    d->m_virtual = nullptr;
}

} // namespace fs
