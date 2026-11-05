#pragma once

#include "fs.hpp"
#include <map>
#include <string>
#include <vector>

namespace fs {

// Read-only virtual filesystem over an archive's file list. The archive reader
// (zip, rar, 7z...) fills the tree with AddFile / AddDir, directories are
// synthesised from path prefixes, and the derived class serves file contents
// (vOpenFile / vReadFile / vGetFileSize / vCloseFile, read_entire_file).
struct FsVirtualTree : Fs {
    FsVirtualTree() : Fs{true} {}

    // read-only: every mutating operation is rejected.
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

    // virtual directory hooks driven by fs::Dir.
    Result vOpenDir(const FsPath& path, u32 mode, Dir* d) override;
    Result vReadDir(Dir* d, s64* total_entries, size_t max_entries, FsDirectoryEntry* buf) override;
    Result vReadDirCount(Dir* d, s64* out) override;
    void vCloseDir(Dir* d) override;
    Result vStat(const FsPath& path, s64* size, FsTimeStampRaw* ts) override;

    // "a\b/../c" -> "/c": the key every path is looked up by.
    static auto Normalize(const char* path) -> std::string;

protected:
    struct FileRec {
        s64 size{};
        FsTimeStampRaw ts{};
    };

    // a file entry (creates its parent folders); `key` comes from Normalize().
    void AddFile(const std::string& key, s64 size, const FsTimeStampRaw& ts);
    // a folder entry; `key` comes from Normalize().
    void AddDir(const std::string& key);

    std::map<std::string, FileRec> m_files{};
    std::map<std::string, std::vector<FsDirectoryEntry>> m_dirs{};

private:
    void AddChild(const std::string& parent, const std::string& name, FsDirEntryType type, s64 size);
    void EnsureDir(const std::string& path);

    static auto ParentOf(const std::string& path) -> std::string;
    static auto LeafOf(const std::string& path) -> std::string;
};

} // namespace fs
