#pragma once

#include "fs_virtual_tree.hpp"
#include <memory>

namespace fs {

// Read-only virtual filesystem over a rar (UnRAR) / 7z / tar(.gz/.xz/.bz2) (libarchive) archive.
// The archive is indexed once; opening a file never unpacks it to disk or to memory. The
// file is decompressed while it is read: forward reads are cheap, a read that goes back
// restarts the archive, so sequential users (copy, install) are the ones that fit.
struct FsArchive final : FsVirtualTree {
    explicit FsArchive(const FsPath& archive_path);

    Result GetFsOpenResult() const { return m_open_result; }

    Result read_entire_file(const FsPath& path, std::vector<u8>& out) override;

    Result vOpenFile(const FsPath& path, u32 mode, File* f) override;
    Result vReadFile(File* f, s64 off, void* buf, u64 read_size, u32 option, u64* bytes_read) override;
    Result vGetFileSize(File* f, s64* out) override;
    void vCloseFile(File* f) override;

private:
    Result IndexRar();

    std::string m_path{};
    // a .rar set goes through UnRAR (solid RAR4, RAR5, split sets): its first volume.
    std::string m_rar_path{};
    Result m_open_result{};
};

// Mounts any browsable archive: zip through minizip, the rest through libarchive.
// `open_rc` receives why it failed; the returned fs is null then.
std::unique_ptr<Fs> OpenArchiveFs(const FsPath& archive_path, Result* open_rc = nullptr);

} // namespace fs
