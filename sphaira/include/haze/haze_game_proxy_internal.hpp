#pragma once

// FsGameProxy: the MTP "Games" drive. Filesystem ops live in haze_game_proxy.cpp,
// the game scan and nsp caches in haze_game_proxy_catalog.cpp.

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
        m_names.mods = "Mods"_i18n;
        ScanGames();
        log_write("[MTP-GAMES] layout=%d scanned %zu games, %zu forwarders\n",
            static_cast<int>(m_layout), m_games.size(), m_forwarders.size());
    }

    ~FsGameProxy() {
        if (m_has_title) {
            title::Exit();
        }
    }

    Result GetTotalSpace(const char *path, s64 *out) override;
    Result GetFreeSpace(const char *path, s64 *out) override;
    Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) override;

    // the drive is a view of installed content: everything that would modify it
    // is rejected, except inside a game's mods folder, which is a real SD folder.
    Result CreateFile(const char* path, s64 size, u32 option) override;
    Result DeleteFile(const char* path) override;
    Result RenameFile(const char *old_path, const char *new_path) override;
    Result CreateDirectory(const char* path) override;
    Result DeleteDirectoryRecursively(const char* path) override;
    Result RenameDirectory(const char *old_path, const char *new_path) override;
    Result SetFileSize(FsFile *file, s64 size) override;
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override;
    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override;
    Result GetFileSize(FsFile *file, s64 *out_size) override;
    Result ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) override;
    void CloseFile(FsFile *file) override;
    Result OpenDirectory(const char *path, u32 mode, FsDir *out_dir) override;
    Result ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) override;
    Result GetDirectoryEntryCount(FsDir *d, s64 *out_count) override;
    void CloseDirectory(FsDir *d) override;

    // ncm reads are not safe to issue concurrently, one reader thread only.
    bool MultiThreadTransfer(s64 size, bool read) override;

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
        // a real file in the game's mods folder on the SD card.
        fs::File sd{};
        bool is_sd{};
    };

    struct DirHandle {
        std::vector<FsDirectoryEntry> entries{};
        s64 index{};
    };

    // one folder per installed title. archived titles (a record with no
    // content) are skipped - there would be nothing to dump.
    void ScanGames();
    Result GetNsp(const std::string& game, std::shared_ptr<GameNsp>& out);
    Result BuildMergedCacheIfNeeded();
    Result FindMergedFile(const std::string& filename, std::shared_ptr<GameNsp>& nsp, size_t& index);
    Result BuildForwarderCacheIfNeeded();
    Result FindForwarderFile(const std::string& filename, std::shared_ptr<GameNsp>& nsp, size_t& index);
    Result FindFile(const std::string& game, const std::string& file, std::shared_ptr<GameNsp>& nsp, size_t& index);
    auto Parse(const char* path) const -> sphaira::mtp::ParsedPath;
    auto InfoText(std::string_view which) const -> std::string;
    auto InfoDirEntry(std::string_view which) const -> FsDirectoryEntry;
    // <game>/Mods/<sub> -> /atmosphere/contents/<tid>/<sub> on the SD card.
    Result ModsSdPath(const sphaira::mtp::ParsedPath& pp, fs::FsPath& out) const;
    // same, for an item inside a mods folder; anything else is not writable.
    Result ModsSdItem(const char* path, fs::FsPath& out) const;

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
    // backs the mods folders; open sd files keep a pointer to it.
    fs::FsNativeSd m_sd{};
};

} // namespace sphaira::haze
