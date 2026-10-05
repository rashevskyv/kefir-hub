#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "dumper.hpp"

#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/file_picker.hpp"

#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"

#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_bundle_util.hpp"
#include "save_menu_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {
namespace {

constexpr std::array<u8, 7> SAVE_TYPE_VALUES{
    FsSaveDataType_System,
    FsSaveDataType_Account,
    FsSaveDataType_Bcat,
    FsSaveDataType_Device,
    FsSaveDataType_Temporary,
    FsSaveDataType_Cache,
    FsSaveDataType_SystemBcat,
};

} // namespace

auto Menu::CollectActionEntries(const std::vector<Entry>& seeds, const std::vector<u8>& types, const std::vector<s64>& account_indexes) -> std::vector<Entry> {
    std::set<u64> app_ids;
    std::set<u64> system_ids;
    // a rescanned entry has no name, and an uninstalled title would then load as
    // "Corrupted": remember the name the list shows (not the synthetic "Title <id>").
    std::map<u64, const Entry*> seed_names;
    for (const auto& e : seeds) {
        if (IsSystemLikeSave(e.save_data_type)) {
            system_ids.emplace(e.system_save_data_id);
        } else {
            app_ids.emplace(e.application_id);
            char fallback[32];
            std::snprintf(fallback, sizeof(fallback), "Title %016lX", e.application_id);
            if (e.lang.name[0] != '\0' && !title::IsPlaceholderName(e.lang.name) && std::strcmp(e.lang.name, fallback) != 0) {
                seed_names.emplace(e.application_id, &e);
            }
        }
    }

    std::set<std::string> seen;
    std::vector<Entry> out;
    for (const auto type : types) {
        std::vector<Entry> scanned;
        if (type == FsSaveDataType_Account) {
            for (const auto account_index : account_indexes) {
                ReadSaveEntries(type, account_index, scanned);
            }
        } else {
            ReadSaveEntries(type, -1, scanned);
        }

        for (auto& e : scanned) {
            const auto matches = IsSystemLikeSave(e.save_data_type) ?
                system_ids.contains(e.system_save_data_id) :
                app_ids.contains(e.application_id);
            if (!matches) {
                continue;
            }

            const auto key = SaveEntryKey(e);
            if (seen.insert(key).second) {
                if (const auto it = seed_names.find(e.application_id); it != seed_names.end() && e.lang.name[0] == '\0') {
                    std::memcpy(e.lang.name, it->second->lang.name, sizeof(e.lang.name));
                }
                out.emplace_back(e);
            }
        }
    }

    return out;
}

void Menu::PromptSaveTypeOptions(SaveOp op) {
    const auto seeds = GetSelectedEntries();
    if (seeds.empty()) {
        return;
    }

    if (op == SaveOp::Delete && (m_category == Category::Backups || std::ranges::all_of(seeds, [](const auto& e){ return e.is_backup; }))) {
        const auto prompt = seeds.size() == 1
            ? "Are you sure you want to delete backups for "_i18n + seeds.front().GetName() + "?"
            : "Are you sure you want to delete backups for the selected games?"_i18n;

        App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [this, seeds](auto choice) {
            if (choice && *choice == 1) {
                App::PopToMenu();
                DeleteSaves(ExpandGameGroups(seeds));
            }
        }, seeds.front().image);
        return;
    }

    std::vector<s64> all_account_indexes;
    for (s64 i = 0; i < static_cast<s64>(m_accounts.size()); i++) {
        all_account_indexes.emplace_back(i);
    }

    std::vector<Entry> available_entries;
    if (op == SaveOp::Restore) {
        available_entries = CollectBackupEntriesForRestore(seeds, MakeSdCardDumpLocation(), fs::FsPath{DEFAULT_BACKUP_ROOT});
    } else {
        const std::vector<u8> all_types{SAVE_TYPES.begin(), SAVE_TYPES.end()};
        available_entries = CollectActionEntries(seeds, all_types, all_account_indexes);
        if (available_entries.empty()) {
            App::Push<OptionBox>("No matching saves found."_i18n, "OK"_i18n);
            return;
        }
    }

    struct ActionState {
        bool all_accounts{true};
        std::vector<u8> account_enabled{};
        bool accounts_available{true};
        std::vector<u8> account_has_backup{};
        std::array<u8, SAVE_TYPE_VALUES.size()> type_available{};
        std::array<u8, SAVE_TYPE_VALUES.size()> type_enabled{};
        std::vector<dump::DumpLocation> locations{};
        std::vector<fs::FsPath> location_base_paths{};
        SidebarEntryArray::Items location_items{};
        // normalized key per entry (parallel to the vectors above), used to
        // detect when a freshly picked folder is already in this session's list.
        std::vector<std::string> location_keys{};
        s64 location_index{};
        std::optional<BackupSource> selected_source{};
        SidebarEntryTextBase* source_entry{nullptr};
    };

    auto state = std::make_shared<ActionState>();
    state->account_enabled = m_account_enabled;
    if (state->account_enabled.size() != m_accounts.size()) {
        state->account_enabled.assign(m_accounts.size(), false);
        if (!state->account_enabled.empty()) {
            state->account_enabled[m_account_index] = true;
        }
    }
    state->account_has_backup.assign(m_accounts.size(), true);
    for (const auto& s : seeds) {
        if (s.is_backup && s.backup_source != BackupSource::Other) { state->selected_source = s.backup_source; break; }
        for (const auto& c : s.children) {
            if (c.is_backup && c.backup_source != BackupSource::Other) { state->selected_source = c.backup_source; break; }
        }
        if (state->selected_source) break;
    }

    for (const auto& e : available_entries) {
        state->type_available[SaveTypeIndex(e.save_data_type)] = true;
    }
    if (op == SaveOp::Restore && available_entries.empty()) {
        for (const auto& s : seeds) {
            if (!IsSystemLikeSave(s.save_data_type)) {
                state->type_available[SaveTypeIndex(FsSaveDataType_Account)] = true;
            }
        }
    }

    const auto system_index = SaveTypeIndex(FsSaveDataType_System);
    bool has_non_system{};
    for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
        if (i != system_index && state->type_available[i]) {
            has_non_system = true;
            state->type_enabled[i] = true;
        }
    }
    if (!has_non_system && state->type_available[system_index]) {
        state->type_enabled[system_index] = true;
    }

    const fs::FsPath default_backup_root{DEFAULT_BACKUP_ROOT};
    const auto stdio_locations = location::GetStdio(true);

    // de-dup key set: the default /dumps entry and every stdio mount are
    // never repeated by the recent-folder history below.
    std::vector<fs::FsPath> stdio_backup_roots(stdio_locations.size());
    std::set<std::string> seen_keys;
    seen_keys.insert(MakeLocationKey(RecentBackupDir{false, "", "", default_backup_root}));
    for (size_t i = 0; i < stdio_locations.size(); i++) {
        stdio_backup_roots[i] = stdio_locations[i].dump_path.empty() ? default_backup_root : fs::FsPath{stdio_locations[i].dump_path};
        seen_keys.insert(MakeLocationKey(RecentBackupDir{true, stdio_locations[i].mount, "", stdio_backup_roots[i]}));
    }

    state->locations.emplace_back(MakeSdCardDumpLocation());
    state->location_base_paths.emplace_back(default_backup_root);
    state->location_items.emplace_back(MakeSdLocationLabel(default_backup_root));
    state->location_keys.emplace_back(MakeLocationKey(RecentBackupDir{false, "", "", default_backup_root}));

    // up to 5 most recently confirmed "Choose Folder..." picks, newest first.
    for (const auto& dir : GetRecentBackupDirs()) {
        if (!seen_keys.insert(MakeLocationKey(dir)).second) {
            continue;
        }

        state->locations.emplace_back(MakeDumpLocationFromRecent(dir));
        state->location_base_paths.emplace_back(dir.path);
        state->location_items.emplace_back(dir.stdio ? MakeLocationLabel(dir.name, dir.path) : MakeSdLocationLabel(dir.path));
        state->location_keys.emplace_back(MakeLocationKey(dir));
    }

    for (s32 i = 0; i < static_cast<s32>(stdio_locations.size()); i++) {
        dump::DumpLocation location{};
        location.entry = {dump::DumpLocationType_Stdio, i};
        location.stdio = stdio_locations;

        state->locations.emplace_back(std::move(location));
        state->location_base_paths.emplace_back(stdio_backup_roots[i]);
        state->location_items.emplace_back(MakeLocationLabel(stdio_locations[i].name, stdio_backup_roots[i]));
        state->location_keys.emplace_back(MakeLocationKey(RecentBackupDir{true, stdio_locations[i].mount, "", stdio_backup_roots[i]}));
    }

    const auto def_key = App::GetSaveDefaultLocation();
    if (!def_key.empty()) {
        for (s64 i = 0; i < static_cast<s64>(state->location_keys.size()); i++) {
            if (state->location_keys[i] == def_key) {
                state->location_index = i;
                break;
            }
        }
    }

    const auto title = (op == SaveOp::Restore) ? "Restore Options"_i18n : (op == SaveOp::Delete) ? "Delete Options"_i18n : "Backup Options"_i18n;
    const auto action_label = (op == SaveOp::Restore) ? "Start Restore"_i18n : (op == SaveOp::Delete) ? "Delete Saves"_i18n : "Start Backup"_i18n;
    const auto action_desc = (op == SaveOp::Restore) ? "Begin restoring saves from the selected location."_i18n :
                             (op == SaveOp::Delete)  ? "Permanently delete save data for selected games and accounts."_i18n :
                                                       "Begin backing up saves to the selected location."_i18n;

    auto options = std::make_unique<Sidebar>(title, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCallback>(action_label, [this, state, seeds, op]() {
        std::vector<u8> selected_types;
        const auto system_index = SaveTypeIndex(FsSaveDataType_System);
        if (state->type_enabled[system_index]) {
            selected_types.emplace_back(FsSaveDataType_System);
        } else {
            for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
                if (i != system_index && state->type_enabled[i]) selected_types.emplace_back(SAVE_TYPES[i]);
            }
        }

        std::vector<s64> selected_accounts;
        for (s64 i = 0; i < static_cast<s64>(state->account_enabled.size()); i++) {
            if (state->all_accounts || state->account_enabled[i]) selected_accounts.emplace_back(i);
        }

        const auto location_index = std::min<s64>(state->location_index, static_cast<s64>(state->locations.size() - 1));
        const auto location = state->locations[location_index];
        const auto backup_root = state->location_base_paths[location_index];

        if (op == SaveOp::Restore) {
            const auto avail_sources = GetAvailableBackupSources(seeds, location, backup_root);
            if (avail_sources.empty()) {
                App::Notify("No backups found in this location."_i18n);
                return;
            }
            if (!state->selected_source.has_value()) {
                App::Notify("Please select a backup source."_i18n);
                return;
            }

            auto restore_entries = CollectBackupEntriesForRestore(seeds, location, backup_root, state->selected_source);
            std::vector<std::pair<uint64_t, uint64_t>> console_account_uids;
            std::vector<bool> console_account_enabled;
            for (size_t i = 0; i < state->account_enabled.size() && i < m_accounts.size(); i++) {
                console_account_uids.emplace_back(m_accounts[i].uid.uid[0], m_accounts[i].uid.uid[1]);
                console_account_enabled.emplace_back(state->account_enabled[i]);
            }

            std::vector<Entry> filtered_restore_entries;
            for (auto& e : restore_entries) {
                if (e.save_data_type == FsSaveDataType_Account) {
                    const uint64_t b_uid[2]{e.uid.uid[0], e.uid.uid[1]};
                    if (!bundle::IsAccountBackupEligibleForRestore(state->all_accounts, b_uid, console_account_uids, console_account_enabled)) {
                        continue;
                    }
                }
                filtered_restore_entries.emplace_back(std::move(e));
            }
            restore_entries = std::move(filtered_restore_entries);

            if (restore_entries.empty()) {
                App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
                return;
            }

            App::PopToMenu();
            StartRestore(std::move(restore_entries), location, backup_root, state->selected_source);
            return;
        }

        auto entries = CollectActionEntries(seeds, selected_types, selected_accounts);
        if (entries.empty()) {
            App::Push<OptionBox>("No matching saves found."_i18n, "OK"_i18n);
            return;
        }

        if (op == SaveOp::Delete) {
            const auto prompt = seeds.size() == 1
                ? "Are you sure you want to delete save data for "_i18n + seeds.front().GetName() + "?"
                : "Are you sure you want to delete save data for the selected games?"_i18n;

            App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [this, entries](auto choice) {
                if (choice && *choice == 1) {
                    App::PopToMenu();
                    DeleteSaves(ExpandGameGroups(entries));
                }
            }, seeds.front().image);
            return;
        }

        App::PopToMenu();
        BackupSaves(entries, location, backup_root);
    }, action_desc);

    auto refresh_location_state = [this](const std::shared_ptr<ActionState>& s, const std::vector<Entry>& seeds_in, SaveOp op_in) {
        if (op_in != SaveOp::Restore) return;
        const auto loc_idx = std::min<s64>(s->location_index, static_cast<s64>(s->locations.size() - 1));
        const auto sources = GetAvailableBackupSources(seeds_in, s->locations[loc_idx], s->location_base_paths[loc_idx]);

        std::vector<bundle::BackupSourceId> src_ids;
        for (auto src : sources) src_ids.push_back(static_cast<bundle::BackupSourceId>(src));
        std::optional<bundle::BackupSourceId> exp_id;
        if (s->selected_source.has_value()) exp_id = static_cast<bundle::BackupSourceId>(*s->selected_source);

        const auto res_src = bundle::ResolveRestoreSource(src_ids, exp_id);
        s->selected_source = res_src ? std::optional<BackupSource>(static_cast<BackupSource>(*res_src)) : std::nullopt;

        if (s->source_entry) {
            if (sources.empty()) {
                s->source_entry->SetValue("None"_i18n);
            } else if (s->selected_source.has_value()) {
                s->source_entry->SetValue(GetBackupSourceLabel(*s->selected_source));
            } else {
                s->source_entry->SetValue("Select"_i18n);
            }
        }

        const auto pool = CollectBackupEntriesForRestore(seeds_in, s->locations[loc_idx], s->location_base_paths[loc_idx], s->selected_source);
        s->account_has_backup.assign(m_accounts.size(), false);
        s->accounts_available = false;
        for (const auto& b : pool) {
            if (b.save_data_type == FsSaveDataType_Account) {
                s->accounts_available = true;
                for (size_t a = 0; a < m_accounts.size(); a++) {
                    if (!std::memcmp(&m_accounts[a].uid, &b.uid, sizeof(AccountUid))) s->account_has_backup[a] = true;
                }
            }
        }
        for (size_t a = 0; a < m_accounts.size(); a++) {
            if (!s->account_has_backup[a]) s->account_enabled[a] = false;
        }
        if (!s->all_accounts && std::ranges::none_of(s->account_enabled, [](auto v){ return v; })) {
            for (size_t a = 0; a < m_accounts.size(); a++) {
                if (s->account_has_backup[a]) { s->account_enabled[a] = true; break; }
            }
        }
    };

    if (op != SaveOp::Delete) {
        options->Add<SidebarEntryHeader>("LOCATION"_i18n);
        auto* location_entry = options->Add<SidebarEntryTextBase>("Location"_i18n, state->location_items[state->location_index], [](){}, "Choose the storage and folder for backups. Game saves are written beneath the chosen folder in DBI format, while existing DBI folders remain discoverable during Restore."_i18n);
        location_entry->SetCallback([this, state, seeds, op, location_entry, refresh_location_state]() {
            auto items = state->location_items;
            const auto picker_index = static_cast<s64>(items.size());
            items.emplace_back("Choose Folder..."_i18n);

            App::Push<PopupList>("Location"_i18n, items, [this, state, seeds, op, location_entry, picker_index, refresh_location_state](auto op_index) {
                if (!op_index) {
                    return;
                }

                if (*op_index != picker_index) {
                    state->location_index = *op_index;
                    location_entry->SetValue(state->location_items[state->location_index]);
                    refresh_location_state(state, seeds, op);
                    return;
                }

                App::Push<filepicker::Menu>(
                    filepicker::LocationCallback{[this, state, seeds, op, location_entry, refresh_location_state](const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> bool {
                        const auto backup_root = NormalizeBackupRoot(path, fs_entry);
                        const auto is_stdio = fs_entry.type == filebrowser::FsType::Stdio;
                        const auto label = is_stdio ? MakeLocationLabel(fs_entry.name.toString(), backup_root) : MakeSdLocationLabel(backup_root);

                        const RecentBackupDir recent{
                            is_stdio,
                            is_stdio ? fs_entry.root.toString() : "",
                            fs_entry.name.toString(),
                            backup_root,
                        };
                        const auto key = MakeLocationKey(recent);

                        // if this folder is already in the current session's list
                        // (default, history or a mount, or a previous pick this
                        // session), just select it instead of adding a twin row.
                        const auto existing = std::ranges::find(state->location_keys, key);
                        if (existing != state->location_keys.end()) {
                            state->location_index = std::distance(state->location_keys.begin(), existing);
                        } else {
                            state->locations.emplace_back(MakeDumpLocationFromFsEntry(fs_entry));
                            state->location_base_paths.emplace_back(backup_root);
                            state->location_items.emplace_back(label);
                            state->location_keys.emplace_back(key);
                            state->location_index = static_cast<s64>(state->location_items.size() - 1);
                        }
                        location_entry->SetValue(state->location_items[state->location_index]);

                        AddRecentBackupDir(recent);
                        refresh_location_state(state, seeds, op);

                        return true;
                    }},
                    std::vector<std::string>{},
                    fs::FsPath{},
                    true
                );
            }, state->location_index);
        });

        if (op == SaveOp::Backup) {
            options->Add<SidebarEntryBool>("Auto-sync after backup"_i18n, App::GetSaveAutosync(), [](bool& v_out){
                App::SetSaveAutosync(v_out);
            }, "After each Backup, automatically upload only the newly created backup ZIP to WebDAV. Does not sync your whole backup library - use Sync with remote (Save Options) for that."_i18n);
        } else if (op == SaveOp::Restore) {
            options->Add<SidebarEntryBool>("Include remote backups"_i18n, App::GetSaveRestoreIncludeRemote(), [](bool& v_out){
                App::SetSaveRestoreIncludeRemote(v_out);
            }, "Before showing the backup list, download any backups that exist on your WebDAV remote but are missing on this console, so they can be restored too. Remote-only backups are marked with a cloud icon. Only applies when a single save is selected."_i18n);

            options->Add<SidebarEntryHeader>("SOURCE"_i18n);
            state->source_entry = options->Add<SidebarEntryTextBase>(
                "Backup Source"_i18n,
                "Auto"_i18n,
                [](){},
                "Choose which backup creator to restore from when multiple are present."_i18n);
            state->source_entry->SetCallback([this, state, seeds, op, refresh_location_state]() {
                const auto l_idx = std::min<s64>(state->location_index, static_cast<s64>(state->locations.size() - 1));
                const auto current_sources = GetAvailableBackupSources(seeds, state->locations[l_idx], state->location_base_paths[l_idx]);
                if (current_sources.empty()) {
                    App::Notify("No backups found in this location."_i18n);
                    return;
                }
                PopupList::Items items;
                for (const auto s : current_sources) {
                    items.emplace_back(GetBackupSourceLabel(s));
                }
                App::Push<PopupList>("Backup Source"_i18n, items, [state, current_sources, refresh_location_state, seeds, op](auto op_index) {
                    if (!op_index || *op_index < 0 || static_cast<size_t>(*op_index) >= current_sources.size()) {
                        return;
                    }
                    state->selected_source = current_sources[*op_index];
                    if (state->source_entry) {
                        state->source_entry->SetValue(GetBackupSourceLabel(*state->selected_source));
                    }
                    refresh_location_state(state, seeds, op);
                });
            });

            refresh_location_state(state, seeds, op);
        }
    }

    const bool show_account_section = (m_accounts.size() > 1) && (op == SaveOp::Restore || state->type_available[SaveTypeIndex(FsSaveDataType_Account)]);
    if (show_account_section) {
        options->Add<SidebarEntryHeader>("ACCOUNTS"_i18n);
        auto* all_acc = options->Add<SidebarEntryCheckbox>(
            "All Accounts"_i18n,
            [state](){ return state->all_accounts; },
            [state](bool enabled) {
                state->all_accounts = enabled;
                if (!enabled && std::ranges::none_of(state->account_enabled, [](auto v){ return v; }) && !state->account_enabled.empty()) {
                    state->account_enabled[0] = true;
                }
            }, "Include saves from all user accounts."_i18n);

        if (op == SaveOp::Restore) {
            all_acc->Depends([state](){ return state->accounts_available; }, "No backups found in this location."_i18n);
        }

        for (size_t i = 0; i < m_accounts.size(); i++) {
            auto* entry = options->Add<SidebarEntryCheckbox>(
                std::string{"    "} + m_accounts[i].nickname,
                [state, i](){ return i < state->account_enabled.size() && state->account_enabled[i]; },
                [state, i](bool enabled) {
                    if (i >= state->account_enabled.size()) return;
                    state->all_accounts = false;
                    state->account_enabled[i] = enabled;
                    if (std::ranges::none_of(state->account_enabled, [](auto v){ return v; })) {
                        state->account_enabled[i] = true;
                    }
                }, "Include saves from this user account."_i18n);

            if (op == SaveOp::Restore) {
                entry->Depends(
                    [state, i]() {
                        if (!state->accounts_available || state->all_accounts) return false;
                        return (i < state->account_has_backup.size()) && state->account_has_backup[i];
                    },
                    state->all_accounts ? "All Accounts is enabled."_i18n : "No backups found in this location."_i18n,
                    [state, i]() {
                        if (state->all_accounts && state->accounts_available) {
                            state->all_accounts = false;
                            if (i < state->account_has_backup.size() && state->account_has_backup[i]) {
                                state->account_enabled[i] = true;
                            }
                        }
                    });
            } else {
                entry->Depends([state](){ return !state->all_accounts; }, "All Accounts is enabled."_i18n, [state, i]() {
                    if (i < state->account_enabled.size()) {
                        state->all_accounts = false;
                        state->account_enabled[i] = true;
                    }
                });
            }
        }
    }

    const auto available_type_count = std::ranges::count_if(state->type_available, [](auto v){ return v; });
    const auto system_available = state->type_available[system_index];
    if (available_type_count <= 1 || (op != SaveOp::Delete && !system_available)) {
        return;
    }

    options->Add<SidebarEntryHeader>("SAVE TYPES"_i18n);
    for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
        if (!state->type_available[i]) {
            continue;
        }

        const auto type = SAVE_TYPES[i];
        const auto label = (system_available && type != FsSaveDataType_System) ?
            "    " + FormatSaveTypeLabel(type) :
            FormatSaveTypeLabel(type);

        auto* entry = options->Add<SidebarEntryCheckbox>(
            label,
            [state, i](){ return state->type_enabled[i]; },
            [state, i, type, system_index](bool enabled) {
                if (type == FsSaveDataType_System) {
                    state->type_enabled[i] = enabled;
                } else {
                    state->type_enabled[system_index] = false;
                    state->type_enabled[i] = enabled;
                }

                bool any{};
                for (size_t n = 0; n < state->type_enabled.size(); n++) {
                    if (state->type_enabled[n]) {
                        any = true;
                        break;
                    }
                }
                if (!any) {
                    state->type_enabled[i] = true;
                }
            });

        if (type != FsSaveDataType_System) {
            entry->Depends(
                [state, system_index](){ return !state->type_enabled[system_index]; },
                "System is enabled."_i18n,
                [state, i, system_index]() {
                    state->type_enabled[system_index] = false;
                    state->type_enabled[i] = true;
                });
        }
    }
}

} // namespace sphaira::ui::menu::save
