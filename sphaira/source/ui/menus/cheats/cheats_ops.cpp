#include "ui/menus/cheats_menu.hpp"
#include "ui/menus/cheats/cheats_ops.hpp"
#include "ui/menus/cheats/cheat_game_select_menu.hpp"
#include "ui/menus/cheats/cheats_dmnt.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_db.hpp"
#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/error_box.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "threaded_file_transfer.hpp"
#include "i18n.hpp"
#include "yyjson_helper.hpp"
#include <switch.h>
#include <yyjson.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <format>
#include <ranges>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sphaira::ui::menu::hats {
namespace detail {
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

using namespace detail;

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
                        App::PushErrorBox(rc, "Failed to install cheats pack"_i18n);
                    }
                }
            );
        }
    );
}


} // namespace sphaira::ui::menu::hats
