#include "app.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "save_menu_internal.hpp"
#include "ui/progress_box.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sphaira::ui::menu::save {
namespace {

constexpr auto ENTRY_CHUNK_COUNT = 1000;

} // namespace

auto Menu::ListAccountSaves(const AccountUid& uid) -> std::vector<Entry> {
    std::vector<Entry> out;
    const auto infos = DiscoverSaveDataInfo(&uid, FsSaveDataType_Account);
    out.reserve(infos.size());
    for (const auto& info : infos) {
        out.emplace_back(info);
    }
    return out;
}

void Menu::ReadSaveEntries(u8 data_type, s64 account_index, std::vector<Entry>& out) const {
    if (data_type == FsSaveDataType_Account) {
        if (m_accounts.empty()) {
            return;
        }
        const auto index = account_index >= 0 ? account_index : 0;
        if (index >= static_cast<s64>(m_accounts.size())) {
            return;
        }
        const auto infos = DiscoverSaveDataInfo(&m_accounts[index].uid, FsSaveDataType_Account);
        for (const auto& info : infos) {
            out.emplace_back(info);
        }
    } else {
        const auto infos = DiscoverSaveDataInfo(nullptr, data_type);
        for (const auto& info : infos) {
            out.emplace_back(info);
        }
    }
}

void Menu::ScanHomebrew(bool keep_backup_cache) {
    TimeStamp ts;

    if (!keep_backup_cache) {
        m_backup_cache_valid = false;
    }

    FreeEntries();
    ClearSelection();
    ueventClear(&g_change_uevent);
    m_entries.reserve(ENTRY_CHUNK_COUNT);
    m_is_reversed = false;
    m_dirty = false;

    if (!m_accounts.empty() && m_account_enabled.size() != m_accounts.size()) {
        m_account_enabled.assign(m_accounts.size(), false);
        if (m_account_index < static_cast<s64>(m_accounts.size())) {
            m_account_enabled[m_account_index] = true;
        }
    }

    const auto account_indexes = GetSelectedAccountIndexes();
    for (const auto type : GetSelectedSaveTypes()) {
        if (type == FsSaveDataType_Account) {
            for (const auto account_index : account_indexes) {
                ReadSaveEntries(type, account_index, m_entries);
            }
        } else {
            ReadSaveEntries(type, -1, m_entries);
        }
    }

    std::vector<Entry> grouped;
    std::vector<std::string> keys;
    grouped.reserve(m_entries.size());
    keys.reserve(m_entries.size());
    for (auto& e : m_entries) {
        const auto key = DisplayEntryKey(e);
        const auto it = std::ranges::find(keys, key);
        if (it == keys.end()) {
            keys.emplace_back(key);
            grouped.emplace_back(e);
            continue;
        }

        const auto index = std::distance(keys.begin(), it);
        if (grouped[index].save_data_type != FsSaveDataType_Account && e.save_data_type == FsSaveDataType_Account) {
            grouped[index] = e;
        }
    }

    // classify live saves as installed vs deleted-game, and drop whichever the
    // "Show saves" filter has turned off. system saves are governed by the Data
    // Types filter instead, so they are always kept here.
    BuildInstalledAppIds();
    bool show_installed = App::GetSaveShowInstalled();
    bool show_deleted = App::GetSaveShowDeleted();
    bool show_backups = App::GetSaveShowBackups();

    if (m_category == Category::Installed) {
        show_installed = true;
        show_deleted = false;
        show_backups = false;
    } else if (m_category == Category::Deleted) {
        show_installed = false;
        show_deleted = true;
        show_backups = false;
    } else if (m_category == Category::Backups) {
        show_installed = false;
        show_deleted = false;
        show_backups = true;
    }

    m_entries.clear();
    m_entries.reserve(grouped.size() + m_installed_apps.size());

    if (m_category == Category::Installed) {
        std::unordered_map<u64, Entry> live_saves;
        for (const auto& e : grouped) {
            if (!IsSystemLikeSave(e.save_data_type)) {
                live_saves.emplace(e.application_id, e);
            }
        }

        for (const auto app_id : m_installed_apps) {
            if (m_app_id_filter && app_id != m_app_id_filter) {
                continue;
            }

            auto it = live_saves.find(app_id);
            if (it != live_saves.end()) {
                m_entries.emplace_back(it->second);
            } else {
                Entry e{};
                e.application_id = app_id;
                e.save_data_type = FsSaveDataType_Account;
                e.is_backup = false;
                m_entries.emplace_back(e);
            }
        }
        // installed apps left out of m_installed_apps (0x05… ids) still show when they have a save.
        for (const auto& [app_id, e] : live_saves) {
            if (m_installed_app_ids.contains(app_id) && !std::ranges::contains(m_installed_apps, app_id) &&
                (!m_app_id_filter || app_id == m_app_id_filter)) {
                m_entries.emplace_back(e);
            }
        }
    } else if (m_category == Category::Deleted) {
        for (const auto& e : grouped) {
            if (m_app_id_filter && e.application_id != m_app_id_filter) {
                continue;
            }
            if (IsSystemLikeSave(e.save_data_type)) {
                continue;
            }
            if (!m_installed_app_ids.contains(e.application_id)) {
                m_entries.emplace_back(e);
            }
        }
    } else if (m_category == Category::Backups) {
        // Backups only
    } else {
        std::unordered_set<u64> added_installed;
        for (const auto& e : grouped) {
            if (m_app_id_filter && e.application_id != m_app_id_filter) {
                continue;
            }

            if (IsSystemLikeSave(e.save_data_type)) {
                m_entries.emplace_back(e);
                continue;
            }

            const bool installed = m_installed_app_ids.contains(e.application_id);
            if (installed) {
                if (show_installed) {
                    m_entries.emplace_back(e);
                    added_installed.insert(e.application_id);
                }
            } else {
                if (show_deleted) {
                    m_entries.emplace_back(e);
                }
            }
        }

        if (show_installed) {
            for (const auto app_id : m_installed_apps) {
                if (m_app_id_filter && app_id != m_app_id_filter) {
                    continue;
                }
                if (!added_installed.contains(app_id)) {
                    Entry e{};
                    e.application_id = app_id;
                    e.save_data_type = FsSaveDataType_Account;
                    e.is_backup = false;
                    m_entries.emplace_back(e);
                }
            }
        }
    }

    // backup tiles come after every live save; remember the boundary so the
    // grid can split the two sections with the "Backups" divider.
    m_backup_start = static_cast<s64>(m_entries.size());
    bool has_uninstalled = false;
    for (const auto& e : m_entries) {
        if (!IsSystemLikeSave(e.save_data_type) && !m_installed_app_ids.contains(e.application_id)) {
            has_uninstalled = true;
            break;
        }
    }

    // the library scan opens every archive on the card (about 1 s for 20 backups), so a
    // tab switch reuses the last one. backups is a copy: its entries are moved out below.
    // ponytail: files added behind the menu's back (MTP/FTP while it stays open and focused)
    // show up after the next action or reopening the screen; add an fs watch if that matters.
    std::vector<Entry> backups;
    if (show_backups || has_uninstalled) {
        if (!m_backup_cache_valid) {
            // hundreds of DBI archives take seconds: show the new tab empty now, read the library
            // under a progress box (started from Update()), and scan again from its done callback.
            m_entries.clear();
            m_backup_start = 0;
            m_backup_scan_pending = true;
            SetIndex(0);
            return;
        }
        backups = m_backup_cache;
    }

    std::unordered_map<u64, std::string> backup_name_lookup;
    for (const auto& b : backups) {
        if (b.application_id && !IsSystemLikeSave(b.save_data_type) && !backup_name_lookup.contains(b.application_id)) {
            if (b.lang.name[0] != '\0' && !title::IsPlaceholderName(b.lang.name)) {
                backup_name_lookup.emplace(b.application_id, b.lang.name);
            } else if (!b.dbi_game_dir.empty() && !IsHex16(b.dbi_game_dir)) {
                backup_name_lookup.emplace(b.application_id, b.dbi_game_dir);
            }
        }
    }

    if (show_backups) {
        if (m_category == Category::Backups && !m_app_id_filter) {
            // one tile per game and owner: backups of different users are never one restore.
            // device/BCAT backups have no owner (uid 0) and share a tile per game.
            std::vector<std::string> app_order;
            std::unordered_map<std::string, std::vector<Entry>> app_groups;
            std::vector<Entry> system_entries;

            for (auto& b : backups) {
                if (!m_all_accounts && b.save_data_type == FsSaveDataType_Account) {
                    bool match = false;
                    for (const auto idx : account_indexes) {
                        if (idx >= 0 && idx < static_cast<s64>(m_accounts.size())) {
                            if (!std::memcmp(&b.uid, &m_accounts[idx].uid, sizeof(AccountUid))) {
                                match = true;
                                break;
                            }
                        }
                    }
                    if (!match) {
                        continue;
                    }
                }

                if (IsSystemLikeSave(b.save_data_type)) {
                    system_entries.emplace_back(std::move(b));
                } else if (b.application_id != 0) {
                    const bool by_user = b.save_data_type == FsSaveDataType_Account;
                    char tile_key[64];
                    std::snprintf(tile_key, sizeof(tile_key), "%016lX:%016lX%016lX", b.application_id,
                        by_user ? b.uid.uid[0] : 0, by_user ? b.uid.uid[1] : 0);
                    auto& group = app_groups[tile_key];
                    if (group.empty()) {
                        app_order.emplace_back(tile_key);
                    }
                    group.emplace_back(std::move(b));
                } else {
                    system_entries.emplace_back(std::move(b));
                }
            }

            for (const auto& tile_key : app_order) {
                auto& children = app_groups[tile_key];
                if (children.empty()) {
                    continue;
                }

                const auto app_id = children.front().application_id;
                Entry parent{};
                parent.application_id = app_id;
                parent.uid = children.front().uid;
                parent.backup_owner_name = children.front().backup_owner_name;
                parent.save_data_type = children.front().save_data_type;
                parent.is_backup = true;
                parent.is_game_parent = true;
                parent.backup_count = children.size();

                const auto it = backup_name_lookup.find(app_id);
                if (it != backup_name_lookup.end() && !it->second.empty()) {
                    std::strncpy(parent.lang.name, it->second.c_str(), sizeof(parent.lang.name) - 1);
                    parent.lang.name[sizeof(parent.lang.name) - 1] = '\0';
                } else {
                    for (const auto& c : children) {
                        if (c.lang.name[0] != '\0' && !title::IsPlaceholderName(c.lang.name)) {
                            std::strncpy(parent.lang.name, c.lang.name, sizeof(parent.lang.name) - 1);
                            parent.lang.name[sizeof(parent.lang.name) - 1] = '\0';
                            break;
                        }
                    }
                    if (parent.lang.name[0] == '\0') {
                        std::snprintf(parent.lang.name, sizeof(parent.lang.name), "Title %016lX", app_id);
                    }
                }

                u64 newest_ts = 0;
                for (auto& c : children) {
                    if (c.lang.name[0] == '\0') {
                        std::strncpy(c.lang.name, parent.lang.name, sizeof(c.lang.name) - 1);
                        c.lang.name[sizeof(c.lang.name) - 1] = '\0';
                    }
                    if (c.backup_timestamp > newest_ts) {
                        newest_ts = c.backup_timestamp;
                        parent.backup_path = c.backup_path;
                        parent.backup_is_directory = c.backup_is_directory;
                    }
                }
                parent.backup_timestamp = newest_ts;
                // section key on the Backups tab. backups arrive sorted by source, so the first
                // child is the game's highest-precedence source and games of one source stay adjacent.
                parent.backup_source = children.front().backup_source;

                parent.children = std::move(children);
                m_entries.emplace_back(std::move(parent));
            }

            for (auto& sys : system_entries) {
                m_entries.emplace_back(std::move(sys));
            }
        } else {
            for (auto& b : backups) {
                if (m_app_id_filter && b.application_id != m_app_id_filter) {
                    continue;
                }
                if (!m_all_accounts && b.save_data_type == FsSaveDataType_Account) {
                    bool match = false;
                    for (const auto idx : account_indexes) {
                        if (idx >= 0 && idx < static_cast<s64>(m_accounts.size())) {
                            if (!std::memcmp(&b.uid, &m_accounts[idx].uid, sizeof(AccountUid))) {
                                match = true;
                                break;
                            }
                        }
                    }
                    if (!match) {
                        continue;
                    }
                }
                m_entries.emplace_back(std::move(b));
            }
        }
    }

    for (auto& e : m_entries) {
        if (!IsSystemLikeSave(e.save_data_type) && !m_installed_app_ids.contains(e.application_id)) {
            if (e.lang.name[0] == '\0') {
                const auto it = backup_name_lookup.find(e.application_id);
                if (it != backup_name_lookup.end() && !it->second.empty()) {
                    std::strncpy(e.lang.name, it->second.c_str(), sizeof(e.lang.name) - 1);
                    e.lang.name[sizeof(e.lang.name) - 1] = '\0';
                } else {
                    std::snprintf(e.lang.name, sizeof(e.lang.name), "Title %016lX", e.application_id);
                }
            }
            e.lang.author[0] = '\0';
        }
    }

    log_write("games found: %zu time_taken: %.2f seconds %zu ms %zu ns\n", m_entries.size(), ts.GetSecondsD(), ts.GetMs(), ts.GetNs());
    this->Sort();
    SetIndex(0);
    ClearSelection();
}

void Menu::StartBackupScan() {
    auto out = std::make_shared<std::vector<Entry>>();
    App::Push<ProgressBox>(0, "Reading backups"_i18n, "", [this, out](auto pbox) -> Result {
        pbox->SetHideSpeed(true);
        ReadBackupEntries(*out, pbox);
        R_SUCCEED();
    }, [this, out](Result) {
        // a cancelled scan keeps what it read: marking it invalid would only start it again.
        m_backup_cache = std::move(*out);
        m_backup_cache_valid = true;
        m_keep_backup_cache = true;
        // archives without a stored name (safety copies) take it from another backup of the same user.
        std::unordered_map<std::string, std::string> owner_names;
        for (const auto& b : m_backup_cache) {
            if (!b.backup_owner_name.empty()) {
                owner_names.try_emplace(std::string(reinterpret_cast<const char*>(&b.uid), sizeof(b.uid)), b.backup_owner_name);
            }
        }
        for (auto& b : m_backup_cache) {
            const auto it = owner_names.find(std::string(reinterpret_cast<const char*>(&b.uid), sizeof(b.uid)));
            if (b.backup_owner_name.empty() && it != owner_names.end()) {
                b.backup_owner_name = it->second;
            }
        }
        ScanHomebrew(true);
    });
}

void Menu::BuildInstalledAppIds() {
    m_installed_app_ids.clear();
    m_installed_apps.clear();

    std::vector<NsApplicationRecord> records(ENTRY_CHUNK_COUNT);
    s32 offset = 0;
    while (true) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &count)) || count <= 0) {
            break;
        }

        for (s32 i = 0; i < count; i++) {
            const auto app_id = records[i].application_id;
            if (!app_id) {
                continue;
            }
            title::MetaEntries installed_content;
            if (R_FAILED(title::GetMetaEntries(app_id, installed_content)) || installed_content.empty()) {
                continue;
            }

            // 0x05… ids (forwarders, but also YouTube 05003A400C3DA000) count as installed so their
            // saves are listed; they get no empty "no save yet" tile of their own.
            if (m_installed_app_ids.insert(app_id).second && (app_id & 0x0500000000000000) != 0x0500000000000000) {
                m_installed_apps.push_back(app_id);
            }
        }
        offset += count;
    }
}

} // namespace sphaira::ui::menu::save
