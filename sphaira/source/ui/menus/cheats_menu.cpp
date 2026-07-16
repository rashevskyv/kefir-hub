#include "ui/menus/cheats_menu.hpp"
#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheat_game_select_menu.hpp"
#include "ui/menus/cheats/cheats_dmnt.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_db.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/scrollable_text.hpp"
#include "ui/sidebar.hpp"
#include "ui/menus/file_picker.hpp"

#include "app.hpp"
#include "log.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "threaded_file_transfer.hpp"
#include "image.hpp"
#include "i18n.hpp"
#include "nacp_util.hpp"
#include "yyjson_helper.hpp"
#include "swkbd.hpp"
#include "title_info.hpp"
#include "utils/devoptab.hpp"
#include "utils/utils.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"

#include <yyjson.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <format>
#include <atomic>
#include <optional>
#include <ranges>
#include <sstream>
#include <switch.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>

namespace sphaira::ui::menu::hats {

using namespace detail;

namespace detail {





// Get version for a title (like aio-switch-updater does)
auto GetTitleVersion(u64 title_id) -> u32 {
    u32 version = 0;
    s32 out = 0;

    // Use title namespace functions to get meta entries
    s32 count = 0;
    Result rc = nsCountApplicationContentMeta(title_id, &count);
    if (R_FAILED(rc) || count == 0) {
        return 0;
    }

    std::vector<NsApplicationContentMetaStatus> meta_statuses(count);
    rc = nsListApplicationContentMetaStatus(title_id, 0, meta_statuses.data(), meta_statuses.size(), &out);
    if (R_FAILED(rc)) {
        return 0;
    }

    meta_statuses.resize(out);

    // Find the highest version
    for (const auto& meta : meta_statuses) {
        if (meta.version > version) {
            version = meta.version;
        }
    }

    return version;
}

// Get title name using nsGetApplicationControlData
auto GetTitleName(u64 title_id) -> std::string {
    const auto base_title_id = GetBaseApplicationTitleId(title_id);

    // Get language entry for the name
    const auto copy_valid_name = [](u64 source_title_id, const NacpLanguageEntry& entry) -> std::string {
        constexpr size_t name_size = sizeof(entry.name);
        const auto name_len = strnlen(entry.name, name_size);
        if (name_len == 0 || name_len == name_size) {
            return "";
        }

        bool has_visible_char = false;
        for (size_t i = 0; i < name_len; i++) {
            const auto c = static_cast<unsigned char>(entry.name[i]);
            if (c < 0x20 && c != '\t') {
                log_write("[Cheats] GetTitleName: invalid control character for %016lx at offset %zu\n", source_title_id, i);
                return "";
            }
            if (!std::isspace(c)) {
                has_visible_char = true;
            }
        }

        if (!has_visible_char) {
            return "";
        }

        return std::string(entry.name, name_len);
    };

    const auto extract_nacp_name = [&](u64 source_title_id, NacpStruct& nacp) -> std::string {
        NacpLanguageEntry* lang_entry = nullptr;
        const auto rc = nacpGetLanguageEntry(&nacp, &lang_entry);
        if (R_SUCCEEDED(rc) && lang_entry) {
            if (auto name = copy_valid_name(source_title_id, *lang_entry); !name.empty()) {
                return name;
            }
            log_write("[Cheats] GetTitleName: selected language entry invalid for %016lx, trying fallbacks\n", source_title_id);
        }

        for (size_t i = 0; i < 16; i++) {
            const auto& entry = nacp_util::GetLanguageEntry(nacp, i);
            if (&entry == lang_entry) {
                continue;
            }
            if (auto name = copy_valid_name(source_title_id, entry); !name.empty()) {
                return name;
            }
        }

        return "";
    };

    const auto try_get_base_application_name = [&]() -> std::string {
        title::MetaEntries entries;
        if (R_FAILED(title::GetMetaEntries(base_title_id, entries, title::ContentFlag_Application)) || entries.empty()) {
            return "";
        }

        u64 program_id = 0;
        fs::FsPath path;
        if (R_FAILED(title::GetControlPathFromStatus(entries.front(), &program_id, &path))) {
            return "";
        }

        NacpStruct nacp{};
        std::vector<u8> icon;
        if (R_FAILED(nca::ParseControl(path, program_id, &nacp, sizeof(nacp), &icon))) {
            return "";
        }

        if (auto name = extract_nacp_name(base_title_id, nacp); !name.empty()) {
            log_write("[Cheats] GetTitleName: loaded base application control name for %016lx\n", base_title_id);
            return name;
        }

        log_write("[Cheats] GetTitleName: base application control name invalid for %016lx\n", base_title_id);
        return "";
    };

    if (R_SUCCEEDED(title::Init())) {
        ON_SCOPE_EXIT(title::Exit());

        if (auto name = try_get_base_application_name(); !name.empty()) {
            return name;
        }

        if (auto data = title::Get(base_title_id); data && data->status == title::NacpLoadStatus::Loaded) {
            if (auto name = copy_valid_name(base_title_id, data->lang); !name.empty()) {
                return name;
            }
            log_write("[Cheats] GetTitleName: title cache/manual name invalid for %016lx\n", base_title_id);
        }
    } else {
        log_write("[Cheats] GetTitleName: title::Init failed for %016lx\n", base_title_id);
    }

    const auto try_get_name = [&](NsApplicationControlSource source, u64 source_title_id) -> std::string {
        NsApplicationControlData control_data{};
        u64 actual_size = 0;
        const auto rc = nsGetApplicationControlData(
            source,
            source_title_id,
            &control_data,
            sizeof(control_data),
            &actual_size
        );

        if (R_FAILED(rc)) {
            return "";
        }

        if (actual_size != 0 && actual_size < sizeof(control_data.nacp)) {
            log_write("[Cheats] GetTitleName: control data too small for %016lx: %llu bytes\n",
                      source_title_id, static_cast<unsigned long long>(actual_size));
            return "";
        }

        return extract_nacp_name(source_title_id, control_data.nacp);
    };

    if (auto name = try_get_name(NsApplicationControlSource_CacheOnly, base_title_id); !name.empty()) {
        return name;
    }

    // Storage can include update-provided control data, so only use it after base cache misses.
    if (auto name = try_get_name(NsApplicationControlSource_Storage, base_title_id); !name.empty()) {
        return name;
    }

    if (title_id != base_title_id) {
        log_write("[Cheats] GetTitleName: base lookup failed for %016lx, trying original title %016lx\n",
                  base_title_id, title_id);
        if (auto name = try_get_name(NsApplicationControlSource_CacheOnly, title_id); !name.empty()) {
            return name;
        }
        if (auto name = try_get_name(NsApplicationControlSource_Storage, title_id); !name.empty()) {
            return name;
        }
    }

    return "";
}

void AppendGameCardGames(std::vector<GameCheatInfo>& games, std::unordered_set<u64>& seen_title_ids) {
    NcmContentMetaDatabase db{};
    Result rc = ncmOpenContentMetaDatabase(&db, NcmStorageId_GameCard);
    if (R_FAILED(rc)) {
        log_write("[Cheats] AppendGameCardGames: failed to open game card metadata DB: %x\n", rc);
        return;
    }
    ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));

    std::vector<NcmContentMetaKey> keys(16);
    s32 total = 0;
    s32 written = 0;
    rc = ncmContentMetaDatabaseList(&db, &total, &written, keys.data(), keys.size(),
        NcmContentMetaType_Unknown, 0, 0, UINT64_MAX, NcmContentInstallType_Full);
    if (R_FAILED(rc)) {
        log_write("[Cheats] AppendGameCardGames: failed to list game card metadata: %x\n", rc);
        return;
    }

    if (total > written && total > static_cast<s32>(keys.size())) {
        keys.resize(total);
        rc = ncmContentMetaDatabaseList(&db, &total, &written, keys.data(), keys.size(),
            NcmContentMetaType_Unknown, 0, 0, UINT64_MAX, NcmContentInstallType_Full);
        if (R_FAILED(rc)) {
            log_write("[Cheats] AppendGameCardGames: failed to list all game card metadata: %x\n", rc);
            return;
        }
    }

    keys.resize(written);
    log_write("[Cheats] AppendGameCardGames: found %d game card metadata entries (%d written)\n", total, written);

    for (const auto& key : keys) {
        if (key.type != NcmContentMetaType_Application && key.type != NcmContentMetaType_Patch) {
            continue;
        }

        const auto base_title_id = GetBaseApplicationTitleId(ncm::GetAppId(key));
        if (base_title_id == 0 || !seen_title_ids.insert(base_title_id).second) {
            continue;
        }

        GameCheatInfo info;
        info.title_id = base_title_id;
        info.version = GetTitleVersion(base_title_id);
        info.name = GetTitleName(base_title_id);
        if (info.name.empty()) {
            info.name = std::format("Game {:016X}", base_title_id);
        }
        std::snprintf(info.lang.name, sizeof(info.lang.name), "%s", info.name.c_str());

        log_write("[Cheats] AppendGameCardGames: added inserted game card %016lX (%s) v%u\n",
                  info.title_id, info.name.c_str(), info.version);
        games.push_back(std::move(info));
    }
}

auto EnumerateInstalledGames() -> std::vector<GameCheatInfo> {
    std::vector<GameCheatInfo> games;

    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] EnumerateInstalledGames: nsInitialize failed: %x\n", rc);
        return games;
    }
    ON_SCOPE_EXIT(nsExit());

    std::vector<NsApplicationRecord> record_list(ENTRY_CHUNK_COUNT);
    std::unordered_set<u64> seen_title_ids;
    s32 offset = 0;

    while (true) {
        s32 record_count = 0;
        rc = nsListApplicationRecord(record_list.data(), record_list.size(), offset, &record_count);
        if (R_FAILED(rc)) {
            log_write("[Cheats] EnumerateInstalledGames: nsListApplicationRecord failed at offset %d: %x\n", offset, rc);
            break;
        }

        if (record_count == 0) {
            break;
        }

        for (s32 i = 0; i < record_count; i++) {
            const auto& record = record_list[i];
            if (record.application_id == 0) {
                continue;
            }
            const auto base_title_id = GetBaseApplicationTitleId(record.application_id);
            if (!seen_title_ids.insert(base_title_id).second) {
                continue;
            }

            GameCheatInfo info;
            info.title_id = base_title_id;
            info.version = GetTitleVersion(base_title_id);
            info.name = GetTitleName(base_title_id);
            if (info.name.empty()) {
                info.name = std::format("Game {:016X}", base_title_id);
            }
            std::snprintf(info.lang.name, sizeof(info.lang.name), "%s", info.name.c_str());
            games.push_back(std::move(info));
        }

        offset += record_count;
    }

    AppendGameCardGames(games, seen_title_ids);

    return games;
}

// Get saved CheatSlips token (checks HATS-Tools and AIO-Switch-Updater paths)
auto GetCheatslipsToken() -> std::string {
    fs::FsNativeSd fs;

    // List of token paths to check (HATS-Tools first, then AIO for compatibility)
    const char* token_paths[] = {TOKEN_PATH, AIO_TOKEN_PATH};

    for (const char* token_path : token_paths) {
        if (!fs.FileExists(token_path)) {
            log_write("[Cheats] Token file not found at %s\n", token_path);
            continue;
        }

        log_write("[Cheats] Found token file at %s\n", token_path);

        std::vector<u8> data;
        Result rc = fs.read_entire_file(token_path, data);
        if (R_FAILED(rc)) {
            log_write("[Cheats] Failed to read token file, result: %x\n", rc);
            continue;
        }

        log_write("[Cheats] Read %zu bytes from token file\n", data.size());

        // Null-terminate for JSON parsing
        data.push_back(0);

        const auto data_len = std::strlen(reinterpret_cast<char*>(data.data()));
        yyjson_doc* doc = yyjson_read((char*)data.data(), data_len, 0);
        if (!doc) {
            log_write("[Cheats] Failed to parse token JSON, raw data: %s\n", (char*)data.data());
            continue;
        }

        ON_SCOPE_EXIT(yyjson_doc_free(doc));

        yyjson_val* root = yyjson_doc_get_root(doc);
        if (!yyjson_is_obj(root)) {
            log_write("[Cheats] Token JSON is not an object\n");
            continue;
        }

        yyjson_val* token_val = yyjson_obj_get(root, "token");
        if (token_val && yyjson_is_str(token_val)) {
            const char* token = yyjson_get_str(token_val);
            log_write("[Cheats] Loaded saved token from %s: %s\n", token_path, token);
            // Copy the token string since the doc will be freed
            return std::string(token);
        }

        log_write("[Cheats] No token field in JSON from %s\n", token_path);
    }

    log_write("[Cheats] No valid token found in any location\n");
    return "";
}

// Authenticate with CheatSlips API and get token
auto AuthenticateCheatslips(const std::string& email, const std::string& password) -> std::string {
    // Create JSON body with credentials
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_strncpy(doc, root, "email", email.c_str(), email.size());
    yyjson_mut_obj_add_strncpy(doc, root, "password", password.c_str(), password.size());

    char* json_body = yyjson_mut_write(doc, 0, 0);
    if (!json_body) {
        log_write("[Cheats] Failed to create login JSON\n");
        return "";
    }

    ON_SCOPE_EXIT(free(json_body));

    // Send POST request to CheatSlips token endpoint
    // Use ToMemory with Fields for POST (FromMemory adds trailing slash and uses CURLOPT_UPLOAD)
    auto result = curl::Api().ToMemory(
        curl::Url{CHEATSLIPS_TOKEN_URL},
        curl::Header{
            {"Accept", "application/json"},
            {"Content-Type", "application/json"}
        },
        curl::Fields{json_body}
    );

    log_write("[Cheats] Auth HTTP code: %ld\n", result.code);
    if (!result.success || result.data.empty()) {
        log_write("[Cheats] Failed to authenticate with CheatSlips\n");
        return "";
    }

    // Parse response to get token
    result.data.push_back(0); // Null-terminate
    const auto response_len = std::strlen(reinterpret_cast<char*>(result.data.data()));
    yyjson_doc* resp_doc = yyjson_read(reinterpret_cast<char*>(result.data.data()), response_len, 0);
    if (!resp_doc) {
        log_write("[Cheats] Failed to parse auth response\n");
        return "";
    }
    ON_SCOPE_EXIT(yyjson_doc_free(resp_doc));

    yyjson_val* resp_root = yyjson_doc_get_root(resp_doc);
    if (!yyjson_is_obj(resp_root)) {
        return "";
    }

    yyjson_val* token_val = yyjson_obj_get(resp_root, "token");
    if (token_val && yyjson_is_str(token_val)) {
        const char* token = yyjson_get_str(token_val);
        log_write("[Cheats] Authentication successful, token: %s\n", token);
        // Copy the token string since the doc will be freed
        return std::string(token);
    }

    // Check for error message
    yyjson_val* error_val = yyjson_obj_get(resp_root, "error");
    if (error_val && yyjson_is_str(error_val)) {
        log_write("[Cheats] Auth error: %s\n", yyjson_get_str(error_val));
    }

    // Log full response for debugging
    log_write("[Cheats] Auth response: %s\n", reinterpret_cast<char*>(result.data.data()));

    return "";
}

// Save CheatSlips token
auto SaveCheatslipsToken(const std::string& token) -> void {
    fs::FsNativeSd fs;

    // Create directory if needed
    fs.CreateDirectoryRecursively("/config/hats-tools");

    // Create JSON document
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_strncpy(doc, root, "token", token.c_str(), token.size());

    // Write to file
    char* json = yyjson_mut_write(doc, 0, 0);
    if (!json) {
        log_write("[Cheats] Failed to write token JSON\n");
        return;
    }

    ON_SCOPE_EXIT(free(json));

    const auto json_data = std::vector<u8>(
        reinterpret_cast<const u8*>(json),
        reinterpret_cast<const u8*>(json) + std::strlen(json)
    );
    if (R_FAILED(fs.write_entire_file(TOKEN_PATH, json_data))) {
        log_write("[Cheats] Failed to write token file\n");
        return;
    }

    // Commit to ensure data is written to disk
    if (R_FAILED(fs.Commit())) {
        log_write("[Cheats] Failed to commit token file\n");
        return;
    }

    log_write("[Cheats] Saved CheatSlips token to file, JSON: %s\n", json);
}



auto DeleteAllCheatsForTitle(u64 title_id) -> bool {
    fs::FsNativeSd fs;

    const auto cheats_dir = GetCheatsDirPath(title_id);

    if (fs.DirExists(cheats_dir.c_str())) {
        Result rc = fs.DeleteDirectoryRecursively(cheats_dir.c_str());
        if (R_FAILED(rc)) {
            log_write("[Cheats] Failed to delete cheats directory %s: %x\n", cheats_dir.c_str(), rc);
            return false;
        }
        log_write("[Cheats] Deleted all cheats for title %016lx\n", title_id);

        // Also try to delete the title directory if empty
        const auto title_dir = std::string(ATMOSPHERE_CONTENTS_PATH) + "/" + FormatTitleIdLower(title_id);
        if (fs.DirExists(title_dir.c_str())) {
            fs.DeleteDirectory(title_dir.c_str());
        }
        return true;
    }

    return false;
}

// Clear cached cheats database from /config/hats-tools/cheats-db
auto ClearCheatsCache() -> Result {
    fs::FsNativeSd fs;

    log_write("[Cheats] Clearing cheats cache: %s\n", NX_DB_PATH);

    if (fs.DirExists(NX_DB_PATH)) {
        Result rc = fs.DeleteDirectoryRecursively(NX_DB_PATH);
        if (R_FAILED(rc)) {
            log_write("[Cheats] Failed to clear cheats cache: %x\n", rc);
            return rc;
        }
        log_write("[Cheats] Successfully cleared cheats cache\n");
        return 0;
    }

    log_write("[Cheats] Cheats cache directory does not exist\n");
    return 0;
}

// Delete all cheats for all games
auto DeleteAllCheats() -> Result {
    fs::FsNativeSd fs;

    // Get all installed games first
    std::vector<u64> installed_titles;

    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] nsInitialize failed: %x\n", rc);
        return rc;
    }

    std::vector<NsApplicationRecord> record_list(ENTRY_CHUNK_COUNT);
    s32 offset = 0;

    while (true) {
        s32 record_count = 0;
        rc = nsListApplicationRecord(record_list.data(), record_list.size(), offset, &record_count);

        if (R_FAILED(rc)) {
            break;
        }

        if (record_count == 0) {
            break;
        }

        for (s32 i = 0; i < record_count; i++) {
            if (record_list[i].application_id != 0) {
                installed_titles.push_back(record_list[i].application_id);
            }
        }

        offset += record_count;
    }

    nsExit();

    // Delete cheats for each installed game
    s32 deleted_count = 0;
    for (u64 title_id : installed_titles) {
        if (DeleteAllCheatsForTitle(title_id)) {
            deleted_count++;
        }
    }

    log_write("[Cheats] Deleted cheats for %d games\n", deleted_count);
    return 0;
}

// Delete orphaned cheats (cheats for games that are no longer installed)
auto DeleteOrphanedCheats() -> Result {
    fs::FsNativeSd fs;

    // Get all installed games
    std::vector<u64> installed_titles;

    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] nsInitialize failed: %x\n", rc);
        return -1;
    }

    std::vector<NsApplicationRecord> record_list(ENTRY_CHUNK_COUNT);
    s32 offset = 0;

    while (true) {
        s32 record_count = 0;
        rc = nsListApplicationRecord(record_list.data(), record_list.size(), offset, &record_count);

        if (R_FAILED(rc)) {
            break;
        }

        if (record_count == 0) {
            break;
        }

        for (s32 i = 0; i < record_count; i++) {
            if (record_list[i].application_id != 0) {
                installed_titles.push_back(record_list[i].application_id);
            }
        }

        offset += record_count;
    }

    nsExit();

    log_write("[Cheats] Found %zu installed games\n", installed_titles.size());

    // Scan atmosphere/contents for cheat directories
    s32 deleted_count = 0;

    // Check if atmosphere directory exists
    if (!fs.DirExists(ATMOSPHERE_CONTENTS_PATH)) {
        log_write("[Cheats] Atmosphere contents directory not found\n");
        return 0;
    }

    // Open directory and iterate through subdirectories
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(ATMOSPHERE_CONTENTS_PATH, FsDirOpenMode_ReadDirs, &dir))) {
        log_write("[Cheats] Failed to open atmosphere contents directory\n");
        return -1;
    }

    ON_SCOPE_EXIT(dir.Close());

    s64 count = 0;
    if (R_FAILED(dir.GetEntryCount(&count))) {
        return -1;
    }

    std::vector<FsDirectoryEntry> entries(count);
    s64 read_count = 0;
    if (R_FAILED(dir.Read(&read_count, entries.size(), entries.data()))) {
        return -1;
    }

    for (s64 i = 0; i < read_count; i++) {
        const auto& entry = entries[i];
        if (entry.type != FsDirEntryType_Dir) continue;

        // Parse title ID from directory name
        std::string dir_name = entry.name;
        u64 title_id = 0;
        if (sscanf(dir_name.c_str(), "%016lx", &title_id) != 1) {
            continue;
        }

        // Check if this title is still installed
        bool is_installed = false;
        for (u64 installed : installed_titles) {
            if (installed == title_id) {
                is_installed = true;
                break;
            }
        }

        // If not installed, delete the cheats directory
        if (!is_installed) {
            const auto title_dir = std::string(ATMOSPHERE_CONTENTS_PATH) + "/" + dir_name;
            const auto cheats_dir = title_dir + "/" + CHEATS_SUBDIR;

            if (fs.DirExists(cheats_dir.c_str())) {
                log_write("[Cheats] Deleting orphaned cheats for %016lx\n", title_id);
                if (R_SUCCEEDED(fs.DeleteDirectoryRecursively(cheats_dir.c_str()))) {
                    deleted_count++;
                }

                // Try to delete empty title directory
                fs.DeleteDirectory(title_dir.c_str());
            }
        }
    }

    log_write("[Cheats] Deleted orphaned cheats for %d games\n", deleted_count);
    return deleted_count;
}

} // namespace detail

void RefreshCheatMetadataCache() {
    log_write("[Cheats] Starting cheat metadata scan\n");

    const bool can_scan_nso = App::IsApplication();
    if (!can_scan_nso) {
        log_write("[Cheats] Bulk NSO build ID scan disabled in applet mode; caching title metadata only\n");
        static std::atomic_bool applet_notice_shown{};
        if (!applet_notice_shown.exchange(true)) {
            App::Notify("Applet Mode: bulk NSO scan skipped; installed-content cheat lookup remains available"_i18n);
        }
    }

    const auto games = EnumerateInstalledGames();
    if (games.empty()) {
        log_write("[Cheats] Cheat metadata scan found no installed games\n");
    }

    std::unordered_map<u64, CachedCheatMetadata> scanned_entries;
    scanned_entries.reserve(games.size());

    for (const auto& game : games) {
        CachedCheatMetadata entry;
        entry.title_id = game.title_id;
        entry.name = game.name;
        entry.version = game.version;
        entry.scanned_at = static_cast<u64>(std::time(nullptr));
        entry.source = "scan";

        const auto installed_nca_build_id = GetBuildIdFromInstalledNca(game.title_id);
        if (!installed_nca_build_id.empty()) {
            entry.build_id = NormalizeBuildId(installed_nca_build_id);
            entry.source = "installed-nca-scan";
        } else if (can_scan_nso) {
            const auto build_id = GetBuildIdFromNso(game.title_id);
            if (!build_id.empty()) {
                entry.build_id = NormalizeBuildId(build_id);
                entry.source = "nso-scan";
            }
        }

        scanned_entries[game.title_id] = std::move(entry);
    }

    mutexLock(&g_cheat_metadata_cache_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&g_cheat_metadata_cache_mutex));

    auto cache_entries = LoadCheatMetadataCacheUnlocked();

    std::unordered_set<u64> installed_ids;
    installed_ids.reserve(scanned_entries.size());
    for (const auto& [title_id, _] : scanned_entries) {
        installed_ids.insert(title_id);
    }

    for (auto it = cache_entries.begin(); it != cache_entries.end();) {
        if (!installed_ids.contains(it->first)) {
            it = cache_entries.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& [title_id, scanned] : scanned_entries) {
        auto& entry = cache_entries[title_id];
        entry.title_id = scanned.title_id;
        entry.name = scanned.name;
        entry.version = scanned.version;
        entry.scanned_at = scanned.scanned_at;

        if (IsValidBuildId(scanned.build_id)) {
            entry.build_id = scanned.build_id;
            entry.source = scanned.source;
        } else if (entry.source.empty()) {
            entry.source = scanned.source;
        }
    }

    if (!SaveCheatMetadataCacheUnlocked(cache_entries)) {
        log_write("[Cheats] Failed to save cheat metadata cache\n");
        return;
    }

    size_t resolved_count = 0;
    for (const auto& [_, entry] : cache_entries) {
        if (IsValidBuildId(entry.build_id)) {
            resolved_count++;
        }
    }

    log_write("[Cheats] Cheat metadata scan complete: %zu/%zu titles resolved\n",
              resolved_count, cache_entries.size());
}

auto DownloadAndExtractKefirCheats(ProgressBox* pbox, const char* url) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    R_TRY(fs.CreateDirectoryRecursively(KEFIR_CHEATS_CACHE_DIR));

    if (fs.FileExists(KEFIR_CHEATS_ZIP)) {
        fs.DeleteFile(KEFIR_CHEATS_ZIP);
    }

    pbox->NewTransfer("Downloading cheats pack..."_i18n);
    const auto result = curl::Api().ToFile(
        curl::Url{url},
        curl::Path{KEFIR_CHEATS_ZIP},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, Result_CurlFailedEasyInit);

    pbox->NewTransfer("Installing cheats..."_i18n);
    R_TRY(thread::TransferUnzipAll(pbox, KEFIR_CHEATS_ZIP, &fs, "/atmosphere"));

    if (fs.FileExists(KEFIR_CHEATS_ZIP)) {
        fs.DeleteFile(KEFIR_CHEATS_ZIP);
    }

    R_TRY(fs.Commit());
    R_SUCCEED();
}

void PromptKefirCheatsDownload(const char* title, const char* url) {
    App::Push<OptionBox>(
        "Download and install this cheats pack?\nExisting matching cheat files may be overwritten."_i18n,
        "Cancel"_i18n, "Download"_i18n, 1,
        [title, url](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            App::Push<ProgressBox>(0, "Downloading..."_i18n, i18n::get(title),
                [url](auto pbox) -> Result {
                    return DownloadAndExtractKefirCheats(pbox, url);
                },
                [](Result rc) {
                    if (R_SUCCEEDED(rc)) {
                        RefreshCheatMetadataCache();
                        App::Notify("Cheats pack installed"_i18n);
                    } else {
                        App::Push<ErrorBox>(rc, "Failed to install cheats pack"_i18n);
                    }
                }
            );
        }
    );
}



// ============================================================
// CheatsMenu - Main menu with cheat management options
// ============================================================

CheatsMenu::CheatsMenu() : MenuBase{"Cheats"_i18n, MenuFlag_None} {
    // Main cheat management options
    m_items = {
        {"Download Kefir Cheats"_i18n, "Full KefirUpdater cheats pack"_i18n},
        {"Download 60FPS/GFX Cheats"_i18n, "KefirUpdater performance/graphics pack"_i18n},
        {"Download Exact Cheats"_i18n, "Select game and match Build ID"_i18n},
        {"Import From File"_i18n, "Import a local cheat .txt file"_i18n},
        {"View Cheats"_i18n, "View installed cheat codes"_i18n},
        {"Delete All Cheats"_i18n, "Delete all existing cheat codes"_i18n},
        {"Delete Orphaned"_i18n, "Delete cheats for uninstalled games"_i18n},
        {"Clear Cheats Cache"_i18n, "Delete cached cheats database"_i18n}
    };

    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    const Vec4 v{75, GetY() + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CheatsMenu::~CheatsMenu() {
}

void CheatsMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void CheatsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& item = m_items[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_items.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f - 6.f, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", item.first.c_str());

        gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f + 14.f, 14.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", item.second.c_str());
    });
}

void CheatsMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

void CheatsMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CheatsMenu::OnSelect() {
    switch (m_index) {
        case 0: // Download full KefirUpdater cheats pack
            PromptKefirCheatsDownload("Kefir Cheats", KEFIR_CHEATS_URL);
            break;
        case 1: // Download KefirUpdater 60FPS/GFX pack
            PromptKefirCheatsDownload("60FPS/GFX Cheats", KEFIR_CHEATS_GFX_URL);
            break;
        case 2: // Download exact cheats from nx-cheats-db
            App::Push<CheatGameSelectMenu>(CheatSource::NxDb);
            break;
        case 3: // Import local cheat file
            App::Push<filepicker::Menu>(
                [](const fs::FsPath& path) -> bool {
                    App::Push<CheatGameSelectMenu>(CheatSource::ManualFile, path);
                    return true;
                },
                std::vector<std::string>{"txt"}
            );
            break;
        case 4: // View Cheats
            App::Push<CheatViewMenu>();
            break;
        case 5: // Delete All Cheats
            App::Push<OptionBox>(
                "Delete all existing cheat codes?\nThis will remove ALL cheat files\nfor ALL installed games."_i18n,
                "Cancel"_i18n, "Delete"_i18n, 1,
                [](auto op_index) {
                    if (!op_index || *op_index != 1) {
                        return;
                    }
                    App::Push<ProgressBox>(0, "Deleting..."_i18n, "Cheats"_i18n,
                        [](auto pbox) -> Result {
                            return DeleteAllCheats();
                        },
                        [](Result rc) {
                            if (R_SUCCEEDED(rc)) {
                                App::Notify("Deleted all cheat codes"_i18n);
                            } else {
                                App::Push<ErrorBox>(rc, "Failed to delete cheats"_i18n);
                            }
                        }
                    );
                }
            );
            break;
        case 6: // Delete Orphaned Cheats
            App::Push<ProgressBox>(0, "Scanning..."_i18n, "Cheats"_i18n,
                [](auto pbox) -> Result {
                    return DeleteOrphanedCheats();
                },
                [](Result rc) {
                    if (rc == 0) {
                        App::Notify("No orphaned cheats found"_i18n);
                    } else if (rc > 0) {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "Deleted %d orphaned cheats"_i18n.c_str(), (int)rc);
                        App::Notify(buf);
                    } else {
                        App::Push<ErrorBox>(rc, "Failed to delete orphaned cheats"_i18n);
                    }
                }
            );
            break;
        case 7: // Clear Cheats Cache
            App::Push<OptionBox>(
                "Clear cached cheats database?"_i18n,
                "Cancel"_i18n, "Clear"_i18n, 0,
                [](auto op_index) {
                    if (!op_index || *op_index != 1) {
                        return;
                    }
                    App::Push<ProgressBox>(0, "Clearing Cache"_i18n, "Cheats"_i18n,
                        [](auto pbox) -> Result {
                            return ClearCheatsCache();
                        },
                        [](Result rc) {
                            if (R_SUCCEEDED(rc)) {
                                App::Notify("Cheats cache cleared successfully"_i18n);
                            } else {
                                App::Push<ErrorBox>(rc, "Failed to clear cheats cache"_i18n);
                            }
                        }
                    );
                }
            );
            break;
    }
}

// ============================================================
// CheatViewMenu - View installed cheats
// ============================================================

CheatViewMenu::CheatViewMenu() : MenuBase{"Installed Cheats", MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::A, Action{"View"_i18n, [this](){
            if (!m_games.empty()) {
                OnSelect();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::Y, Action{"Delete"_i18n, [this](){
            if (!m_games.empty()) {
                OnDelete();
            }
        }})
    );

    const Vec4 v{75, GetY() + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CheatViewMenu::~CheatViewMenu() {
}

void CheatViewMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_games.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_games.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void CheatViewMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_scanning) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "Scanning for cheats..."_i18n.c_str());
        return;
    }

    if (m_games.empty() && m_loaded) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "No installed cheats found"_i18n.c_str());
        return;
    }

    if (!m_games.empty()) {
        // Save and restore scissor to clip list drawing area
        nvgSave(vg);
        // Clip area starts below the header text; inflated by the selection
        // outline pad so the highlight of edge rows isn't clipped.
        const float p = gfx::SELECTION_OUTLINE_PAD;
        nvgScissor(vg, 75.f - p, GetY() + 40.f - p, 1220.f - 150.f + p * 2, 720.f - GetY() - 40.f + p * 2);
        ON_SCOPE_EXIT(nvgRestore(vg));

        constexpr float text_xoffset{15.f};

        m_list->Draw(vg, theme, m_games.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto& [x, y, w, h] = v;
            const auto& game = m_games[i];

            auto text_id = ThemeEntryID_TEXT;
            if (m_index == i) {
                text_id = ThemeEntryID_TEXT_SELECTED;
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else {
                if (i != m_games.size() - 1) {
                    gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
                }
            }

            // Game name
            gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f - 6.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(text_id),
                "%s", game.name.c_str());

            // Title ID and cheat count
            gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f + 14.f, 14.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%016lX - %zu cheat(s)", game.title_id, game.cheat_count);
        });
    }
}

void CheatViewMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_loaded && !m_scanning) {
        ScanGamesWithCheats();
    }
}

void CheatViewMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CheatViewMenu::OnSelect() {
    if (m_games.empty() || m_index >= (s64)m_games.size()) {
        return;
    }

    const auto& game = m_games[m_index];
    App::Push<CheatFilesMenu>(game);
}

void CheatViewMenu::OnDelete() {
    if (m_games.empty() || m_index >= (s64)m_games.size()) {
        return;
    }

    const auto& game = m_games[m_index];
    App::Push<OptionBox>(
        "Delete all cheats for "_i18n + game.name + "?",
        "Cancel"_i18n, "Delete"_i18n, 1,
        [this, game](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            if (DeleteAllCheatsForTitle(game.title_id)) {
                App::Notify("Deleted cheats for "_i18n + game.name);
                m_loaded = false; // Rescan
                ScanGamesWithCheats();
            } else {
                App::Notify("Failed to delete cheats"_i18n);
            }
        }
    );
}

void CheatViewMenu::ScanGamesWithCheats() {
    m_scanning = true;
    m_games.clear();

    // Initialize ns service
    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] nsInitialize failed: %x\n", rc);
        m_scanning = false;
        m_loaded = true;
        return;
    }

    std::vector<NsApplicationRecord> record_list(ENTRY_CHUNK_COUNT);
    std::unordered_set<u64> seen_title_ids;
    s32 offset = 0;

    while (true) {
        s32 record_count = 0;
        rc = nsListApplicationRecord(record_list.data(), record_list.size(), offset, &record_count);

        if (R_FAILED(rc)) {
            log_write("[Cheats] nsListApplicationRecord failed at offset %d: %x\n", offset, rc);
            break;
        }

        if (record_count == 0) {
            break;
        }

        // Process each record
        for (s32 i = 0; i < record_count; i++) {
            const auto& record = record_list[i];
            if (record.application_id == 0) continue;
            const auto base_title_id = GetBaseApplicationTitleId(record.application_id);
            if (!seen_title_ids.insert(base_title_id).second) {
                continue;
            }

            // Check if this game has any cheats
            auto existing = GetExistingCheats(base_title_id);
            if (!existing.empty()) {
                // Get game name
                std::string name = GetTitleName(base_title_id);
                if (name.empty()) {
                    char placeholder[64];
                    std::snprintf(placeholder, sizeof(placeholder), "Game %016llX", (unsigned long long)base_title_id);
                    name = placeholder;
                }

                GameCheatInfo info;
                info.title_id = base_title_id;
                info.name = name;
                info.build_id = "";
                info.version = GetTitleVersion(base_title_id);
                info.cheat_count = existing.size();

                m_games.push_back(std::move(info));
            }
        }

        offset += record_count;
    }

    nsExit();

    m_scanning = false;
    m_loaded = true;

    if (!m_games.empty()) {
        SetIndex(0);
    }

    log_write("[Cheats] Total: Found %zu games with cheats\n", m_games.size());
}





// CheatslipsLoginMenu implementation
CheatslipsLoginMenu::CheatslipsLoginMenu()
    : MenuBase{"CheatSlips Login", MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );
}

CheatslipsLoginMenu::~CheatslipsLoginMenu() = default;

void CheatslipsLoginMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
}

void CheatslipsLoginMenu::Draw(NVGcontext* vg, Theme* theme) {
    // Don't draw anything - keyboard provides the UI
    (void)vg;
    (void)theme;
}

void CheatslipsLoginMenu::OnFocusGained() {
    m_keyboard_shown = false;
    ShowEmailKeyboard();
}

void CheatslipsLoginMenu::ShowEmailKeyboard() {
    if (m_keyboard_shown) return;
    m_keyboard_shown = true;

    std::string email;
    Result rc = swkbd::ShowText(email, "CheatSlips Email", "", 0, 32);
    if (R_FAILED(rc) || email.empty()) {
        SetPop();
        return;
    }

    m_email = email;
    m_state = LoginState::Password;
    m_keyboard_shown = false;
    ShowPasswordKeyboard();
}

void CheatslipsLoginMenu::ShowPasswordKeyboard() {
    if (m_keyboard_shown) return;
    m_keyboard_shown = true;

    std::string password;
    Result rc = swkbd::ShowText(password, "CheatSlips Password", "", 0, 32);
    if (R_FAILED(rc) || password.empty()) {
        SetPop();
        return;
    }

    m_password = password;
    Authenticate();
}

void CheatslipsLoginMenu::Authenticate() {
    auto token = AuthenticateCheatslips(m_email, m_password);
    if (token.empty()) {
        App::Notify("Login failed. Check your credentials."_i18n);
    } else {
        SaveCheatslipsToken(token);
        App::Notify("Logged in successfully!"_i18n);
    }
    SetPop();
}

} // namespace sphaira::ui::menu::hats
