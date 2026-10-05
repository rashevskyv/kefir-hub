#include "app.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_bundle_util.hpp"
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

void Menu::ScanHomebrew(bool keep_backup_cache) {
    TimeStamp ts;

    if (!keep_backup_cache) {
        m_backup_cache_valid = false;
    }
    m_backup_scan_pending = false; // set again below if this view needs the library

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
    BuildInstalledAppIds();
    const auto all_saves = DiscoverSaveDataInfo();
    std::unordered_map<u64, u64> save_sizes;
    std::vector<u64> game_order;
    std::unordered_map<u64, Entry> game_representatives;
    std::vector<Entry> system_entries;

    const bool system_only = m_save_type_enabled[SaveTypeIndex(FsSaveDataType_System)];
    const auto* active_uid = (!m_accounts.empty() && m_account_index >= 0 && m_account_index < static_cast<s64>(m_accounts.size()))
        ? &m_accounts[m_account_index].uid.uid[0] : nullptr;

    for (const auto& info : all_saves) {
        if (IsSystemLikeSave(info.save_data_type)) {
            if (system_only || m_save_type_enabled[SaveTypeIndex(info.save_data_type)]) {
                system_entries.emplace_back(info);
            }
            continue;
        }

        if (system_only || info.application_id == 0) {
            continue;
        }

        save_sizes[info.application_id] += info.size;

        auto [it, inserted] = game_representatives.try_emplace(info.application_id, info);
        if (inserted) {
            game_order.emplace_back(info.application_id);
        } else {
            const int cur_prio = bundle::SlotPriority(it->second.save_data_type, it->second.uid.uid, active_uid);
            const int new_prio = bundle::SlotPriority(info.save_data_type, info.uid.uid, active_uid);
            if (new_prio > cur_prio) {
                it->second = Entry(info);
            }
        }
    }

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
    m_entries.reserve(game_representatives.size() + m_installed_apps.size() + system_entries.size());

    std::unordered_set<u64> added_apps;

    if (m_category == Category::All) {
        for (auto& sys : system_entries) {
            m_entries.emplace_back(std::move(sys));
        }
    }

    if (m_category != Category::Backups) {
        for (const auto app_id : m_installed_apps) {
            const bool has_save = game_representatives.contains(app_id);
            if (bundle::ShouldShowGameTile(app_id, true, has_save, static_cast<bundle::Category>(m_category), show_installed, show_deleted, m_app_id_filter)) {
                if (added_apps.insert(app_id).second) {
                    if (has_save) {
                        m_entries.emplace_back(game_representatives[app_id]);
                    } else {
                        Entry e{};
                        e.application_id = app_id;
                        e.save_data_type = FsSaveDataType_Account;
                        e.is_backup = false;
                        m_entries.emplace_back(e);
                    }
                }
            }
        }

        for (const auto app_id : game_order) {
            const bool is_installed = m_installed_app_ids.contains(app_id);
            if (bundle::ShouldShowGameTile(app_id, is_installed, true, static_cast<bundle::Category>(m_category), show_installed, show_deleted, m_app_id_filter)) {
                if (added_apps.insert(app_id).second) {
                    m_entries.emplace_back(game_representatives[app_id]);
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
    if (show_backups) {
        if (!m_backup_cache_valid) {
            // hundreds of DBI archives take seconds: show the tab now with a progress bar in place of
            // the grid (DrawBackupScan), read the library on a thread, and scan again when it is done.
            m_entries.clear();
            m_backup_start = 0;
            m_backup_scan_pending = true;
            SetIndex(0);
            return;
        }
        backups = m_backup_cache;
    }

    // names for saves of deleted games: from the scanned library when there is one, else from the
    // backup folder names (Deleted Games never opens the archives).
    std::unordered_map<u64, std::string> backup_name_lookup;
    for (const auto& b : m_backup_cache_valid ? m_backup_cache : backups) {
        if (b.application_id && !IsSystemLikeSave(b.save_data_type) && !backup_name_lookup.contains(b.application_id)) {
            if (b.lang.name[0] != '\0' && !title::IsPlaceholderName(b.lang.name)) {
                backup_name_lookup.emplace(b.application_id, b.lang.name);
            } else if (!b.dbi_game_dir.empty() && !IsHex16(b.dbi_game_dir)) {
                backup_name_lookup.emplace(b.application_id, b.dbi_game_dir);
            }
        }
    }

    if (has_uninstalled && !m_backup_cache_valid) {
        ReadBackupNames(backup_name_lookup);
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
                    char tile_key[48];
                    std::snprintf(tile_key, sizeof(tile_key), "%u:%016lX", static_cast<unsigned>(b.backup_source), b.application_id);
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
                const auto rep_it = std::ranges::find_if(children, [](const Entry& c) {
                    return c.save_data_type == FsSaveDataType_Account;
                });
                const auto& rep = (rep_it != children.end()) ? *rep_it : children.front();
                parent.uid = rep.uid;
                parent.backup_owner_name = rep.backup_owner_name;
                parent.save_data_type = rep.save_data_type;
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

    for (auto& e : m_entries) {
        if (!e.is_backup) {
            const auto it = save_sizes.find(e.application_id);
            e.sort_size = it != save_sizes.end() ? it->second : 0;
        }
    }

    log_write("games found: %zu time_taken: %.2f seconds %zu ms %zu ns\n", m_entries.size(), ts.GetSecondsD(), ts.GetMs(), ts.GetNs());
    this->Sort();
    SetIndex(0);
    ClearSelection();
}

void Menu::StartBackupScan() {
    auto job = std::make_unique<BackupScanJob>();
    job->menu = this;
    const auto run = [](void* arg) {
        auto* j = static_cast<BackupScanJob*>(arg);
        j->menu->ReadBackupEntries(j->out, [j](size_t done, size_t total, const fs::FsPath& path) {
            {
                std::scoped_lock lock{j->mutex};
                j->path = path.toString();
            }
            j->done = done;
            j->total = total;
            return !j->cancel;
        });
        j->finished = true;
    };
    job->started = R_SUCCEEDED(threadCreate(&job->thread, run, job.get(), nullptr, 1024 * 128, PRIO_PREEMPTIVE, 1));
    if (job->started && R_FAILED(threadStart(&job->thread))) {
        threadClose(&job->thread);
        job->started = false;
    }
    if (!job->started) {
        run(job.get()); // no thread: the old blocking read
    }
    m_backup_scan = std::move(job);
}

void Menu::PollBackupScan() {
    if (!m_backup_scan) {
        if (m_backup_scan_pending) {
            StartBackupScan(); // pending stays set: the view that asked is rebuilt when it is done
        }
        return;
    }
    if (!m_backup_scan->finished) {
        return;
    }

    auto job = std::move(m_backup_scan);
    if (job->started) {
        threadWaitForExit(&job->thread);
        threadClose(&job->thread);
    }
    m_backup_cache = std::move(job->out);
    m_backup_cache_valid = true;
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
    if (m_backup_scan_pending) {
        ScanHomebrew(true);
    }
}

void Menu::StopBackupScan() {
    if (m_backup_scan && m_backup_scan->started) {
        m_backup_scan->cancel = true;
        threadWaitForExit(&m_backup_scan->thread);
        threadClose(&m_backup_scan->thread);
    }
    m_backup_scan.reset();
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
            // an archived game keeps its record and content list with storage None, and update/DLC
            // can outlive the base: installed means the base program is on a storage.
            title::MetaEntries installed_content;
            if (R_FAILED(title::GetMetaEntries(app_id, installed_content, title::ContentFlag_Application)) ||
                std::ranges::none_of(installed_content, [](const auto& m) {
                    return m.storageID == NcmStorageId_SdCard || m.storageID == NcmStorageId_BuiltInUser || m.storageID == NcmStorageId_GameCard;
                })) {
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
