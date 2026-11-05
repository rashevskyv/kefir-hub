#include "fs_archive.hpp"
#include "fs_zip.hpp"
#include "archive_extract_plan.hpp"
#include "archive_extract.hpp"
#include "archive_volumes.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "rar_reader.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>

namespace fs {
namespace {

constexpr size_t READ_BUF_SIZE = 256 * 1024;
constexpr size_t SKIP_BUF_SIZE = 1024 * 1024;

// one libarchive reader on a plain FILE*: the archive path works for sd and network mounts.
// a split archive (name.part1.rar, name.7z.001...) is one reader that moves on to the next volume.
struct Reader {
    struct Volume {
        Reader* reader{};
        std::string path;
    };

    FILE* fp{};
    struct archive* a{};
    std::vector<u8> buf = std::vector<u8>(READ_BUF_SIZE);
    std::vector<Volume> volumes{};

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

    int OnSwitch(const Volume* next) {
        if (fp) {
            std::fclose(fp);
        }
        fp = std::fopen(next->path.c_str(), "rb");
        return fp ? ARCHIVE_OK : ARCHIVE_FATAL;
    }

    Result Open(const std::string& path) {
        const auto paths = sphaira::archive::VolumePaths(path);
        volumes.reserve(paths.size());
        for (const auto& p : paths) {
            volumes.push_back({this, p});
        }

        fp = std::fopen(volumes.front().path.c_str(), "rb");
        R_UNLESS(fp, FsError_PathNotFound);

        a = archive_read_new();
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);
        archive_read_support_format_raw(a);
        archive_read_set_read_callback(a, [](struct archive*, void* user, const void** out) -> la_ssize_t {
            return static_cast<Volume*>(user)->reader->OnRead(out);
        });
        archive_read_set_seek_callback(a, [](struct archive*, void* user, la_int64_t offset, int whence) -> la_int64_t {
            return static_cast<Volume*>(user)->reader->OnSeek(offset, whence);
        });
        archive_read_set_switch_callback(a, [](struct archive*, void*, void* next) -> int {
            const auto v = static_cast<Volume*>(next);
            return v->reader->OnSwitch(v);
        });
        archive_read_set_callback_data(a, &volumes.front());
        for (size_t i = 1; i < volumes.size(); i++) {
            archive_read_append_callback_data(a, &volumes[i]);
        }
        if (archive_read_open1(a) != ARCHIVE_OK) {
            log_write("[FS-ARCHIVE] open %s: %s\n", path.c_str(), archive_error_string(a));
            R_THROW(Result_ArchiveUnsupported);
        }
        R_SUCCEED();
    }

    Result Failure() const {
        if (archive_errno(a) == ENOMEM) {
            return Result_ArchiveNoMemory;
        }
        return sphaira::archive::IsSolidRar(archive_error_string(a)) ? Result_ArchiveSolidRar : Result_ArchiveDamaged;
    }
};

// where an entry named `name` shows up in the tree; empty = skip it.
auto KeyOf(std::string_view name, bool raw, const std::string& archive_path) -> std::string {
    const auto rel = sphaira::archive::OutputPath(name, raw, archive_path);
    return rel ? FsVirtualTree::Normalize(rel->c_str()) : std::string{};
}

auto EntryKey(struct archive* a, struct archive_entry* entry, const std::string& archive_path) -> std::string {
    const char* name = archive_entry_pathname_utf8(entry);
    if (!name) {
        name = archive_entry_pathname(entry);
    }
    return KeyOf(name ? name : "", archive_format(a) == ARCHIVE_FORMAT_RAW, archive_path);
}

// per-open-file state: its own reader, advanced lazily to `pos` inside the entry.
struct ArchiveOpenFile {
    std::string path;
    std::string key;
    s64 size{};
    s64 pos{};
    std::unique_ptr<Reader> reader{};
    std::vector<u8> skip{};
    // RAR through UnRAR: the first volume, and the stream of this entry.
    std::string rar_path{};
    std::unique_ptr<sphaira::rar::EntryStream> stream{};

    // RAR: same contract as Read(); the worker thread decompresses ahead into a bounded pipe.
    Result ReadRar(s64 off, void* out, u64 n, u64* bytes_read) {
        if (!stream || off < pos) {
            stream.reset(); // joins the old worker first
            pos = 0;
            stream = std::make_unique<sphaira::rar::EntryStream>(rar_path, [k = key, p = path](const sphaira::rar::Entry& e) {
                return KeyOf(e.name, false, p) == k;
            });
        }
        const auto failed = [&] { return sphaira::archive::RarResult(stream->GetError()); };
        while (pos < off) {
            if (skip.empty()) {
                skip.resize(SKIP_BUF_SIZE);
            }
            const auto want = static_cast<size_t>(std::min<s64>(off - pos, skip.size()));
            const auto got = stream->Read(skip.data(), want);
            pos += got;
            if (got < want) {
                return failed(); // success = EOF before the offset
            }
        }
        const auto got = stream->Read(out, n);
        pos += got;
        *bytes_read = got;
        return got < n ? failed() : Result{};
    }

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
        if (!rar_path.empty()) {
            return ReadRar(off, out, n, bytes_read);
        }
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

// RAR: UnRAR reads the headers (cheap even when solid: no data is decompressed).
Result FsArchive::IndexRar() {
    sphaira::rar::Reader r;
    R_TRY(sphaira::archive::RarResult(r.Open(m_rar_path, false)));
    AddDir("/");
    while (true) {
        sphaira::rar::Entry e;
        bool end{};
        R_TRY(sphaira::archive::RarResult(r.Next(&e, &end)));
        if (end) {
            break;
        }
        const auto key = KeyOf(e.name, false, m_path);
        if (!key.empty() && key != "/") {
            if (e.is_dir) {
                AddDir(key);
            } else if (e.is_file) {
                FsTimeStampRaw ts{};
                ts.is_valid = e.mtime != 0;
                ts.created = ts.modified = ts.accessed = e.mtime;
                AddFile(key, e.size, ts);
            }
        }
        R_TRY(sphaira::archive::RarResult(r.Skip({})));
    }
    log_write("[FS-ARCHIVE] rar indexed %zu files, %zu dirs (solid %d)\n", m_files.size(), m_dirs.size(), r.solid);
    R_SUCCEED();
}

FsArchive::FsArchive(const FsPath& archive_path) : m_path{archive_path.s} {
    if (const auto first = sphaira::archive::VolumePaths(m_path).front(); sphaira::rar::IsRarName(first)) {
        m_rar_path = first;
        m_open_result = IndexRar();
        if (R_FAILED(m_open_result)) {
            log_write("[FS-ARCHIVE] rar open %s: 0x%X\n", first.c_str(), m_open_result);
        }
        return;
    }

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
    h->rar_path = m_rar_path;
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
