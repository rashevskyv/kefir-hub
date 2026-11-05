#pragma once

#include "base.hpp"
#include "stream.hpp"
#include "fs.hpp"
#include <switch.h>
#include <memory>

namespace sphaira::yati::source {

struct File final : Base {
    File(fs::Fs* fs, const fs::FsPath& path);
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;
    Result GetSize(s64* out) override;

private:
    fs::Fs* m_fs{};
    fs::File m_file{};
};

// a file that can only be read forwards at a sensible speed (a file inside a rar / 7z):
// yati takes the stream path, which never seeks back.
struct FileStream final : Stream {
    FileStream(fs::Fs* fs, const fs::FsPath& path);
    Result ReadChunk(void* buf, s64 size, u64* bytes_read) override;
    Result GetSize(s64* out) override;

private:
    fs::File m_file{};
    s64 m_pos{};
};

} // namespace sphaira::yati::source
