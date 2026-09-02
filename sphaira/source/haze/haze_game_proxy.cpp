#include "haze/haze_internal.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "title_info.hpp"
#include "title_export_name.hpp"
#include "title_nsp.hpp"
#include "mtp_games_path.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/progress_box.hpp"
#include <usbhsfs.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <functional>
#include <haze.h>

namespace sphaira::haze {

struct FsGameProxy final : FsProxyBase {
    FsGameProxy(const char* name, const char* display_name) : FsProxyBase{name, display_name} {
        // holds the ncm storages (and the control loader) open for our
        // lifetime - every listing and every read goes through them.
        m_has_title = R_SUCCEEDED(title::Init());
        m_is_file_based_emummc = App::IsFileBaseEmummc();
        m_layout = static_cast<sphaira::mtp::GamesLayout>(App::GetMtpGamesLayout());
        m_names.merged = "Merged"_i18n;
        m_names.separate = "Separate"_i18n;
        m_names.forwarders = "Forwarders"_i18n;
        m_names.readme = "Readme.txt"_i18n;
        ScanGames();
        log_write("[MTP-GAMES] layout=%d scanned %zu games, %zu forwarders\n",
            static_cast<int>(m_layout), m_games.size(), m_forwarders.size());
    }

    ~FsGameProxy() {
        if (m_has_title) {
            title::Exit();
        }
    }

    Result GetTotalSpace(const char *path, s64 *out) override {
        *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
        R_SUCCEED();
    }
    Result GetFreeSpace(const char *path, s64 *out) override {
        // read-only drive: nothing can be written into it.
        *out = 0;
        R_SUCCEED();
    }

    Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) override {
        const auto pp = Parse(path);

        switch (pp.kind) {
            case sphaira::mtp::PathKind::Root:
            case sphaira::mtp::PathKind::MergedDir:
            case sphaira::mtp::PathKind::SeparateDir:
            case sphaira::mtp::PathKind::ForwardersDir:
                *out_entry_type = FsDirEntryType_Dir;
                R_SUCCEED();

            case sphaira::mtp::PathKind::SeparateGameDir: {
                R_UNLESS(m_games.count(pp.game), FsError_PathNotFound);
                *out_entry_type = FsDirEntryType_Dir;
                R_SUCCEED();
            }

            case sphaira::mtp::PathKind::InfoFile:
                *out_entry_type = FsDirEntryType_File;
                R_SUCCEED();

            case sphaira::mtp::PathKind::MergedFile: {
                std::shared_ptr<GameNsp> nsp;
                size_t index{};
                R_TRY(FindMergedFile(pp.filename, nsp, index));
                *out_entry_type = FsDirEntryType_File;
                R_SUCCEED();
            }

            case sphaira::mtp::PathKind::ForwardersFile: {
                std::shared_ptr<GameNsp> nsp;
                size_t index{};
                R_TRY(FindForwarderFile(pp.filename, nsp, index));
                *out_entry_type = FsDirEntryType_File;
                R_SUCCEED();
            }

            case sphaira::mtp::PathKind::SeparateFile: {
                std::shared_ptr<GameNsp> nsp;
                size_t index{};
                R_TRY(FindFile(pp.game, pp.filename, nsp, index));
                *out_entry_type = FsDirEntryType_File;
                R_SUCCEED();
            }

            default:
                R_THROW(FsError_PathNotFound);
        }
    }

    // the drive is a view of installed content: everything that would modify
    // it is rejected, same as the saves drive.
    Result CreateFile(const char* path, s64 size, u32 option) override {
        log_write("[MTP-GAMES] rejecting CreateFile(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteFile(const char* path) override {
        log_write("[MTP-GAMES] rejecting DeleteFile(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result RenameFile(const char *old_path, const char *new_path) override {
        log_write("[MTP-GAMES] rejecting RenameFile(%s)\n", old_path);
        R_THROW(FsError_NotImplemented);
    }
    Result CreateDirectory(const char* path) override {
        log_write("[MTP-GAMES] rejecting CreateDirectory(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteDirectoryRecursively(const char* path) override {
        log_write("[MTP-GAMES] rejecting DeleteDirectoryRecursively(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result RenameDirectory(const char *old_path, const char *new_path) override {
        log_write("[MTP-GAMES] rejecting RenameDirectory(%s)\n", old_path);
        R_THROW(FsError_NotImplemented);
    }
    Result SetFileSize(FsFile *file, s64 size) override {
        log_write("[MTP-GAMES] rejecting SetFileSize()\n");
        R_THROW(FsError_NotImplemented);
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
        log_write("[MTP-GAMES] rejecting WriteFile()\n");
        R_THROW(FsError_NotImplemented);
    }

    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        log_write("[MTP-GAMES] OpenFile(%s)\n", path);
        R_UNLESS(!(mode & (FsOpenMode_Write | FsOpenMode_Append)), FsError_NotImplemented);

        const auto pp = Parse(path);
        auto handle = std::make_unique<FileHandle>();

        if (pp.kind == sphaira::mtp::PathKind::InfoFile) {
            handle->info = InfoText(pp.game);
        } else if (pp.kind == sphaira::mtp::PathKind::MergedFile) {
            R_TRY(FindMergedFile(pp.filename, handle->nsp, handle->index));
        } else if (pp.kind == sphaira::mtp::PathKind::ForwardersFile) {
            R_TRY(FindForwarderFile(pp.filename, handle->nsp, handle->index));
        } else if (pp.kind == sphaira::mtp::PathKind::SeparateFile) {
            R_TRY(FindFile(pp.game, pp.filename, handle->nsp, handle->index));
        } else {
            R_THROW(FsError_PathNotFound);
        }

        auto raw = handle.release();
        std::memcpy(&out_file->s, &raw, sizeof(raw));
        R_SUCCEED();
    }
    Result GetFileSize(FsFile *file, s64 *out_size) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        if (!h->info.empty()) {
            *out_size = static_cast<s64>(h->info.size());
            R_SUCCEED();
        }
        *out_size = h->nsp->entries[h->index].nsp_size;
        R_SUCCEED();
    }
    Result ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));

        if (!h->info.empty()) {
            *out_bytes_read = 0;
            if (off < 0 || off >= static_cast<s64>(h->info.size())) {
                R_SUCCEED();
            }
            const auto n = std::min<u64>(read_size, static_cast<u64>(h->info.size() - off));
            std::memcpy(buf, h->info.data() + off, n);
            *out_bytes_read = n;
            R_SUCCEED();
        }

        const auto rc = h->nsp->entries[h->index].Read(buf, off, read_size, out_bytes_read);
        if (m_is_file_based_emummc) {
            svcSleepThread(2e+6); // 2ms, same throttle the sd card dump uses.
        }
        return rc;
    }
    void CloseFile(FsFile *file) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        delete h;
        std::memset(file, 0, sizeof(*file));
    }

    Result OpenDirectory(const char *path, u32 mode, FsDir *out_dir) override {
        const auto pp = Parse(path);
        auto handle = std::make_unique<DirHandle>();

        switch (pp.kind) {
            case sphaira::mtp::PathKind::Root: {
                if (m_layout == sphaira::mtp::GamesLayout::Compatible) {
                    if (mode & FsDirOpenMode_ReadFiles) {
                        R_TRY(BuildMergedCacheIfNeeded());
                        SCOPED_MUTEX(&m_cache_mutex);
                        for (const auto& [name, nsp] : m_merged_cache) {
                            if (!nsp->listing.empty()) {
                                handle->entries.emplace_back(nsp->listing[0]);
                            }
                        }
                        handle->entries.emplace_back(InfoDirEntry(""));
                    }
                    if (mode & FsDirOpenMode_ReadDirs) {
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                    }
                } else if (m_layout == sphaira::mtp::GamesLayout::Separate) {
                    if (mode & FsDirOpenMode_ReadDirs) {
                        for (const auto& [name, game] : m_games) {
                            handle->entries.emplace_back(MakeVirtualDirEntry(name));
                        }
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                    }
                    if (mode & FsDirOpenMode_ReadFiles) {
                        handle->entries.emplace_back(InfoDirEntry(""));
                    }
                } else {
                    if (mode & FsDirOpenMode_ReadDirs) {
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.merged));
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.separate));
                        handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                    }
                    if (mode & FsDirOpenMode_ReadFiles) {
                        handle->entries.emplace_back(InfoDirEntry(""));
                    }
                }
                break;
            }

            case sphaira::mtp::PathKind::MergedDir: {
                if (mode & FsDirOpenMode_ReadFiles) {
                    R_TRY(BuildMergedCacheIfNeeded());
                    SCOPED_MUTEX(&m_cache_mutex);
                    for (const auto& [name, nsp] : m_merged_cache) {
                        if (!nsp->listing.empty()) {
                            handle->entries.emplace_back(nsp->listing[0]);
                        }
                    }
                    handle->entries.emplace_back(InfoDirEntry("merged"));
                }
                break;
            }

            case sphaira::mtp::PathKind::ForwardersDir: {
                if (mode & FsDirOpenMode_ReadFiles) {
                    R_TRY(BuildForwarderCacheIfNeeded());
                    SCOPED_MUTEX(&m_cache_mutex);
                    for (const auto& [name, nsp] : m_forwarder_cache) {
                        if (!nsp->listing.empty()) {
                            handle->entries.emplace_back(nsp->listing[0]);
                        }
                    }
                    handle->entries.emplace_back(InfoDirEntry("forwarders"));
                }
                break;
            }

            case sphaira::mtp::PathKind::SeparateDir: {
                if (mode & FsDirOpenMode_ReadDirs) {
                    for (const auto& [name, game] : m_games) {
                        handle->entries.emplace_back(MakeVirtualDirEntry(name));
                    }
                }
                if (mode & FsDirOpenMode_ReadFiles) {
                    handle->entries.emplace_back(InfoDirEntry("separate"));
                }
                break;
            }

            case sphaira::mtp::PathKind::SeparateGameDir: {
                std::shared_ptr<GameNsp> nsp;
                R_TRY(GetNsp(pp.game, nsp));
                if (mode & FsDirOpenMode_ReadFiles) {
                    handle->entries = nsp->listing;
                }
                break;
            }

            default:
                R_THROW(FsError_PathNotFound);
        }

        auto raw = handle.release();
        std::memcpy(&out_dir->s, &raw, sizeof(raw));
        log_write("[MTP-GAMES] OpenDirectory(%s) kind=%d\n", path, static_cast<int>(pp.kind));
        R_SUCCEED();
    }
    Result ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));

        max_entries = std::min<s64>(h->entries.size() - h->index, max_entries);
        std::memcpy(buf, h->entries.data() + h->index, max_entries * sizeof(*buf));
        h->index += max_entries;
        *out_total_entries = max_entries;
        R_SUCCEED();
    }
    Result GetDirectoryEntryCount(FsDir *d, s64 *out_count) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));
        *out_count = h->entries.size();
        R_SUCCEED();
    }
    void CloseDirectory(FsDir *d) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));
        delete h;
        std::memset(d, 0, sizeof(*d));
    }

    // ncm reads are not safe to issue concurrently, one reader thread only.
    bool MultiThreadTransfer(s64 size, bool read) override {
        return false;
    }

private:
    // how many games keep their built nsp headers around.
    static constexpr size_t CACHE_MAX = 8;

    struct GameRec {
        u64 app_id{};
        // display name as it comes from the control data, may be empty.
        std::string name{};
    };

    // every installed component of one game, plus the directory listing that
    // describes them.
    struct GameNsp {
        std::vector<title::NspEntry> entries{};
        std::vector<FsDirectoryEntry> listing{};
    };

    // open handles hold their own reference, so a cache flush never pulls the
    // nsp out from under a transfer in progress. `info` is a virtual readme.
    struct FileHandle {
        std::shared_ptr<GameNsp> nsp{};
        size_t index{};
        std::string info{};
    };

    struct DirHandle {
        std::vector<FsDirectoryEntry> entries{};
        s64 index{};
    };

    // one folder per installed title. archived titles (a record with no
    // content) are skipped - there would be nothing to dump.
    void ScanGames() {
        constexpr auto RECORD_CHUNK = 1000;
        std::vector<NsApplicationRecord> records(RECORD_CHUNK);

        s32 offset{};
        while (true) {
            s32 count{};
            if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &count)) || !count) {
                break;
            }

            for (s32 i = 0; i < count; i++) {
                const auto app_id = records[i].application_id;

                title::MetaEntries installed;
                if (R_FAILED(title::GetMetaEntries(app_id, installed)) || installed.empty()) {
                    continue;
                }

                GameRec rec{app_id};
                if (const auto data = title::Get(app_id); data && data->status == title::NacpLoadStatus::Loaded) {
                    rec.name = data->lang.name;
                }

                if (sphaira::mtp::IsForwarderTitleId(app_id)) {
                    m_forwarders.emplace(BuildGameDirName(app_id), std::move(rec));
                } else {
                    m_games.emplace(BuildGameDirName(app_id), std::move(rec));
                }
            }

            offset += count;
        }

        log_write("[MTP-GAMES] scanned %zu games, %zu forwarders\n",
            m_games.size(), m_forwarders.size());
    }

    Result GetNsp(const std::string& game, std::shared_ptr<GameNsp>& out) {
        const auto it = m_games.find(game);
        R_UNLESS(it != m_games.end(), FsError_PathNotFound);

        // canonical key, so differently-cased requests share one build.
        const auto& key = it->first;

        // ops run on the haze thread(s) and may interleave, guard the cache.
        SCOPED_MUTEX(&m_cache_mutex);

        if (const auto cached = m_cache.find(key); cached != m_cache.end()) {
            out = cached->second;
            R_SUCCEED();
        }

        auto nsp = std::make_shared<GameNsp>();
        const auto rc = title::BuildNspEntries(it->second.app_id, it->second.name.c_str(), title::ContentFlag_All, false, nsp->entries);
        if (R_FAILED(rc)) {
            // e.g. missing keys or a corrupt install - only this game fails.
            log_write("[MTP-GAMES] failed to build nsp for %s 0x%X\n", key.c_str(), rc);
            return rc;
        }

        for (const auto& e : nsp->entries) {
            FsDirectoryEntry fe{};
            std::snprintf(fe.name, sizeof(fe.name), "%s", e.path.s);
            fe.type = FsDirEntryType_File;
            fe.file_size = e.nsp_size;
            nsp->listing.emplace_back(fe);
        }

        // a cached game is only its nsp header, file table and tickets (tens of
        // kb), so the whole cache is dropped at once rather than tracking an
        // lru - the cost of a miss is one rebuild.
        if (m_cache.size() >= CACHE_MAX) {
            m_cache.clear();
        }

        m_cache.emplace(key, nsp);
        out = nsp;
        R_SUCCEED();
    }

    Result BuildMergedCacheIfNeeded() {
        SCOPED_MUTEX(&m_cache_mutex);
        if (m_merged_built) {
            R_SUCCEED();
        }

        // ponytail: Merged listing needs exact sizes, so headers/tickets stay cached
        // for this MTP session; add lightweight sizing only if hardware measurements
        // show memory or listing latency is a problem.
        for (const auto& [dir_name, rec] : m_games) {
            title::NspEntry entry;
            const auto rc = title::BuildMergedNspEntry(rec.app_id, rec.name.c_str(), entry);
            if (R_FAILED(rc)) {
                log_write("[MTP-GAMES] failed to build merged nsp for %s 0x%X\n", dir_name.c_str(), rc);
                continue;
            }

            auto nsp = std::make_shared<GameNsp>();
            FsDirectoryEntry fe{};
            std::snprintf(fe.name, sizeof(fe.name), "%s", entry.path.s);
            fe.type = FsDirEntryType_File;
            fe.file_size = entry.nsp_size;

            nsp->entries.emplace_back(std::move(entry));
            nsp->listing.emplace_back(fe);

            m_merged_cache.emplace(fe.name, std::move(nsp));
        }

        m_merged_built = true;
        R_SUCCEED();
    }

    Result FindMergedFile(const std::string& filename, std::shared_ptr<GameNsp>& nsp, size_t& index) {
        R_TRY(BuildMergedCacheIfNeeded());

        SCOPED_MUTEX(&m_cache_mutex);
        const auto it = m_merged_cache.find(filename);
        R_UNLESS(it != m_merged_cache.end(), FsError_PathNotFound);

        nsp = it->second;
        index = 0;
        R_SUCCEED();
    }

    Result BuildForwarderCacheIfNeeded() {
        SCOPED_MUTEX(&m_cache_mutex);
        if (m_forwarder_built) {
            R_SUCCEED();
        }

        for (const auto& [dir_name, rec] : m_forwarders) {
            title::NspEntry entry;
            const auto rc = title::BuildMergedNspEntry(rec.app_id, rec.name.c_str(), entry);
            if (R_FAILED(rc)) {
                log_write("[MTP-GAMES] failed to build forwarder nsp for %s 0x%X\n", dir_name.c_str(), rc);
                continue;
            }

            auto nsp = std::make_shared<GameNsp>();
            FsDirectoryEntry fe{};
            std::snprintf(fe.name, sizeof(fe.name), "%s", entry.path.s);
            fe.type = FsDirEntryType_File;
            fe.file_size = entry.nsp_size;

            nsp->entries.emplace_back(std::move(entry));
            nsp->listing.emplace_back(fe);

            m_forwarder_cache.emplace(fe.name, std::move(nsp));
        }

        m_forwarder_built = true;
        R_SUCCEED();
    }

    Result FindForwarderFile(const std::string& filename, std::shared_ptr<GameNsp>& nsp, size_t& index) {
        R_TRY(BuildForwarderCacheIfNeeded());

        SCOPED_MUTEX(&m_cache_mutex);
        const auto it = m_forwarder_cache.find(filename);
        R_UNLESS(it != m_forwarder_cache.end(), FsError_PathNotFound);

        nsp = it->second;
        index = 0;
        R_SUCCEED();
    }

    Result FindFile(const std::string& game, const std::string& file, std::shared_ptr<GameNsp>& nsp, size_t& index) {
        R_TRY(GetNsp(game, nsp));

        for (size_t i = 0; i < nsp->entries.size(); i++) {
            if (!strcasecmp(nsp->entries[i].path.s, file.c_str())) {
                index = i;
                R_SUCCEED();
            }
        }

        R_THROW(FsError_PathNotFound);
    }

    auto Parse(const char* path) const -> sphaira::mtp::ParsedPath {
        // libhaze names the storage root "/games"; strip that so listings and
        // reads see "/", "/Merged", "/Separate/...", "/Forwarders" like the
        // host tests (and the locale's names, which m_names holds).
        return sphaira::mtp::ParseGamesPath(FixPath(path).s, m_layout, m_names);
    }

    auto InfoText(std::string_view which) const -> std::string {
        if (which == "merged") {
            return "Each file is one game. Base, update and DLC are packed into a single NSP. Use this when the installer expects one file per title."_i18n + "\n";
        }
        if (which == "separate") {
            return "Each folder is one game. Inside are separate NSP files for the base, the update and each DLC."_i18n + "\n";
        }
        if (which == "forwarders") {
            return "HOME-menu forwarders (homebrew icons). These are not Nintendo games. Copy one to dump that icon as an NSP."_i18n + "\n";
        }
        return "This drive dumps installed titles as NSP files you can copy to a PC."_i18n + "\n\n"
            + m_names.merged + " — " + "One NSP per game with base, update and DLC together."_i18n + "\n"
            + m_names.separate + " — " + "A folder per game with each component as its own NSP."_i18n + "\n"
            + m_names.forwarders + " — " + "HOME-menu homebrew icons, not Nintendo games."_i18n + "\n";
    }

    auto InfoDirEntry(std::string_view which) const -> FsDirectoryEntry {
        return MakeVirtualFileEntry(m_names.readme, static_cast<s64>(InfoText(which).size()));
    }

    // game folder name -> title. built once at registration, immutable
    // afterwards (safe for concurrent reads). forwarders live in m_forwarders.
    std::map<std::string, GameRec, CaseInsensitiveLess> m_games{};
    std::map<std::string, GameRec, CaseInsensitiveLess> m_forwarders{};

    // built on demand, see GetNsp().
    std::map<std::string, std::shared_ptr<GameNsp>, CaseInsensitiveLess> m_cache{};
    std::map<std::string, std::shared_ptr<GameNsp>, CaseInsensitiveLess> m_merged_cache{};
    std::map<std::string, std::shared_ptr<GameNsp>, CaseInsensitiveLess> m_forwarder_cache{};
    bool m_merged_built{false};
    bool m_forwarder_built{false};
    Mutex m_cache_mutex{};

    bool m_has_title{};
    bool m_is_file_based_emummc{};
    sphaira::mtp::GamesLayout m_layout{sphaira::mtp::GamesLayout::Both};
    sphaira::mtp::GamesFolderNames m_names{};
};

std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsGameProxy(const char* name, const char* display_name) {
    return std::make_shared<FsGameProxy>(name, display_name);
}

} // namespace sphaira::haze
