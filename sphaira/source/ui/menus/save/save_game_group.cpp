#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_slot_backend.hpp"
#include "ui/menus/save/save_batch_util.hpp"
#include "ui/menus/save/save_bundle_util.hpp"
#include "save_menu_internal.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"

#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save/save_folder_discovery.hpp"
#include "path_util.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sphaira::ui::menu::save {

auto ExpandGameGroups(const std::vector<Entry>& entries) -> std::vector<Entry> {
    std::vector<Entry> out;
    for (const auto& e : entries) {
        if (e.is_game_parent && !e.children.empty()) {
            for (const auto& child : e.children) {
                out.push_back(child);
            }
        } else {
            out.push_back(e);
        }
    }
    return out;
}

void KeepNewestPerSlot(std::vector<Entry>& groups) {
    KeepNewestPerKey(groups,
        [](const Entry& e) { return bundle::FormatSourceGroupKey(static_cast<bundle::BackupSourceId>(e.backup_source), BackupGroupKey(e)); },
        [](const Entry& e) { return e.backup_timestamp; });
}

auto AccountPickerItems(const std::vector<AccountProfileBase>& accounts) -> std::vector<std::string> {
    std::vector<std::string> items;
    std::vector<std::string> tags;
    for (const auto& acc : accounts) {
        char tag[8];
        std::snprintf(tag, sizeof(tag), "%04X", static_cast<unsigned>(acc.uid.uid[1] & 0xFFFF));
        items.emplace_back(acc.nickname);
        tags.emplace_back(tag);
    }
    DisambiguateLabels(items, tags);
    return items;
}

auto BackupPickerTitle(const Entry& seed, const std::vector<AccountProfileBase>& accounts) -> std::string {
    // two backups of one game from different users look the same otherwise.
    std::string title = "Restore for user"_i18n + " (";
    if (seed.GetName() && seed.GetName()[0] != '\0') {
        title += std::string(seed.GetName()) + " · ";
    }
    title += FormatBackupAccount(seed, accounts) + " · " + FormatBackupTimestamp(seed.backup_timestamp, false);
    if (seed.save_data_index != 0) {
        title += " · #" + std::to_string(seed.save_data_index);
    }
    return title + ")";
}

auto BackupTileOwner(const Entry& tile, const std::vector<AccountProfileBase>& accounts) -> std::string {
    if (tile.is_game_parent) {
        return "All saves (bundle)"_i18n;
    }
    return tile.save_data_type == FsSaveDataType_Account ? FormatBackupAccount(tile, accounts) : FormatSaveTypeLabel(tile.save_data_type);
}

void Menu::OpenGameBackupGroup(const Entry& game) {
    if (game.children.empty()) {
        App::Push<OptionBox>("No backups found for this game."_i18n, "OK"_i18n);
        return;
    }

    // one backup group (one save of one user): straight to its actions, as DBI does.
    // restoring several saves at once is done by selecting tiles with X.
    if (game.children.size() == 1) {
        PromptBackupGroupAction({game.children.front()});
        return;
    }

    PopupList::Items items;
    items.emplace_back("All saves (bundle)"_i18n);

    for (const auto& child : game.children) {
        std::string label;
        if (child.save_data_type == FsSaveDataType_Account) {
            label = "Account: "_i18n + FormatBackupAccount(child, m_accounts);
            if (child.save_data_index != 0) {
                label += " (slot "_i18n + std::to_string(child.save_data_index) + ")";
            }
            if (child.save_data_rank == FsSaveDataRank_Secondary) {
                label += " (secondary)"_i18n;
            }
        } else {
            label = FormatSaveTypeLabel(child.save_data_type);
            if (child.save_data_index != 0) {
                label += " (slot "_i18n + std::to_string(child.save_data_index) + ")";
            }
        }

        label += " • " + std::string(GetBackupSourceLabel(child.backup_source));

        if (child.backup_count > 0) {
            label += " • " + std::to_string(child.backup_count) + " " +
                (child.backup_count == 1 ? "archive"_i18n : "archives"_i18n);
        }
        if (child.backup_timestamp > 0) {
            label += " • " + FormatBackupTimestamp(child.backup_timestamp, false);
        }

        items.emplace_back(std::move(label));
    }

    std::string title = game.GetName();
    if (title.empty()) {
        title = "Backups"_i18n;
    }

    auto popup = std::make_unique<PopupList>(title, items, [this, game](auto op_index) {
        if (!op_index || *op_index < 0) {
            return;
        }
        if (*op_index == 0) {
            PromptBackupGroupAction({game});
            return;
        }
        const size_t child_idx = static_cast<size_t>(*op_index - 1);
        if (child_idx < game.children.size()) {
            PromptBackupGroupAction({game.children[child_idx]});
        }
    });

    popup->SetMenuStyle(true);
    App::Push(std::move(popup));
}

auto Menu::CollectBackupEntriesForRestore(
    const std::vector<Entry>& seeds,
    const dump::DumpLocation& location,
    const fs::FsPath& backup_root,
    std::optional<BackupSource> source_filter) const -> std::vector<Entry>
{
    std::vector<Entry> out;
    if (seeds.empty()) {
        return out;
    }

    const bool is_default_sd = (location.entry.type == dump::DumpLocationType_SdCard &&
                               (backup_root.empty() || backup_root == DEFAULT_BACKUP_ROOT));

    std::vector<Entry> location_entries;
    const std::vector<Entry>* pool_ptr = nullptr;

    if (is_default_sd) {
        if (!m_backup_cache_valid) {
            ReadBackupEntries(m_backup_cache);
            m_backup_cache_valid = true;
        }
        pool_ptr = &m_backup_cache;
    } else {
        fs::FsStdio stdio_fs;
        fs::FsNativeSd sd_fs;
        fs::Fs* probe_fs = (location.entry.type == dump::DumpLocationType_Stdio || backup_root.starts_with("ums"))
            ? static_cast<fs::Fs*>(&stdio_fs)
            : static_cast<fs::Fs*>(&sd_fs);
        ReadBackupEntries(location_entries, {}, probe_fs, backup_root);
        pool_ptr = &location_entries;
    }

    const auto& pool = *pool_ptr;
    std::unordered_set<std::string> seen_group_keys;

    for (const auto& seed : seeds) {
        if (seed.is_backup) {
            if (seed.is_game_parent && !seed.children.empty()) {
                for (const auto& child : seed.children) {
                    if (source_filter && child.backup_source != *source_filter) continue;
                    if (seen_group_keys.insert(BackupGroupKey(child)).second) {
                        out.emplace_back(child);
                    }
                }
            } else {
                if (source_filter && seed.backup_source != *source_filter) continue;
                if (seen_group_keys.insert(BackupGroupKey(seed)).second) {
                    out.emplace_back(seed);
                }
            }
            continue;
        }

        // Live tile: find matching backup entries from pool
        std::vector<const Entry*> matching;
        for (const auto& b : pool) {
            const bool match = IsSystemLikeSave(seed.save_data_type)
                ? (b.system_save_data_id == seed.system_save_data_id)
                : (b.application_id == seed.application_id);
            if (match) {
                matching.emplace_back(&b);
            }
        }
        if (matching.empty()) {
            continue;
        }

        // Determine target backup source:
        // A normal Hub restore must use Hub history only.
        // External history must be used only when explicitly selected via source_filter.
        const BackupSource target_source = source_filter.value_or(BackupSource::KefirHub);

        // Collect and deduplicate slots strictly within target_source
        std::unordered_map<std::string, Entry> slot_groups;
        for (const auto* b : matching) {
            if (b->backup_source != target_source) {
                continue;
            }
            const auto key = BackupGroupKey(*b);
            auto it = slot_groups.find(key);
            if (it == slot_groups.end()) {
                slot_groups.emplace(key, *b);
            } else {
                bundle::BackupMemberCandidate curr{it->second.backup_timestamp, it->second.backup_path.s, static_cast<bundle::BackupSourceId>(it->second.backup_source)};
                bundle::BackupMemberCandidate cand{b->backup_timestamp, b->backup_path.s, static_cast<bundle::BackupSourceId>(b->backup_source)};
                if (bundle::IsCandidateBetter(cand, curr)) {
                    it->second = *b;
                }
            }
        }

        for (auto& [key, b] : slot_groups) {
            if (seen_group_keys.insert(key).second) {
                Entry copy = std::move(b);
                copy.image = seed.image;
                if (copy.lang.name[0] == '\0' && seed.GetName() && seed.GetName()[0] != '\0') {
                    std::strncpy(copy.lang.name, seed.GetName(), sizeof(copy.lang.name) - 1);
                    copy.lang.name[sizeof(copy.lang.name) - 1] = '\0';
                }
                out.emplace_back(std::move(copy));
            }
        }
    }

    return out;
}

auto Menu::GetAvailableBackupSources(
    const std::vector<Entry>& seeds,
    const dump::DumpLocation& location,
    const fs::FsPath& backup_root) const -> std::vector<BackupSource>
{
    std::vector<BackupSource> out;
    if (seeds.empty()) {
        return out;
    }

    const bool is_default_sd = (location.entry.type == dump::DumpLocationType_SdCard &&
                               (backup_root.empty() || backup_root == DEFAULT_BACKUP_ROOT));

    std::vector<Entry> location_entries;
    const std::vector<Entry>* pool_ptr = nullptr;

    if (is_default_sd) {
        if (!m_backup_cache_valid) {
            ReadBackupEntries(m_backup_cache);
            m_backup_cache_valid = true;
        }
        pool_ptr = &m_backup_cache;
    } else {
        fs::FsStdio stdio_fs;
        fs::FsNativeSd sd_fs;
        fs::Fs* probe_fs = (location.entry.type == dump::DumpLocationType_Stdio || backup_root.starts_with("ums"))
            ? static_cast<fs::Fs*>(&stdio_fs)
            : static_cast<fs::Fs*>(&sd_fs);
        ReadBackupEntries(location_entries, {}, probe_fs, backup_root);
        pool_ptr = &location_entries;
    }

    const auto& pool = *pool_ptr;
    std::set<BackupSource> sources;

    for (const auto& seed : seeds) {
        if (seed.is_backup) {
            if (seed.is_game_parent && !seed.children.empty()) {
                for (const auto& child : seed.children) {
                    sources.insert(child.backup_source);
                }
            } else {
                sources.insert(seed.backup_source);
            }
            continue;
        }

        for (const auto& b : pool) {
            const bool match = IsSystemLikeSave(seed.save_data_type)
                ? (b.system_save_data_id == seed.system_save_data_id)
                : (b.application_id == seed.application_id);
            if (match) {
                sources.insert(b.backup_source);
            }
        }
    }

    if (sources.contains(BackupSource::KefirHub)) {
        out.push_back(BackupSource::KefirHub);
    }
    for (const auto s : sources) {
        if (s != BackupSource::KefirHub) {
            out.push_back(s);
        }
    }
    return out;
}

} // namespace sphaira::ui::menu::save
