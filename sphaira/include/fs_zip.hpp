#pragma once

#include "fs.hpp"
#include <map>
#include <string>
#include <vector>

namespace fs {

// Read-only virtual filesystem over a .zip archive. Entries are indexed once
// at construction (from the central directory) and directories are synthesised
// from entry path prefixes, so the archive browses through the normal fs::Fs
// path in the file manager. Opening a file decompresses it fully into memory.
struct FsZip final : Fs {
    explicit FsZip(const FsPath& zip_path);
    ~FsZip() override;

    Result GetFsOpenResult() const { return m_open_result; }

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
    Result read_entire_file(const FsPath& path, std::vector<u8>& out) override;

    // virtual handle hooks driven by fs::File / fs::Dir.
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
    // minizip's unz64_file_pos, stored raw so the header stays minizip-free.
    struct FileRec {
        u64 dir_pos{};
        u64 num_file{};
        s64 size{};
        u32 crc{};
        FsTimeStampRaw ts{};
    };

    // builds the directory tree from the archive's central directory.
    Result BuildIndex();
    // decompresses one entry fully into out (locks the shared unz handle).
    Result DecompressFile(const std::string& key, std::vector<u8>& out);
    void AddChild(const std::string& parent, const std::string& name, FsDirEntryType type, s64 size);
    void EnsureDir(const std::string& path);

    static auto Normalize(const char* path) -> std::string;
    static auto ParentOf(const std::string& path) -> std::string;
    static auto LeafOf(const std::string& path) -> std::string;

    std::string m_zip_path{};
    void* m_zip{}; // unzFile
    Mutex m_mutex{};
    Result m_open_result{};

    std::map<std::string, FileRec> m_files{};
    std::map<std::string, std::vector<FsDirectoryEntry>> m_dirs{};
};

} // namespace fs
