#include "ui/menus/cheats/cheat_game_select_menu.hpp"
#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_db.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "app.hpp"
#include "utils/utils.hpp"

#include <switch.h>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <ranges>
#include <algorithm>
#include <cctype>

namespace sphaira::ui::menu::hats {

using namespace detail;

void CheatGameSelectMenu::ScanGames() {
    m_scanning = true;
    FreeGames();

    std::unordered_map<u64, CachedCheatMetadata> cached_entries;
    {
        mutexLock(&g_cheat_metadata_cache_mutex);
        ON_SCOPE_EXIT(mutexUnlock(&g_cheat_metadata_cache_mutex));
        cached_entries = LoadCheatMetadataCacheUnlocked();
    }

    // Initialize ns service (like original sphaira game_menu)
    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] nsInitialize failed: %x\n", rc);
        m_scanning = false;
        m_loaded = true;
        return;
    }

    // Use chunked approach like original sphaira (game_menu.cpp ScanHomebrew)
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

        // Finished parsing all entries
        if (record_count == 0) {
            break;
        }

        log_write("[Cheats] Got %d records at offset %d\n", record_count, offset);

        // Process each record
        for (s32 i = 0; i < record_count; i++) {
            const auto& record = record_list[i];
            if (record.application_id == 0) continue;
            const auto base_title_id = GetBaseApplicationTitleId(record.application_id);
            if (!seen_title_ids.insert(base_title_id).second) {
                continue;
            }

            log_write("[Cheats] Processing %016lX\n", base_title_id);

            // Get version
            u32 version = GetTitleVersion(base_title_id);

            // Get title name using nsGetApplicationControlData
            std::string name = GetTitleName(base_title_id);
            if (name.empty()) {
                // Use placeholder name if we couldn't get it
                char placeholder[64];
                std::snprintf(placeholder, sizeof(placeholder), "Game %016llX", (unsigned long long)base_title_id);
                name = placeholder;
            }

            GameCheatInfo info;
            info.title_id = base_title_id;
            info.name = name;
            info.version = version;
            std::snprintf(info.lang.name, sizeof(info.lang.name), "%s", info.name.c_str());
            if (const auto it = cached_entries.find(info.title_id); it != cached_entries.end() &&
                it->second.version == info.version && IsValidBuildId(it->second.build_id)) {
                info.build_id = it->second.build_id;
            }

            m_games.push_back(std::move(info));
        }

        offset += record_count;
    }

    AppendGameCardGames(m_games, seen_title_ids);

    // Exit ns service when done
    nsExit();

    bool needs_cache_refresh = cached_entries.empty();
    if (!needs_cache_refresh && App::IsApplication()) {
        needs_cache_refresh = std::ranges::any_of(m_games, [&](const auto& game) {
            return !IsValidBuildId(game.build_id);
        });
    }

    if (needs_cache_refresh && App::IsApplication()) {
        log_write("[Cheats] Refreshing cheat metadata cache from game select menu\n");
        RefreshCheatMetadataCache();

        mutexLock(&g_cheat_metadata_cache_mutex);
        ON_SCOPE_EXIT(mutexUnlock(&g_cheat_metadata_cache_mutex));
        cached_entries = LoadCheatMetadataCacheUnlocked();

        for (auto& game : m_games) {
            if (const auto it = cached_entries.find(game.title_id); it != cached_entries.end() &&
                it->second.version == game.version && IsValidBuildId(it->second.build_id)) {
                game.build_id = it->second.build_id;
            }
        }
    }

    m_scanning = false;
    m_loaded = true;

    if (!m_games.empty()) {
        SetIndex(0);
    }

    log_write("[Cheats] Total: Found %zu games\n", m_games.size());
}

namespace detail {

auto ImportManualCheatFile(u64 title_id, const fs::FsPath& source_path, const std::string& target_build_id) -> Result {
    fs::FsNativeSd fs;

    const auto build_id = NormalizeBuildId(target_build_id);
    if (!IsValidBuildId(build_id)) {
        log_write("[Cheats] Manual import rejected invalid target Build ID: %s\n", target_build_id.c_str());
        return 1;
    }

    std::vector<u8> data;
    R_TRY(fs.read_entire_file(source_path, data));
    const auto sanitized_content = SanitizeCheatText({reinterpret_cast<const char*>(data.data()), data.size()}).text;
    if (sanitized_content.empty()) {
        log_write("[Cheats] Manual import rejected invalid cheat file: %s\n", source_path.s);
        return 1;
    }

    const auto cheats_dir = GetCheatsDirPath(title_id);
    R_TRY(fs.CreateDirectoryRecursively(cheats_dir.c_str()));

    const auto dest_path = GetManualCheatImportPath(title_id, build_id);
    const auto content = std::vector<u8>(
        reinterpret_cast<const u8*>(sanitized_content.data()),
        reinterpret_cast<const u8*>(sanitized_content.data()) + sanitized_content.size()
    );
    R_TRY(fs.write_entire_file(dest_path, content));

    log_write("[Cheats] Imported manual cheat %s to %s\n", source_path.s, dest_path.s);
    return 0;
}

} // namespace detail

} // namespace sphaira::ui::menu::hats
