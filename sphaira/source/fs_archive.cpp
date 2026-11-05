#include "fs_archive.hpp"
#include "fs_zip.hpp"
#include "archive_extract_plan.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace fs {
namespace {

constexpr size_t READ_BUF_SIZE = 256 * 1024;
constexpr size_t SKIP_BUF_SIZE = 1024 * 1024;

// one libarchive reader on a plain FILE*: the archive path works for sd and network mounts.
struct Reader {
    FILE* fp{};
    struct archive* a{};
    std::vector<u8> buf = std::vector<u8>(READ_BUF_SIZE);

    ~Reader() {
        if (a) {
            archive_read_free(a);
        }
        if (fp) {
            std::fclose(fp);
        }
    }

    la_ssize_t OnRead(const void** out) {
        *out = buf.data();
        return static_cast<la_ssize_t>(std::fread(buf.data(), 1, buf.size(), fp));
    }

    la_int64_t OnSeek(la_int64_t offset, int whence) {
        if (fseeko(fp, static_cast<off_t>(offset), whence)) {
            return ARCHIVE_FATAL;
        }
        return static_cast<la_int64_t>(ftello(fp));
    }

    Result Open(const std::string& path) {
        fp = std::fopen(path.c_str(), "rb");
        R_UNLESS(fp, FsError_PathNotFound);

        a = archive_read_new();
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);
        archive_read_support_format_raw(a);
        archive_read_set_read_callback(a, [](struct archive*, void* user, const void** out) -> la_ssize_t {
            return static_cast<Reader*>(user)->OnRead(out);
        });
        archive_read_set_seek_callback(a, [](struct archive*, void* user, la_int64_t offset, int whence) -> la_int64_t {
            return static_cast<Reader*>(user)->OnSeek(offset, whence);
        });
        archive_read_set_callback_data(a, this);
        if (archive_read_open1(a) != ARCHIVE_OK) {
            log_write("[FS-ARCHIVE] open %s: %s\n", path.c_str(), archive_error_string(a));
            R_THROW(Result_ArchiveUnsupported);
        }
        R_SUCCEED();
    }

    Result Failure() const {
        return archive_errno(a) == ENOMEM ? Result_ArchiveNoMemory : Result_ArchiveDamaged;
    }
};

// where an entry shows up in the tree; empty = skip it.
auto EntryKey(struct archive* a, struct archive_entry* entry, const std::string& archive_path) -> std::string {
    const char* name = archive_entry_pathname_utf8(entry);
    if (!name) {
        name = archive_entry_pathname(entry);
    }
    const bool raw = archive_format(a) == ARCHIVE_FORMAT_RAW;
    const auto rel = sphaira::archive::OutputPath(name ? name : "", raw, archive_path);
    return rel ? FsVirtualTree::Normalize(rel->c_str()) : std::string{};
}

// per-open-file state: its own reader, advanced lazily to `pos` inside the entry.
struct ArchiveOpenFile {
    std::string path;
    std::string key;
    s64 size{};
    s64 pos{};
    std::unique_ptr<Reader> reader{};
    std::vector<u8> skip{};

    // (re)starts the archive and stops at this entry's data.
    Result Restart() {
        reader.reset();
        pos = 0;

        auto r = std::make_unique<Reader>();
        R_TRY(r->Open(path));
        struct archive_entry* entry;
        while (true) {
            const auto rc = archive_read_next_header(r->a, &entry);
            if (rc == ARCHIVE_EOF) {
                R_THROW(FsError_PathNotFound);
            }
            if (rc < ARCHIVE_WARN) {
                R_THROW(r->Failure());
            }
            if (archive_entry_filetype(entry) == AE_IFREG && EntryKey(r->a, entry, path) == key) {
                break;
            }
        }
        reader = std::move(r);
        R_SUCCEED();
    }

    // reads `n` bytes at `off`; going backwards restarts the archive, going forward decompresses and drops.
    Result Read(s64 off, void* out, u64 n, u64* bytes_read) {
        *bytes_read = 0;
        if (!reader || off < pos) {
            R_TRY(Restart());
        }

        while (pos < off) {
            if (skip.empty()) {
                skip.resize(SKIP_BUF_SIZE);
            }
            const auto want = static_cast<size_t>(std::min<s64>(off - pos, skip.size()));
            const auto got = archive_read_data(reader->a, skip.data(), want);
            if (got < 0) {
                R_THROW(reader->Failure());
            }
            if (got == 0) {
                R_SUCCEED(); // EOF before the offset
            }
            pos += got;
        }

        auto dst = static_cast<u8*>(out);
        while (n) {
            const auto got = archive_read_data(reader->a, dst, static_cast<size_t>(std::min<u64>(n, 0x4000000)));
            if (got < 0) {
                R_THROW(reader->Failure());
            }
            if (got == 0) {
                break;
            }
            dst += got;
            n -= got;
            pos += got;
            *bytes_read += got;
        }
        R_SUCCEED();
    }
};

} // namespace

FsArchive::FsArchive(const FsPath& archive_path) : m_path{archive_path.s} {
    Reader r;
    m_open_result = r.Open(m_path);
    if (R_FAILED(m_open_result)) {
        return;
    }

    AddDir("/"); // root always exists, even for an empty archive.

    struct archive_entry* entry;
    while (true) {
        const auto rc = archive_read_next_header(r.a, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc < ARCHIVE_WARN) {
            log_write("[FS-ARCHIVE] header: %s\n", archive_error_string(r.a));
            m_open_result = r.Failure();
            return;
        }

        // a bare .gz/.xz holds one stream of unknown size: only "Extract" fits it.
        if (archive_format(r.a) == ARCHIVE_FORMAT_RAW) {
            m_open_result = Result_ArchiveUnsupported;
            return;
        }

        const auto key = EntryKey(r.a, entry, m_path);
        if (key.empty() || key == "/") {
            continue;
        }

        const auto type = archive_entry_filetype(entry);
        if (type == AE_IFDIR) {
            AddDir(key);
        } else if (type == AE_IFREG) {
            FsTimeStampRaw ts{};
            if (archive_entry_mtime_is_set(entry)) {
                ts.is_valid = true;
                ts.created = ts.modified = ts.accessed = static_cast<u64>(archive_entry_mtime(entry));
            }
            AddFile(key, archive_entry_size_is_set(entry) ? archive_entry_size(entry) : 0, ts);
        }
    }

    log_write("[FS-ARCHIVE] indexed %zu files, %zu dirs\n", m_files.size(), m_dirs.size());
}

Result FsArchive::vOpenFile(const FsPath& path, u32 mode, File* f) {
    R_UNLESS(!(mode & FsOpenMode_Write), FsError_NotImplemented);

    const auto key = Normalize(path);
    const auto it = m_files.find(key);
    R_UNLESS(it != m_files.end(), FsError_PathNotFound);

    auto h = std::make_unique<ArchiveOpenFile>();
    h->path = m_path;
    h->key = key;
    h->size = it->second.size;
    f->m_virtual = h.release();
    R_SUCCEED();
}

Result FsArchive::vReadFile(File* f, s64 off, void* buf, u64 read_size, u32, u64* bytes_read) {
    *bytes_read = 0;
    auto h = static_cast<ArchiveOpenFile*>(f->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);

    if (off < 0 || off >= h->size) {
        R_SUCCEED(); // EOF
    }
    return h->Read(off, buf, std::min<u64>(read_size, static_cast<u64>(h->size - off)), bytes_read);
}

Result FsArchive::vGetFileSize(File* f, s64* out) {
    auto h = static_cast<ArchiveOpenFile*>(f->m_virtual);
    R_UNLESS(h, FsError_NotImplemented);
    *out = h->size;
    R_SUCCEED();
}

void FsArchive::vCloseFile(File* f) {
    delete static_cast<ArchiveOpenFile*>(f->m_virtual);
    f->m_virtual = nullptr;
}

Result FsArchive::read_entire_file(const FsPath& path, std::vector<u8>& out) {
    out.clear();
    File f;
    R_TRY(OpenFile(path, FsOpenMode_Read, &f));
    s64 size{};
    R_TRY(f.GetSize(&size));
    out.resize(size);
    u64 got{};
    R_TRY(f.Read(0, out.data(), out.size(), 0, &got));
    out.resize(got);
    R_SUCCEED();
}

std::unique_ptr<Fs> OpenArchiveFs(const FsPath& archive_path, Result* open_rc) {
    Result rc{};
    std::unique_ptr<Fs> out;

    const auto ext = std::strrchr(archive_path.s, '.');
    if (ext && !strcasecmp(ext, ".zip")) {
        auto z = std::make_unique<FsZip>(archive_path);
        rc = z->GetFsOpenResult();
        out = std::move(z);
    } else {
        auto a = std::make_unique<FsArchive>(archive_path);
        rc = a->GetFsOpenResult();
        out = std::move(a);
    }

    if (open_rc) {
        *open_rc = rc;
    }
    if (R_FAILED(rc)) {
        out.reset();
    }
    return out;
}

} // namespace fs
