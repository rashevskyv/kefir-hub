#include "app.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "save_menu_internal.hpp"
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

void Menu::ScanHomebrew() {
    TimeStamp ts;

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

    std::vector<Entry> backups;
    if (show_backups || has_uninstalled) {
        ReadBackupEntries(backups);
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
            std::vector<u64> app_order;
            std::unordered_map<u64, std::vector<Entry>> app_groups;
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
                    auto& group = app_groups[b.application_id];
                    if (group.empty()) {
                        app_order.push_back(b.application_id);
                    }
                    group.emplace_back(std::move(b));
                } else {
                    system_entries.emplace_back(std::move(b));
                }
            }

            for (const auto app_id : app_order) {
                auto& children = app_groups[app_id];
                if (children.empty()) {
                    continue;
                }

                Entry parent{};
                parent.application_id = app_id;
                parent.save_data_type = FsSaveDataType_Account;
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
                bool first_child = true;
                bool multi_source = false;
                BackupSource common_source = BackupSource::Other;

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
                    if (first_child) {
                        common_source = c.backup_source;
                        first_child = false;
                    } else if (c.backup_source != common_source) {
                        multi_source = true;
                    }
                }
                parent.backup_timestamp = newest_ts;
                parent.backup_source = multi_source ? BackupSource::Other : common_source;

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
            if ((app_id & 0x0500000000000000) == 0x0500000000000000) {
                continue;
            }
            title::MetaEntries installed_content;
            if (R_FAILED(title::GetMetaEntries(app_id, installed_content)) || installed_content.empty()) {
                continue;
            }

            if (m_installed_app_ids.insert(app_id).second) {
                m_installed_apps.push_back(app_id);
            }
        }
        offset += count;
    }
}

} // namespace sphaira::ui::menu::save
