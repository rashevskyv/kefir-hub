#include "sd_bundle.hpp"
#include "sd_bundle_rules.hpp"
#include "save_bundle_rules.hpp"
#include "archive_extract.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_slot_backend.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
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

Result ReadText(struct archive* a, const Stream& s, std::string& text) {
    text.clear();
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
    R_SUCCEED();
}

Result OpenTar(struct archive* a, Stream& s) {
    archive_read_support_format_tar(a);
    if (archive_read_open(a, &s, nullptr, OnRead, nullptr) != ARCHIVE_OK) {
        log_write("[SDBUNDLE] open: %s\n", archive_error_string(a));
        R_THROW(ReadFailed(s));
    }
    R_SUCCEED();
}

// ---- console saves (save_bundle_rules.hpp) ----

u64 SaveOwner(u64 app_id) {
    auto data = std::make_unique<NsApplicationControlData>();
    u64 size{};
    if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, data.get(), sizeof(*data), &size)) ||
        size < sizeof(NacpStruct)) {
        return 0;
    }
    return data->nacp.save_data_owner_id;
}

// the storage of `attr` as the console has it, else created; grown when it holds less than `need`.
Result PrepareStorage(const FsSaveDataAttribute& attr, s64 need, FsSaveDataSpaceId& space, s64& journal) {
    using namespace ui::menu::save;
    const bool cache = attr.save_data_type == FsSaveDataType_Cache;
    for (const auto& info : DiscoverSaveDataInfo(cache ? nullptr : &attr.uid, attr.save_data_type)) {
        if (info.application_id != attr.application_id || info.save_data_index != attr.save_data_index ||
            info.save_data_rank != FsSaveDataRank_Primary || std::memcmp(&info.uid, &attr.uid, sizeof(attr.uid))) {
            continue;
        }
        space = static_cast<FsSaveDataSpaceId>(info.save_data_space_id);
        FsSaveDataExtraData extra{};
        R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), space, info.save_data_id));
        journal = extra.journal_size;
        if (extra.data_size >= need) {
            R_SUCCEED();
        }
        SaveGrowRequest grow{};
        grow.target_info = info;
        grow.space_id = space;
        grow.requested_data_size = save_bundle::AlignUp(need);
        grow.requested_journal_size = extra.journal_size;
        const auto res = ExtendSaveDataChecked(grow);
        log_write("[SAVEBUNDLE] grow %016lX/%u to %lld: 0x%X\n", attr.application_id, attr.save_data_index, (long long)need, res.rc);
        R_UNLESS(res.verified, R_FAILED(res.rc) ? res.rc : static_cast<Result>(FsError_InvalidSize));
        journal = res.actual_journal_size;
        R_SUCCEED();
    }

    // a new storage. A cache gets the game's own size or `need`, whichever is larger, on the
    // microSD like the game makes it; a user's save gets the game's size.
    SaveArchiveSizing sizing{};
    sizing.has_sizing = sizing.has_metadata = true;
    sizing.data_size = save_bundle::AlignUp(need);
    sizing.journal_size = save_bundle::MIN_JOURNAL;
    sizing.owner_id = SaveOwner(attr.application_id);
    sizing.attr = attr;
    SaveCreationRequest req{};
    SaveBackendStatus status{};
    const auto plan_rc = PlanAccountSaveCreation(attr.application_id, attr.uid, cache ? &sizing : nullptr, req, &status,
        attr.save_data_type, attr.save_data_index, cache ? FsSaveDataSpaceId_SdUser : FsSaveDataSpaceId_User);
    if (R_FAILED(plan_rc)) {
        log_write("[SAVEBUNDLE] plan %016lX/%u: %s\n", attr.application_id, attr.save_data_index, GetBackendStatusMessage(status).c_str());
        R_THROW(plan_rc);
    }
    const auto res = CreateSaveDataChecked(req);
    log_write("[SAVEBUNDLE] create %016lX/%u data %lld journal %lld: 0x%X\n", attr.application_id, attr.save_data_index,
        (long long)req.data_size, (long long)req.journal_size, res.rc);
    R_UNLESS(res.verified, R_FAILED(res.rc) ? res.rc : static_cast<Result>(FsError_PathNotFound));
    space = req.space_id;
    journal = res.verified_extra.journal_size;
    R_SUCCEED();
}

// one open storage. A save's journal holds only so much uncommitted data, so writes commit
// every `budget` bytes; a commit needs every written file closed.
struct SaveTarget {
    std::unique_ptr<fs::FsNativeSave> fs;
    FsSaveDataAttribute attr{};
    s64 budget{};
    s64 pending{};

    Result Commit() {
        pending = 0;
        return fs->Commit();
    }
};

Result OpenTarget(SaveTarget& t, u64 app_id, FsSaveDataType type, u16 index, const AccountUid& uid, s64 need) {
    if (t.fs) {
        R_TRY(t.Commit());
        t.fs.reset();
    }
    t.attr = {};
    t.attr.application_id = app_id;
    t.attr.uid = uid;
    t.attr.save_data_type = type;
    t.attr.save_data_rank = FsSaveDataRank_Primary;
    t.attr.save_data_index = index;
    FsSaveDataSpaceId space{};
    s64 journal{};
    R_TRY(PrepareStorage(t.attr, need, space, journal));
    t.fs = std::make_unique<fs::FsNativeSave>(static_cast<FsSaveDataType>(type), space, &t.attr, false);
    R_TRY(t.fs->GetFsOpenResult());
    t.budget = save_bundle::CommitBudget(journal);
    t.pending = 0;
    R_SUCCEED();
}

Result WriteSaveMember(ui::InstallProgress* p, struct archive* a, SaveTarget& t, const fs::FsPath& dest, s64 size, const Stream& s, s64& written) {
    auto& fs = *t.fs;
    R_TRY(fs.CreateDirectoryRecursivelyWithPath(dest));
    fs.DeleteFile(dest);
    R_TRY(fs.CreateFile(dest, size, 0));
    R_TRY(t.Commit());

    fs::File out; // closing a written file also commits (fs::File::Close)
    bool open = false;
    const void* block;
    size_t block_size;
    la_int64_t offset;
    while (true) {
        R_TRY(p->CheckCancelled());
        const auto r = archive_read_data_block(a, &block, &block_size, &offset);
        if (r == ARCHIVE_EOF) {
            R_SUCCEED();
        }
        if (r < ARCHIVE_WARN) {
            log_write("[SAVEBUNDLE] data %s: %s\n", dest.s, archive_error_string(a));
            R_THROW(ReadFailed(s));
        }
        auto data = static_cast<const u8*>(block);
        while (block_size) {
            const auto piece = std::min<s64>(block_size, t.budget - t.pending);
            if (!open) {
                R_TRY(fs.OpenFile(dest, FsOpenMode_Write, &out));
                open = true;
            }
            R_TRY(out.Write(offset, data, piece, FsWriteOption_None));
            data += piece;
            offset += piece;
            block_size -= piece;
            t.pending += piece;
            written += piece;
            if (t.pending >= t.budget) {
                out.Close();
                open = false;
                R_TRY(t.Commit());
            }
        }
        p->UpdateInstallTransfer(s.off, s.size);
        p->UpdateInstallReadWrite(s.off, written);
    }
}

} // namespace

Result Receive(ui::InstallProgress* p, yati::source::Base* source, s64 size) {
    R_UNLESS(size > 0, Result_ArchiveRead);
    p->SetInstallTransfer("Writing files to the microSD"_i18n); // else the last package's step stays on screen
    fs::FsNativeSd fs;
    Stream s{source, size};

    auto a = archive_read_new();
    ON_SCOPE_EXIT(archive_read_free(a));
    R_TRY(OpenTar(a, s));

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
            std::string text;
            R_TRY(ReadText(a, s, text));
            rules = ParseRules(text);
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

Result ReceiveSave(ui::InstallProgress* p, yati::source::Base* source, s64 size, bool cache, u64 app_id, const AccountUid& uid) {
    R_UNLESS(size > 0 && app_id, Result_ArchiveRead);
    p->SetInstallTransfer("Restoring save..."_i18n);
    Stream s{source, size};

    auto a = archive_read_new();
    ON_SCOPE_EXIT(archive_read_free(a));
    R_TRY(OpenTar(a, s));

    std::map<u16, s64> sizes;
    SaveTarget t;
    std::optional<u16> current;
    s64 written{};
    u32 files{}, kept{};
    struct archive_entry* entry;
    while (true) {
        R_TRY(p->CheckCancelled());
        const auto r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_WARN) {
            log_write("[SAVEBUNDLE] header: %s\n", archive_error_string(a));
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
        if (*path == save_bundle::SIZES) {
            std::string text;
            R_TRY(ReadText(a, s, text));
            sizes = save_bundle::ParseSizes(text);
            continue;
        }

        u16 index = 0;
        std::string inner = *path;
        if (cache) {
            auto e = save_bundle::ParseCacheEntry(*path);
            if (!e) {
                archive_read_data_skip(a);
                continue;
            }
            index = e->index;
            inner = std::move(e->path);
        }
        if (!current || *current != index) {
            const auto it = sizes.find(index);
            const s64 need = cache ? save_bundle::StorageSize(it == sizes.end() ? 0 : it->second) : 0;
            R_TRY(OpenTarget(t, app_id, cache ? FsSaveDataType_Cache : FsSaveDataType_Account, index, cache ? AccountUid{} : uid, need));
            current = index;
        }
        const auto dest = fs::AppendPath("/", inner.c_str());
        if (!cache && t.fs->FileExists(dest)) {
            kept++;
            archive_read_data_skip(a);
            continue;
        }
        R_TRY(WriteSaveMember(p, a, t, dest, archive_entry_size(entry), s, written));
        files++;
    }
    if (t.fs) {
        R_TRY(t.Commit());
    }
    log_write("[SAVEBUNDLE] done %016lX: %u files, %u kept, %lld bytes\n", app_id, files, kept, (long long)written);
    R_SUCCEED();
}

} // namespace sphaira::sd_bundle
