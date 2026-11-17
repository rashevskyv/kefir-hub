#include "haze/haze_game_proxy_internal.hpp"
#include "ui/menus/game/game_internal.hpp"
#include "keys_dump.hpp"

namespace sphaira::haze {

void FsGameProxy::ScanGames() {
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

Result FsGameProxy::GetNsp(const std::string& game, std::shared_ptr<GameNsp>& out) {
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
    nsp->app_id = it->second.app_id;
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

Result FsGameProxy::BuildMergedCacheIfNeeded() {
    SCOPED_MUTEX(&m_cache_mutex);
    if (m_merged_built) {
        R_SUCCEED();
    }

    // ponytail: Merged listing needs exact sizes, so headers/tickets stay cached
    // for this MTP session; add lightweight sizing only if hardware measurements
    // show memory or listing latency is a problem.
    for (const auto& [dir_name, rec] : m_games) {
        AddMergedEntry(dir_name, rec);
    }

    m_merged_built = true;
    R_SUCCEED();
}

void FsGameProxy::AddMergedEntry(const std::string& dir_name, const GameRec& rec) {
    title::NspEntry entry;
    const auto rc = title::BuildMergedNspEntry(rec.app_id, rec.name.c_str(), entry);
    if (R_FAILED(rc)) {
        log_write("[MTP-GAMES] failed to build merged nsp for %s 0x%X\n", dir_name.c_str(), rc);
        return;
    }

    auto nsp = std::make_shared<GameNsp>();
    nsp->app_id = rec.app_id;
    FsDirectoryEntry fe{};
    std::snprintf(fe.name, sizeof(fe.name), "%s", entry.path.s);
    fe.type = FsDirEntryType_File;
    fe.file_size = entry.nsp_size;

    nsp->entries.emplace_back(std::move(entry));
    nsp->listing.emplace_back(fe);

    m_merged_cache.emplace(fe.name, std::move(nsp));
}

Result FsGameProxy::FindMergedFile(const std::string& filename, std::shared_ptr<GameNsp>& nsp, size_t& index) {
    R_TRY(BuildMergedCacheIfNeeded());

    SCOPED_MUTEX(&m_cache_mutex);
    const auto it = m_merged_cache.find(filename);
    R_UNLESS(it != m_merged_cache.end(), FsError_PathNotFound);

    nsp = it->second;
    index = 0;
    R_SUCCEED();
}

Result FsGameProxy::BuildForwarderCacheIfNeeded() {
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
        nsp->app_id = rec.app_id;
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

Result FsGameProxy::FindForwarderFile(const std::string& filename, std::shared_ptr<GameNsp>& nsp, size_t& index) {
    R_TRY(BuildForwarderCacheIfNeeded());

    SCOPED_MUTEX(&m_cache_mutex);
    const auto it = m_forwarder_cache.find(filename);
    R_UNLESS(it != m_forwarder_cache.end(), FsError_PathNotFound);

    nsp = it->second;
    index = 0;
    R_SUCCEED();
}

Result FsGameProxy::FindFile(const std::string& game, const std::string& file, std::shared_ptr<GameNsp>& nsp, size_t& index) {
    R_TRY(GetNsp(game, nsp));

    for (size_t i = 0; i < nsp->entries.size(); i++) {
        if (!strcasecmp(nsp->entries[i].path.s, file.c_str())) {
            index = i;
            R_SUCCEED();
        }
    }

    R_THROW(FsError_PathNotFound);
}

auto FsGameProxy::Parse(const char* path) const -> sphaira::mtp::ParsedPath {
    // libhaze names the storage root "/games"; strip that so listings and
    // reads see "/", "/Merged", "/Separate/...", "/Forwarders" like the
    // host tests (and the locale's names, which m_names holds).
    auto pp = sphaira::mtp::ParseGamesPath(FixPath(path).s, m_layout, m_names);
    // the install folders are off: their names are not special here
    if (!m_install_folders && (pp.kind == sphaira::mtp::PathKind::InstallDir || pp.kind == sphaira::mtp::PathKind::InstallFile)) {
        pp = {};
    }
    return pp;
}

auto FsGameProxy::InfoText(std::string_view which) const -> std::string {
    // the start-up check result does not change in a session, so the readme size stays right.
    const std::string keys = keys_dump::KeysBad()
        ? "Games may be missing here because the console keys are missing or wrong. Start Kefir Hub and choose \"Yes, dump the keys\": the console restarts and dumps them by itself. Then connect again."_i18n + "\n\n"
        : std::string{};
    if (which == "merged") {
        return keys + "Each file is one game. Base, update and DLC are packed into a single NSP. Use this when the installer expects one file per title."_i18n + "\n";
    }
    if (which == "separate") {
        return keys + "Each folder is one game. Inside are separate NSP files for the base, the update and each DLC."_i18n + "\n";
    }
    if (which == "forwarders") {
        return "HOME-menu forwarders (homebrew icons). These are not Nintendo games. Copy one to dump that icon as an NSP."_i18n + "\n";
    }
    return keys + "This drive dumps installed titles as NSP files you can copy to a PC."_i18n + "\n\n"
        + m_names.merged + " — " + "One NSP per game with base, update and DLC together."_i18n + "\n"
        + m_names.separate + " — " + "A folder per game with each component as its own NSP."_i18n + "\n"
        + m_names.forwarders + " — " + "HOME-menu homebrew icons, not Nintendo games."_i18n + "\n"
        + (m_install_folders
            ? m_names.install_sd + " — " + "Copy an NSP, NSZ, XCI or XCZ file here to install it to the microSD card."_i18n + "\n"
                + m_names.install_nand + " — " + "Copy an NSP, NSZ, XCI or XCZ file here to install it to the system memory."_i18n + "\n"
            : std::string{})
        + "Delete a game here to remove it from the console. Its saves stay."_i18n + "\n";
}

auto FsGameProxy::InfoDirEntry(std::string_view which) const -> FsDirectoryEntry {
    return MakeVirtualFileEntry(m_names.readme, static_cast<s64>(InfoText(which).size()));
}

Result FsGameProxy::ModsSdPath(const sphaira::mtp::ParsedPath& pp, fs::FsPath& out) const {
    const auto it = m_games.find(pp.game);
    R_UNLESS(it != m_games.end(), FsError_PathNotFound);
    out = title::GetContentsPath(it->second.app_id);
    if (!pp.filename.empty()) {
        out = fs::AppendPath(out, pp.filename.c_str());
    }
    R_SUCCEED();
}

Result FsGameProxy::ModsSdItem(const char* path, fs::FsPath& out) const {
    const auto pp = Parse(path);
    if (pp.kind != sphaira::mtp::PathKind::ModsPath || pp.filename.empty()) {
        log_write("[MTP-GAMES] rejecting write to %s\n", path);
        R_THROW(FsError_NotImplemented);
    }
    return ModsSdPath(pp, out);
}

Result FsGameProxy::DeleteGame(u64 app_id) {
    log_write("[MTP-GAMES] deleting game %016lX\n", app_id);
    const auto rc = ui::menu::game::DeleteApplicationKeepSave(app_id);
    log_write("[MTP-GAMES] delete %016lX: 0x%X\n", app_id, rc);
    R_TRY(rc);

    SCOPED_MUTEX(&m_cache_mutex);
    const auto is_game = [app_id](const auto& kv) { return kv.second.app_id == app_id; };
    const auto is_nsp = [app_id](const auto& kv) { return kv.second->app_id == app_id; };
    std::erase_if(m_games, is_game);
    std::erase_if(m_forwarders, is_game);
    std::erase_if(m_cache, is_nsp);
    std::erase_if(m_merged_cache, is_nsp);
    std::erase_if(m_forwarder_cache, is_nsp);
    R_SUCCEED();
}

Result FsGameProxy::DeleteComponent(const std::string& game, const std::shared_ptr<GameNsp>& nsp, size_t index) {
    const auto status = nsp->entries[index].status;
    // game card content has no storage of ours to delete from (same rule as Game Details).
    R_UNLESS(status.storageID == NcmStorageId_SdCard || status.storageID == NcmStorageId_BuiltInUser, FsError_NotImplemented);

    const auto rc = title::DeleteComponent(status);
    log_write("[MTP-GAMES] delete component %016lX type %u: 0x%X\n", status.application_id, status.meta_type, rc);
    R_TRY(rc);

    const auto app_id = nsp->app_id;
    title::MetaEntries left;
    const bool gone = R_FAILED(title::GetMetaEntries(app_id, left)) || left.empty();
    const auto is_game = [app_id](const auto& kv) { return kv.second.app_id == app_id; };
    const auto is_nsp = [app_id](const auto& kv) { return kv.second->app_id == app_id; };

    SCOPED_MUTEX(&m_cache_mutex);
    // the Merged nsp still holds the deleted ncas.
    std::erase_if(m_merged_cache, is_nsp);
    if (gone) {
        std::erase_if(m_games, is_game);
        std::erase_if(m_cache, is_nsp);
        R_SUCCEED();
    }

    // drop the one file and keep the other names: Explorer deletes the next file of the
    // same selection by the name it already holds. a copy, so open handles keep their index.
    if (const auto it = m_cache.find(game); it != m_cache.end() && it->second == nsp) {
        auto copy = std::make_shared<GameNsp>(*nsp);
        copy->entries.erase(copy->entries.begin() + index);
        copy->listing.erase(copy->listing.begin() + index);
        it->second = std::move(copy);
    }
    if (const auto it = m_games.find(game); m_merged_built && it != m_games.end()) {
        AddMergedEntry(it->first, it->second);
    }
    R_SUCCEED();
}

} // namespace sphaira::haze
