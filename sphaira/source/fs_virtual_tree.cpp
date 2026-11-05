#include "fs_virtual_tree.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace fs {
namespace {

// per-open-dir cursor over a synthesised directory's children.
struct TreeOpenDir {
    std::vector<FsDirectoryEntry> entries;
    size_t index{};
};

} // namespace

auto FsVirtualTree::Normalize(const char* path) -> std::string {
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

auto FsVirtualTree::ParentOf(const std::string& path) -> std::string {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

auto FsVirtualTree::LeafOf(const std::string& path) -> std::string {
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

void FsVirtualTree::AddChild(const std::string& parent, const std::string& name, FsDirEntryType type, s64 size) {
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

void FsVirtualTree::EnsureDir(const std::string& path) {
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

void FsVirtualTree::AddDir(const std::string& key) {
    EnsureDir(key);
}

void FsVirtualTree::AddFile(const std::string& key, s64 size, const FsTimeStampRaw& ts) {
    const auto parent = ParentOf(key);
    EnsureDir(parent);
    m_files[key] = FileRec{size, ts};
    AddChild(parent, LeafOf(key), FsDirEntryType_File, size);
}

Result FsVirtualTree::GetEntryType(const FsPath& path, FsDirEntryType* out) {
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

Result FsVirtualTree::GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw* out) {
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

bool FsVirtualTree::FileExists(const FsPath& path) {
    return m_files.count(Normalize(path)) > 0;
}

bool FsVirtualTree::DirExists(const FsPath& path) {
    return m_dirs.count(Normalize(path)) > 0;
}

Result FsVirtualTree::vStat(const FsPath& path, s64* size, FsTimeStampRaw* ts) {
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

Result FsVirtualTree::vOpenDir(const FsPath& path, u32 mode, Dir* d) {
    const auto key = Normalize(path);
    const auto it = m_dirs.find(key);
    R_UNLESS(it != m_dirs.end(), FsError_PathNotFound);

    auto handle = std::make_unique<TreeOpenDir>();
    for (const auto& e : it->second) {
        if (e.type == FsDirEntryType_Dir && !(mode & FsDirOpenMode_ReadDirs)) continue;
        if (e.type == FsDirEntryType_File && !(mode & FsDirOpenMode_ReadFiles)) continue;
        handle->entries.emplace_back(e);
    }
    d->m_virtual = handle.release();
    R_SUCCEED();
}

Result FsVirtualTree::vReadDir(Dir* d, s64* total_entries, size_t max_entries, FsDirectoryEntry* buf) {
    *total_entries = 0;
    auto h = static_cast<TreeOpenDir*>(d->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);

    while (h->index < h->entries.size() && static_cast<size_t>(*total_entries) < max_entries) {
        buf[*total_entries] = h->entries[h->index];
        (*total_entries)++;
        h->index++;
    }
    R_SUCCEED();
}

Result FsVirtualTree::vReadDirCount(Dir* d, s64* out) {
    auto h = static_cast<TreeOpenDir*>(d->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);
    *out = static_cast<s64>(h->entries.size());
    R_SUCCEED();
}

void FsVirtualTree::vCloseDir(Dir* d) {
    delete static_cast<TreeOpenDir*>(d->m_virtual);
    d->m_virtual = nullptr;
}

} // namespace fs
