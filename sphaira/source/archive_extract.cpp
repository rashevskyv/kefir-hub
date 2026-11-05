#include "archive_extract.hpp"
#include "archive_extract_plan.hpp"
#include "archive_volumes.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "rar_reader.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::archive {
namespace {

// libarchive reads through fs::File so sd, usb drives and network mounts all work,
// and 7z / rar can seek. A split archive (name.part1.rar, name.7z.001...) is read as one
// stream: libarchive asks for the next volume through OnSwitch.
struct Source {
    struct Volume {
        Source* source{};
        size_t index{};
    };

    fs::Fs* fs{};
    std::vector<fs::FsPath> paths{};
    std::vector<s64> sizes{};
    std::vector<Volume> volumes{};
    s64 total{};

    fs::File file{};
    s64 size{}; // of the current volume
    s64 off{};  // inside the current volume
    s64 base{}; // bytes in the volumes before the current one
    bool read_failed{};
    std::vector<u8> buf = std::vector<u8>(256 * 1024);

    // progress over every volume.
    s64 Done() const { return base + off; }

    Result OpenVolume(size_t i) {
        file.Close();
        R_TRY(fs->OpenFile(paths[i], FsOpenMode_Read, &file));
        R_TRY(file.GetSize(&size));
        off = 0;
        base = 0;
        for (size_t j = 0; j < i; j++) {
            base += sizes[j];
        }
        R_SUCCEED();
    }

    // finds the volumes next to `archive_path` and opens the first.
    Result Open(const fs::FsPath& archive_path) {
        std::string_view full{archive_path.s};
        const auto slash = full.find_last_of('/');
        const auto dir = slash == full.npos ? std::string{} : std::string{full.substr(0, slash + 1)};
        const auto leaf = std::string{slash == full.npos ? full : full.substr(slash + 1)};

        std::vector<std::string> siblings;
        fs::Dir d;
        std::vector<FsDirectoryEntry> entries;
        if (R_SUCCEEDED(fs->OpenDirectory(dir.empty() ? fs::FsPath{"/"} : fs::FsPath{dir.c_str()}, FsDirOpenMode_ReadFiles, &d)) && R_SUCCEEDED(d.ReadAll(entries))) {
            for (const auto& e : entries) {
                siblings.emplace_back(e.name);
            }
        }

        for (const auto& name : Volumes(leaf, siblings)) {
            paths.emplace_back((dir + name).c_str());
        }
        for (const auto& p : paths) {
            fs::File f;
            s64 sz{};
            R_TRY(fs->OpenFile(p, FsOpenMode_Read, &f));
            R_TRY(f.GetSize(&sz));
            sizes.emplace_back(sz);
            total += sz;
        }

        for (size_t i = 0; i < paths.size(); i++) {
            volumes.push_back({this, i});
        }
        return OpenVolume(0);
    }
};

la_ssize_t OnRead(struct archive* a, void* user, const void** out) {
    auto src = static_cast<Source::Volume*>(user)->source;
    u64 bytes{};
    if (R_FAILED(src->file.Read(src->off, src->buf.data(), src->buf.size(), 0, &bytes))) {
        src->read_failed = true;
        archive_set_error(a, EIO, "read failed");
        return -1;
    }
    src->off += bytes;
    *out = src->buf.data();
    return static_cast<la_ssize_t>(bytes);
}

la_int64_t OnSeek(struct archive*, void* user, la_int64_t offset, int whence) {
    auto src = static_cast<Source::Volume*>(user)->source;
    const s64 base = whence == SEEK_CUR ? src->off : whence == SEEK_END ? src->size : 0;
    src->off = std::clamp<s64>(base + offset, 0, src->size);
    return src->off;
}

int OnSwitch(struct archive* a, void*, void* next) {
    const auto v = static_cast<Source::Volume*>(next);
    if (R_FAILED(v->source->OpenVolume(v->index))) {
        v->source->read_failed = true;
        archive_set_error(a, EIO, "volume missing");
        return ARCHIVE_FATAL;
    }
    return ARCHIVE_OK;
}

// the most specific reason libarchive stopped: our own drive read, no memory, or bad data.
Result Failure(struct archive* a, const Source& src, Result otherwise) {
    if (src.read_failed) {
        return Result_ArchiveSourceRead;
    }
    if (archive_errno(a) == ENOMEM) {
        return Result_ArchiveNoMemory;
    }
    if (IsSolidRar(archive_error_string(a))) {
        return Result_ArchiveSolidRar;
    }
    return otherwise;
}

Result WriteEntry(ui::InstallProgress* pbox, struct archive* a, fs::Fs* fs, const fs::FsPath& dest, const Source& src) {
    R_TRY(fs->CreateDirectoryRecursivelyWithPath(dest));
    fs->DeleteFile(dest);
    R_TRY(fs->CreateFile(dest));
    fs::File out;
    R_TRY(fs->OpenFile(dest, FsOpenMode_Write | FsOpenMode_Append, &out));

    const void* block;
    size_t size;
    la_int64_t offset;
    while (true) {
        R_TRY(pbox->CheckCancelled());
        const auto r = archive_read_data_block(a, &block, &size, &offset);
        if (r == ARCHIVE_EOF) {
            R_SUCCEED();
        }
        if (r < ARCHIVE_WARN) {
            log_write("[ARCHIVE] data: %s\n", archive_error_string(a));
            R_THROW(Failure(a, src, Result_ArchiveDamaged));
        }
        R_TRY(out.Write(offset, block, size, FsWriteOption_None));
        pbox->UpdateInstallTransfer(src.Done(), src.total);
    }
}

// where an entry goes, or nullopt to skip it; `is_dir` entries only make their folder.
using Place = std::function<std::optional<fs::FsPath>(const char* name, bool raw, bool is_dir)>;

// the first volume of a .rar set as a path UnRAR can fopen(), or nullopt: not a rar, or
// a drive stdio cannot reach (UnRAR opens files by name). Stdio mounts (network, usb) are
// already stdio paths; the sd card is newlib's default device.
std::optional<std::string> RarPath(fs::Fs* fs, const fs::FsPath& archive_path) {
    if (fs->IsVirtual()) {
        return std::nullopt;
    }
    if (fs->IsNative()) {
        const auto sd = fsdevGetDeviceFileSystem("sdmc:");
        if (!sd || static_cast<fs::FsNative*>(fs)->m_fs.s.session != sd->s.session) {
            return std::nullopt;
        }
    }
    auto first = VolumePaths(archive_path.s).front();
    if (!rar::IsRarName(first)) {
        return std::nullopt;
    }
    return first;
}

// the unpacked size of every file: the progress total (reading headers is cheap).
Result RarTotal(const std::string& path, s64* total) {
    rar::Reader r;
    R_TRY(RarResult(r.Open(path, false)));
    while (true) {
        rar::Entry e;
        bool end{};
        R_TRY(RarResult(r.Next(&e, &end)));
        if (end) {
            R_SUCCEED();
        }
        *total += e.is_file ? e.size : 0;
        R_TRY(RarResult(r.Skip({})));
    }
}

// RAR through UnRAR: solid RAR4, RAR5 and split sets. Entries are written as UnRAR pushes
// their data; cancel and write errors stop it from inside the data callback.
Result RunRar(ui::InstallProgress* pbox, fs::Fs* fs, const std::string& path, const Place& place) {
    s64 total{}, done{};
    if (const auto rc = RarTotal(path, &total); R_FAILED(rc)) {
        log_write("[ARCHIVE] rar list %s: 0x%X\n", path.c_str(), rc);
        R_THROW(rc);
    }
    rar::Reader r;
    R_TRY(RarResult(r.Open(path, true)));
    pbox->UpdateInstallTransfer(0, total);

    Result sink_rc{};
    fs::File out;
    s64 off{};
    // a solid archive decompresses the entries it skips too: count them, let cancel stop it.
    const rar::Reader::Sink skip = [&](const void*, size_t n) {
        done += n;
        pbox->UpdateInstallTransfer(done, total);
        sink_rc = pbox->CheckCancelled();
        return R_SUCCEEDED(sink_rc);
    };
    const rar::Reader::Sink write = [&](const void* data, size_t n) {
        if (R_FAILED(sink_rc = pbox->CheckCancelled()) || R_FAILED(sink_rc = out.Write(off, data, n, FsWriteOption_None))) {
            return false;
        }
        off += n;
        done += n;
        pbox->UpdateInstallTransfer(done, total);
        return true;
    };
    const auto check = [&](rar::Error e) -> Result {
        if (e != rar::Error::None) {
            log_write("[ARCHIVE] rar %s: error %d\n", path.c_str(), static_cast<int>(e));
        }
        return e == rar::Error::Stopped ? sink_rc : RarResult(e);
    };

    while (true) {
        R_TRY(pbox->CheckCancelled());
        rar::Entry e;
        bool end{};
        R_TRY(check(r.Next(&e, &end)));
        if (end) {
            R_SUCCEED();
        }

        const auto dest = (e.is_file || e.is_dir) ? place(e.name.c_str(), false, e.is_dir) : std::nullopt;
        if (!dest || e.is_dir) {
            if (!dest) {
                log_write("[ARCHIVE] skipping %s\n", e.name.c_str());
            } else {
                R_TRY(fs->CreateDirectoryRecursively(*dest));
            }
            R_TRY(check(r.Skip(skip)));
            if (!r.solid && e.is_file) {
                done += e.size;
            }
            continue;
        }

        R_TRY(fs->CreateDirectoryRecursivelyWithPath(*dest));
        fs->DeleteFile(*dest);
        R_TRY(fs->CreateFile(*dest));
        R_TRY(fs->OpenFile(*dest, FsOpenMode_Write | FsOpenMode_Append, &out));
        off = 0;
        const auto rc = check(r.Read(write));
        out.Close();
        R_TRY(rc);
    }
}

Result Run(ui::InstallProgress* pbox, fs::Fs* fs, const fs::FsPath& archive_path, const Place& place) {
    if (const auto rar_path = RarPath(fs, archive_path)) {
        return RunRar(pbox, fs, *rar_path, place);
    }

    Source src;
    src.fs = fs;
    R_TRY(src.Open(archive_path));

    auto a = archive_read_new();
    ON_SCOPE_EXIT(archive_read_free(a));
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    // last resort: a bare .gz/.xz/.bz2 holds one unnamed stream.
    archive_read_support_format_raw(a);
    archive_read_set_read_callback(a, OnRead);
    archive_read_set_seek_callback(a, OnSeek);
    archive_read_set_switch_callback(a, OnSwitch);
    archive_read_set_callback_data(a, &src.volumes.front());
    for (size_t i = 1; i < src.volumes.size(); i++) {
        archive_read_append_callback_data(a, &src.volumes[i]);
    }
    if (archive_read_open1(a) != ARCHIVE_OK) {
        log_write("[ARCHIVE] open %s: %s\n", archive_path.s, archive_error_string(a));
        R_THROW(Failure(a, src, Result_ArchiveUnsupported));
    }

    pbox->UpdateInstallTransfer(0, src.total);
    struct archive_entry* entry;
    while (true) {
        R_TRY(pbox->CheckCancelled());
        const auto r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_WARN) {
            log_write("[ARCHIVE] header: %s\n", archive_error_string(a));
            R_THROW(Failure(a, src, Result_ArchiveDamaged));
        }

        const char* name = archive_entry_pathname_utf8(entry);
        if (!name) {
            name = archive_entry_pathname(entry);
        }
        const bool raw = archive_format(a) == ARCHIVE_FORMAT_RAW;
        const auto type = archive_entry_filetype(entry);
        const auto dest = (type == AE_IFREG || type == AE_IFDIR) ? place(name ? name : "", raw, type == AE_IFDIR) : std::nullopt;
        if (!dest) {
            log_write("[ARCHIVE] skipping %s\n", name ? name : "(no name)");
            archive_read_data_skip(a);
            continue;
        }

        if (type == AE_IFDIR) {
            R_TRY(fs->CreateDirectoryRecursively(*dest));
        } else {
            R_TRY(WriteEntry(pbox, a, fs, *dest, src));
        }
    }

    R_SUCCEED();
}

} // namespace

Result RarResult(rar::Error e) {
    switch (e) {
        case rar::Error::None: R_SUCCEED();
        case rar::Error::SourceRead: return Result_ArchiveSourceRead;
        case rar::Error::Unsupported: return Result_ArchiveUnsupported;
        case rar::Error::NoMemory: return Result_ArchiveNoMemory;
        case rar::Error::Password: return Result_ArchivePassword;
        case rar::Error::NotFound: return FsError_PathNotFound;
        case rar::Error::Stopped: return Result_TransferCancelled;
        case rar::Error::Damaged: break;
    }
    return Result_ArchiveDamaged;
}

Result Extract(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& archive_path, const fs::FsPath& out_dir) {
    pbox->NewTransfer("Extracting "_i18n);
    const auto place = [&](const char* name, bool raw, bool) -> std::optional<fs::FsPath> {
        const auto rel = OutputPath(name, raw, archive_path.s);
        if (!rel) {
            return std::nullopt;
        }
        pbox->SetTransfer(*rel);
        return fs::AppendPath(out_dir, rel->c_str());
    };
    return Run(pbox, fs, archive_path, place);
}

Result ExtractMapped(ui::InstallProgress* progress, fs::Fs* fs, const fs::FsPath& archive_path, const EntryMap& map) {
    const auto place = [&](const char* name, bool, bool is_dir) -> std::optional<fs::FsPath> {
        if (is_dir) {
            return std::nullopt; // folders come with their files
        }
        const auto dest = map(name);
        if (!dest) {
            return std::nullopt;
        }
        return fs::FsPath{*dest};
    };
    return Run(progress, fs, archive_path, place);
}

} // namespace sphaira::archive
