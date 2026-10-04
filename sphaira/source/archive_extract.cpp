#include "archive_extract.hpp"
#include "archive_extract_plan.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace sphaira::archive {
namespace {

// libarchive reads through fs::File so sd, usb drives and network mounts all work,
// and 7z / rar can seek.
struct Source {
    fs::File file{};
    s64 size{};
    s64 off{};
    std::vector<u8> buf = std::vector<u8>(256 * 1024);
};

la_ssize_t OnRead(struct archive* a, void* user, const void** out) {
    auto src = static_cast<Source*>(user);
    u64 bytes{};
    if (R_FAILED(src->file.Read(src->off, src->buf.data(), src->buf.size(), 0, &bytes))) {
        archive_set_error(a, EIO, "read failed");
        return -1;
    }
    src->off += bytes;
    *out = src->buf.data();
    return static_cast<la_ssize_t>(bytes);
}

la_int64_t OnSeek(struct archive*, void* user, la_int64_t offset, int whence) {
    auto src = static_cast<Source*>(user);
    const s64 base = whence == SEEK_CUR ? src->off : whence == SEEK_END ? src->size : 0;
    src->off = std::clamp<s64>(base + offset, 0, src->size);
    return src->off;
}

Result WriteEntry(ui::ProgressBox* pbox, struct archive* a, fs::Fs* fs, const fs::FsPath& dest, const Source& src) {
    R_TRY(fs->CreateDirectoryRecursivelyWithPath(dest));
    fs->DeleteFile(dest);
    R_TRY(fs->CreateFile(dest));
    fs::File out;
    R_TRY(fs->OpenFile(dest, FsOpenMode_Write | FsOpenMode_Append, &out));

    const void* block;
    size_t size;
    la_int64_t offset;
    while (true) {
        R_TRY(pbox->ShouldExitResult());
        const auto r = archive_read_data_block(a, &block, &size, &offset);
        if (r == ARCHIVE_EOF) {
            R_SUCCEED();
        }
        if (r < ARCHIVE_WARN) {
            log_write("[ARCHIVE] data: %s\n", archive_error_string(a));
            R_THROW(Result_ArchiveRead);
        }
        R_TRY(out.Write(offset, block, size, FsWriteOption_None));
        pbox->UpdateTransfer(src.off, src.size);
    }
}

} // namespace

Result Extract(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& archive_path, const fs::FsPath& out_dir) {
    Source src;
    R_TRY(fs->OpenFile(archive_path, FsOpenMode_Read, &src.file));
    R_TRY(src.file.GetSize(&src.size));

    auto a = archive_read_new();
    ON_SCOPE_EXIT(archive_read_free(a));
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    // last resort: a bare .gz/.xz/.bz2 holds one unnamed stream.
    archive_read_support_format_raw(a);
    archive_read_set_read_callback(a, OnRead);
    archive_read_set_seek_callback(a, OnSeek);
    archive_read_set_callback_data(a, &src);
    if (archive_read_open1(a) != ARCHIVE_OK) {
        log_write("[ARCHIVE] open %s: %s\n", archive_path.s, archive_error_string(a));
        R_THROW(Result_ArchiveRead);
    }

    pbox->NewTransfer("Extracting "_i18n).UpdateTransfer(0, src.size);
    struct archive_entry* entry;
    while (true) {
        R_TRY(pbox->ShouldExitResult());
        const auto r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_WARN) {
            log_write("[ARCHIVE] header: %s\n", archive_error_string(a));
            R_THROW(Result_ArchiveRead);
        }

        const char* name = archive_entry_pathname_utf8(entry);
        if (!name) {
            name = archive_entry_pathname(entry);
        }
        const bool raw = archive_format(a) == ARCHIVE_FORMAT_RAW;
        const auto rel = OutputPath(name ? name : "", raw, archive_path.s);
        const auto type = archive_entry_filetype(entry);
        if (!rel || (type != AE_IFREG && type != AE_IFDIR)) {
            log_write("[ARCHIVE] skipping %s\n", name ? name : "(no name)");
            archive_read_data_skip(a);
            continue;
        }

        const auto dest = fs::AppendPath(out_dir, rel->c_str());
        pbox->SetTransfer(*rel);
        if (type == AE_IFDIR) {
            R_TRY(fs->CreateDirectoryRecursively(dest));
        } else {
            R_TRY(WriteEntry(pbox, a, fs, dest, src));
        }
    }

    R_SUCCEED();
}

} // namespace sphaira::archive
