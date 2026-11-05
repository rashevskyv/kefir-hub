#pragma once

#include "fs_virtual_tree.hpp"
#include <map>
#include <string>
#include <vector>

namespace fs {

// Read-only virtual filesystem over a .zip archive. Entries are indexed once
// at construction (from the central directory) and directories are synthesised
// from entry path prefixes, so the archive browses through the normal fs::Fs
// path in the file manager. Opening a file decompresses it fully into memory.
struct FsZip final : FsVirtualTree {
    explicit FsZip(const FsPath& zip_path);
    ~FsZip() override;

    Result GetFsOpenResult() const { return m_open_result; }

    Result read_entire_file(const FsPath& path, std::vector<u8>& out) override;

    // virtual handle hooks driven by fs::File.
    Result vOpenFile(const FsPath& path, u32 mode, File* f) override;
    Result vReadFile(File* f, s64 off, void* buf, u64 read_size, u32 option, u64* bytes_read) override;
    Result vGetFileSize(File* f, s64* out) override;
    void vCloseFile(File* f) override;

private:
    // minizip's unz64_file_pos, stored raw so the header stays minizip-free.
    struct ZipPos {
        u64 dir_pos{};
        u64 num_file{};
    };

    // builds the directory tree from the archive's central directory.
    Result BuildIndex();
    // decompresses one entry fully into out (locks the shared unz handle).
    Result DecompressFile(const std::string& key, std::vector<u8>& out);

    std::string m_zip_path{};
    void* m_zip{}; // unzFile
    Mutex m_mutex{};
    Result m_open_result{};

    std::map<std::string, ZipPos> m_pos{};
};

} // namespace fs
