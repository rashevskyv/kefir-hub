#pragma once

#include "fs.hpp"
#include <map>
#include <string>
#include <vector>

namespace fs {

// Read-only virtual filesystem exposing the NCA files of one installed content
// component (Application / Update / AddOn) as a flat directory, read directly
// from NcmContentStorage. Lets the file browser mount and navigate a component
// via the normal fs::Fs path. Holds a title:: reference so the NCM storages
// stay open for its lifetime.
struct FsNcm final : Fs {
    FsNcm(u64 application_id, u8 meta_type, u8 storage_id);
    ~FsNcm() override;

    Result GetFsOpenResult() const { return m_open_result; }

    // read-only.
    Result CreateFile(const FsPath& path, u64 size = 0, u32 option = 0) override { R_THROW(FsError_NotImplemented); }
    Result CreateDirectory(const FsPath& path) override { R_THROW(FsError_NotImplemented); }
    Result CreateDirectoryRecursively(const FsPath& path) override { R_THROW(FsError_NotImplemented); }
    Result CreateDirectoryRecursivelyWithPath(const FsPath& path) override { R_THROW(FsError_NotImplemented); }
    Result DeleteFile(const FsPath& path) override { R_THROW(FsError_NotImplemented); }
    Result DeleteDirectory(const FsPath& path) override { R_THROW(FsError_NotImplemented); }
    Result DeleteDirectoryRecursively(const FsPath& path) override { R_THROW(FsError_NotImplemented); }
    Result RenameFile(const FsPath& src, const FsPath& dst) override { R_THROW(FsError_NotImplemented); }
    Result RenameDirectory(const FsPath& src, const FsPath& dst) override { R_THROW(FsError_NotImplemented); }
    Result SetTimestamp(const FsPath& path, const FsTimeStampRaw* ts) override { R_THROW(FsError_NotImplemented); }
    Result write_entire_file(const FsPath& path, const std::vector<u8>& in) override { R_THROW(FsError_NotImplemented); }
    Result copy_entire_file(const FsPath& dst, const FsPath& src) override { R_THROW(FsError_NotImplemented); }
    Result Commit() override { R_SUCCEED(); }

    bool IsNative() const override { return false; }
    bool IsVirtual() const override { return true; }
    FsPath Root() const override { return "/"; }

    Result GetEntryType(const FsPath& path, FsDirEntryType* out) override;
    Result GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw* out) override;
    bool FileExists(const FsPath& path) override;
    bool DirExists(const FsPath& path) override;
    Result read_entire_file(const FsPath& path, std::vector<u8>& out) override;

    Result vOpenFile(const FsPath& path, u32 mode, File* f) override;
    Result vReadFile(File* f, s64 off, void* buf, u64 read_size, u32 option, u64* bytes_read) override;
    Result vGetFileSize(File* f, s64* out) override;
    void vCloseFile(File* f) override;
    Result vOpenDir(const FsPath& path, u32 mode, Dir* d) override;
    Result vReadDir(Dir* d, s64* total_entries, size_t max_entries, FsDirectoryEntry* buf) override;
    Result vReadDirCount(Dir* d, s64* out) override;
    void vCloseDir(Dir* d) override;
    Result vStat(const FsPath& path, s64* size, FsTimeStampRaw* ts) override;

private:
    struct FileRec {
        NcmContentId id{};
        s64 size{};
    };

    Result BuildIndex(u64 application_id, u8 meta_type);
    static auto LeafOf(const char* path) -> std::string;

    u8 m_storage_id{};
    bool m_has_title{};
    Result m_open_result{};

    // flat: only the root directory, holding every NCA of the component.
    std::map<std::string, FileRec> m_files{};
    std::vector<FsDirectoryEntry> m_root_entries{};
};

} // namespace fs
