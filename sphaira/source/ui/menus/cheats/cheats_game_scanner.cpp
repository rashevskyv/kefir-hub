#include "ui/menus/cheats_menu.hpp"
#include "ui/menus/cheats/cheat_game_select_menu.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "nacp_util.hpp"
#include "title_info.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include <switch.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <unordered_set>
#include <vector>

namespace sphaira::ui::menu::hats::detail {
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


} // namespace sphaira::ui::menu::hats::detail
