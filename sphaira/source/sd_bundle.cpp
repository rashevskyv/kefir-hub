#include "sd_bundle.hpp"
#include "sd_bundle_rules.hpp"
#include "archive_extract.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <vector>

namespace sphaira::sd_bundle {
namespace {

// an archive member waits here until it is extracted, then it is deleted.
constexpr const char* TMP_DIR = "/config/kefir/tmp";
constexpr size_t MAX_RULES_SIZE = 1024 * 1024;

// the tar comes as one forward stream of FILE_RANGE reads; libarchive needs no seek for tar.
struct Stream {
    yati::source::Base* source{};
    s64 size{};
    s64 off{};
    Result rc{};
    std::vector<u8> buf = std::vector<u8>(1024 * 1024);
};

la_ssize_t OnRead(struct archive* a, void* user, const void** out) {
    auto s = static_cast<Stream*>(user);
    const auto want = std::min<s64>(s->buf.size(), s->size - s->off);
    if (want <= 0) {
        return 0;
    }
    u64 bytes{};
    s->rc = s->source->Read(s->buf.data(), s->off, want, &bytes);
    if (R_FAILED(s->rc) || !bytes) {
        archive_set_error(a, EIO, "read failed");
        return -1;
    }
    s->off += bytes;
    *out = s->buf.data();
    return static_cast<la_ssize_t>(bytes);
}

Result ReadFailed(const Stream& s) {
    return R_FAILED(s.rc) ? s.rc : Result_ArchiveRead;
}

Result WriteMember(ui::InstallProgress* p, struct archive* a, fs::Fs* fs, const fs::FsPath& dest, const Stream& s, s64& written) {
    R_TRY(fs->CreateDirectoryRecursivelyWithPath(dest));
    fs->DeleteFile(dest);
    R_TRY(fs->CreateFile(dest));
    fs::File out;
    R_TRY(fs->OpenFile(dest, FsOpenMode_Write | FsOpenMode_Append, &out));

    const void* block;
    size_t size;
    la_int64_t offset;
    while (true) {
        R_TRY(p->CheckCancelled());
        const auto r = archive_read_data_block(a, &block, &size, &offset);
        if (r == ARCHIVE_EOF) {
            R_SUCCEED();
        }
        if (r < ARCHIVE_WARN) {
            log_write("[SDBUNDLE] data %s: %s\n", dest.s, archive_error_string(a));
            R_THROW(ReadFailed(s));
        }
        R_TRY(out.Write(offset, block, size, FsWriteOption_None));
        written += size;
        p->UpdateInstallTransfer(s.off, s.size);
        p->UpdateInstallReadWrite(s.off, written);
    }
}

Result ReadRules(struct archive* a, const Stream& s, std::vector<Rule>& rules) {
    std::string text;
    char buf[4096];
    while (true) {
        const auto r = archive_read_data(a, buf, sizeof(buf));
        if (r == 0) {
            break;
        }
        if (r < 0) {
            R_THROW(ReadFailed(s));
        }
        text.append(buf, r);
        R_UNLESS(text.size() <= MAX_RULES_SIZE, Result_ArchiveRead);
    }
    rules = ParseRules(text);
    R_SUCCEED();
}

} // namespace

Result Receive(ui::InstallProgress* p, yati::source::Base* source, s64 size) {
    R_UNLESS(size > 0, Result_ArchiveRead);
    fs::FsNativeSd fs;
    Stream s{source, size};

    auto a = archive_read_new();
    ON_SCOPE_EXIT(archive_read_free(a));
    archive_read_support_format_tar(a);
    if (archive_read_open(a, &s, nullptr, OnRead, nullptr) != ARCHIVE_OK) {
        log_write("[SDBUNDLE] open: %s\n", archive_error_string(a));
        R_THROW(ReadFailed(s));
    }

    // a port's save that is already on the console stays: a reinstall must not reset progress.
    const auto allowed = [&fs](std::string_view path) {
        return IsWritable(path) && !(IsSaveData(path) && fs.FileExists(fs::AppendPath("/", std::string{path}.c_str())));
    };

    std::vector<Rule> rules;
    s64 written{};
    u32 files{}, skipped{}, archives{};
    struct archive_entry* entry;
    while (true) {
        R_TRY(p->CheckCancelled());
        const auto r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_WARN) {
            log_write("[SDBUNDLE] header: %s\n", archive_error_string(a));
            R_THROW(ReadFailed(s));
        }

        const char* raw = archive_entry_pathname_utf8(entry);
        if (!raw) {
            raw = archive_entry_pathname(entry);
        }
        const auto path = CleanEntry(raw ? raw : "");
        if (!path || archive_entry_filetype(entry) != AE_IFREG) {
            archive_read_data_skip(a);
            continue;
        }

        if (*path == RULES) {
            R_TRY(ReadRules(a, s, rules));
        } else if (path->starts_with(META_DIR)) {
            // an archive of the release, sent as is: extract the entries the rules map.
            const auto tmp = fs::AppendPath(TMP_DIR, path->substr(META_DIR.size()).c_str());
            R_TRY(WriteMember(p, a, &fs, tmp, s, written));
            ON_SCOPE_EXIT(fs.DeleteFile(tmp));
            const auto map = [&rules, &allowed](std::string_view e) -> std::optional<std::string> {
                auto out = MapEntry(e, rules);
                if (!out || !allowed(*out)) {
                    return std::nullopt;
                }
                return "/" + *out;
            };
            R_TRY(archive::ExtractMapped(p, &fs, tmp, map));
            rules.clear();
            archives++;
        } else if (!allowed(*path)) {
            skipped++;
            archive_read_data_skip(a);
        } else {
            R_TRY(WriteMember(p, a, &fs, fs::AppendPath("/", path->c_str()), s, written));
            files++;
        }
    }

    log_write("[SDBUNDLE] done: %u files, %u archives, %u kept or refused, %lld bytes\n",
        files, archives, skipped, (long long)written);
    R_SUCCEED();
}

} // namespace sphaira::sd_bundle
