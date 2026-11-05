#pragma once

// RAR backend on the official UnRAR library (rarlab.com, DLL API). libarchive stays the
// reader for 7z / tar / gz...; RAR goes here because libarchive cannot read solid RAR4.
// This header and rar_reader.cpp are libnx-free: the host test builds them against UnRAR.

#include "byte_pipe.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <pthread.h>
#include <memory>
#include <string>
#include <string_view>

struct RARHeaderDataEx;

namespace sphaira::rar {

// why a call failed, in the terms the user is told.
enum class Error {
    None,
    SourceRead,  // the archive (or the next volume) cannot be opened or read
    Unsupported, // not a RAR archive
    Damaged,     // bad data, bad header, checksum error, truncated
    NoMemory,    // e.g. a dictionary larger than the console can hold
    Password,    // encrypted: passwords are not supported
    Stopped,     // the sink asked to stop (cancel, write error: the caller knows which)
    NotFound,    // EntryStream: no entry matched
};

struct Entry {
    std::string name; // UTF-8, '/' separated, as stored in the archive
    std::int64_t size{};
    std::uint64_t mtime{}; // unix seconds, 0 = not stored
    bool is_dir{};
    bool is_file{}; // a regular file: not a folder, not a link
};

// one UnRAR handle over a path fopen() can open (the sd card or a stdio mount).
// A split set is read through: pass its first volume, UnRAR opens the next ones by name.
// ponytail: UnRAR keeps its error code in one global (ErrHandler); two handles failing at
// the same moment on two threads may report each other's error. Serialise if that matters.
struct Reader {
    // receives the decompressed bytes of the current entry; false stops (Error::Stopped).
    using Sink = std::function<bool(const void* data, size_t size)>;

    Reader();
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    // `extract` = entries will be read; false = headers only (cheap, any archive).
    Error Open(const std::string& path, bool extract);
    // the next entry; *end = true after the last one.
    Error Next(Entry* out, bool* end);
    // moves past the current entry. A solid archive still decompresses it (each entry
    // depends on the ones before it): `sink` gets the bytes so it can stop a long skip.
    Error Skip(const Sink& sink);
    // decompresses the current entry into `sink`, checking its CRC.
    Error Read(const Sink& sink);

    bool solid{};

    // internal, used by the UnRAR callback.
    void* handle{};
    std::unique_ptr<RARHeaderDataEx> header;
    const Sink* sink{};
    bool sink_stopped{};
    bool password_asked{};

private:
    Error Process(int operation, const Sink& sink);
};

// One entry of an archive as a forward-only stream. UnRAR pushes data from its callback,
// a file read pulls it: a worker thread decompresses into a bounded BytePipe.
// Destroying the stream closes the pipe (the worker stops at its next data chunk) and
// joins the worker; opening the archive or a non-solid seek has no chunk, so that part
// runs to its end first (disk I/O only, it does not block on the reader).
class EntryStream {
public:
    using Match = std::function<bool(const Entry&)>;

    // `path`: first volume; `match` picks the entry (the first regular file it accepts).
    EntryStream(std::string path, Match match);
    ~EntryStream();
    EntryStream(const EntryStream&) = delete;
    EntryStream& operator=(const EntryStream&) = delete;

    // fills `out` with up to `size` bytes, fewer only at the end of the entry or on an error.
    size_t Read(void* out, size_t size);
    // why the stream ended early; None after a full entry. Valid once Read() returned short.
    Error GetError() const { return m_error.load(); }

private:
    static void* Run(void* self);

    std::string m_path;
    Match m_match;
    std::unique_ptr<BytePipe> m_pipe;
    std::atomic<Error> m_error{Error::None};
    pthread_t m_thread{};
    bool m_started{};
};

// UnRAR's ERAR_* code -> Error. `password_asked` / `stopped` come from the callback.
Error MapCode(int erar_code, bool password_asked, bool stopped);

// wchar_t (UTF-32 on the Switch and Linux) -> UTF-8; invalid code points become '?'.
inline auto WideToUtf8(const wchar_t* s) -> std::string {
    std::string out;
    for (; s && *s; s++) {
        auto c = static_cast<std::uint32_t>(*s);
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            c = '?';
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// Windows FILETIME (100 ns since 1601) -> unix seconds; 0 for "not set" or before 1970.
inline auto FileTimeToUnix(std::uint64_t filetime) -> std::uint64_t {
    constexpr std::uint64_t EPOCH_DIFF = 11644473600ULL; // seconds 1601 -> 1970
    const auto secs = filetime / 10000000ULL;
    return secs > EPOCH_DIFF ? secs - EPOCH_DIFF : 0;
}

// true when the first volume of a set is a .rar: that set goes to UnRAR.
inline bool IsRarName(std::string_view first_volume) {
    if (first_volume.size() < 4) {
        return false;
    }
    const auto ext = first_volume.substr(first_volume.size() - 4);
    return ext[0] == '.' && (ext[1] | 0x20) == 'r' && (ext[2] | 0x20) == 'a' && (ext[3] | 0x20) == 'r';
}

} // namespace sphaira::rar
