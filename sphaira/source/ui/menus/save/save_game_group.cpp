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
#include "save_menu_internal.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
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
        [](const Entry& e) { return BackupGroupKey(e); },
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
    return tile.save_data_type == FsSaveDataType_Account ? FormatBackupAccount(tile, accounts) : FormatSaveTypeLabel(tile.save_data_type);
}

void Menu::OpenGameBackupGroup(const Entry& game) {
    if (game.children.empty()) {
        App::Push<OptionBox>("No backups found for this game."_i18n, "OK"_i18n);
        return;
    }

    PopupList::Items items;
    items.emplace_back("Restore all"_i18n);

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
        if (!op_index) {
            return;
        }
        if (*op_index == 0) {
            RestoreAllForGame(game);
        } else {
            size_t child_idx = static_cast<size_t>(*op_index - 1);
            if (child_idx < game.children.size()) {
                PromptBackupGroupAction({game.children[child_idx]});
            }
        }
    });

    popup->SetMenuStyle(true);
    App::Push(std::move(popup));
}

void Menu::RestoreAllForGame(const Entry& game) {
    if (game.children.empty()) {
        App::Push<OptionBox>("No backups found for this game."_i18n, "OK"_i18n);
        return;
    }

    fs::FsNativeSd sd_fs;
    const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

    std::vector<Entry> valid_children;
    for (auto child : game.children) {
        if (child.backup_members.empty()) {
            child.backup_members = CollectGroupArchives(&sd_fs, child, backup_root);
            child.backup_count = child.backup_members.size();
        }
        if (child.backup_members.empty()) {
            std::string child_desc;
            if (child.save_data_type == FsSaveDataType_Account) {
                child_desc = "Account: "_i18n + FormatBackupAccount(child, m_accounts);
            } else {
                child_desc = GetSaveTypeLabel(child.save_data_type);
            }
            if (child.save_data_index != 0) {
                child_desc += " (slot "_i18n + std::to_string(child.save_data_index) + ")";
            }
            const std::string name = (child.GetName() && child.GetName()[0] != '\0') ? child.GetName() : game.GetName();
            const std::string msg = "Cannot restore: no admissible backup archive found for "_i18n +
                child_desc + " (" + name + ").\n\n" +
                "Batch restore requires valid archives for all items in the game group."_i18n;
            App::Push<OptionBox>(msg, "OK"_i18n);
            return;
        }
        child.backup_path = child.backup_members.front().path;
        child.backup_timestamp = child.backup_members.front().ts;
        child.backup_is_directory = child.backup_members.front().is_directory;
        valid_children.emplace_back(std::move(child));
    }
    KeepNewestPerSlot(valid_children);

    // Check Device/Bcat children for live targets FIRST.
    // If any lacks a compatible live target, explain which one is missing and block the batch before any save mutation.
    for (const auto& child : valid_children) {
        if (child.save_data_type != FsSaveDataType_Account) {
            const auto targets = FindLiveRestoreCandidates(child, nullptr);
            if (targets.empty()) {
                const std::string missing_type = GetSaveTypeLabel(child.save_data_type);
                const std::string name = (child.GetName() && child.GetName()[0] != '\0') ? child.GetName() : game.GetName();
                const std::string msg = "Cannot restore: compatible live save slot is missing for "_i18n +
                    missing_type + " (" + name + ").\n\n" +
                    "Device and BCAT saves cannot be created automatically. Please launch the game to create the live save slot first."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }
        }
    }

    auto seeds = std::make_shared<std::vector<Entry>>(std::move(valid_children));
    auto resolved_targets = std::make_shared<std::vector<Entry>>(seeds->size());
    auto seen_target_keys = std::make_shared<std::set<std::string>>();
    auto accounts = std::make_shared<std::vector<AccountProfileBase>>(App::GetAccountList());

    PromptRestoreAllDestinations(seeds, 0, accounts, resolved_targets, seen_target_keys, MakeSdCardDumpLocation(), backup_root, game.GetName());
}

void Menu::PromptRestoreAllDestinations(
    std::shared_ptr<std::vector<Entry>> seeds,
    size_t step,
    std::shared_ptr<std::vector<AccountProfileBase>> accounts,
    std::shared_ptr<std::vector<Entry>> resolved_targets,
    std::shared_ptr<std::set<std::string>> seen_target_keys,
    const dump::DumpLocation& location,
    const fs::FsPath& backup_root,
    const std::string& game_name) {

    if (step >= seeds->size()) {
        RestoreSaves(std::move(*seeds), std::move(*resolved_targets), location, backup_root);
        return;
    }

    const auto& current_seed = (*seeds)[step];

    const auto on_target_resolved = [this, seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, game_name](std::optional<Entry> target) {
        if (!target) {
            return;
        }

        const auto key = SaveEntryKey(*target);
        if (!seen_target_keys->insert(key).second) {
            // with one user on the console the answer cannot change: stop instead of asking again.
            const bool can_choose = accounts->size() > 1 && (*seeds)[step].save_data_type == FsSaveDataType_Account;
            App::Push<OptionBox>("This user already gets another backup of this game. Choose another user, or restore one backup on its own."_i18n, "OK"_i18n,
                [this, can_choose, seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, game_name](auto) {
                    if (can_choose) {
                        PromptRestoreAllDestinations(seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, game_name);
                    }
                });
            return;
        }

        (*resolved_targets)[step] = std::move(*target);
        PromptRestoreAllDestinations(seeds, step + 1, accounts, resolved_targets, seen_target_keys, location, backup_root, game_name);
    };

    if (current_seed.save_data_type != FsSaveDataType_Account) {
        const auto candidates = FindLiveRestoreCandidates(current_seed, nullptr);
        if (candidates.empty()) {
            const std::string missing_type = GetSaveTypeLabel(current_seed.save_data_type);
            const std::string msg = "Cannot restore: compatible live save slot is missing for "_i18n +
                missing_type + " (" + game_name + ").\n\n" +
                "Device and BCAT saves cannot be created automatically. Please launch the game to create the live save slot first."_i18n;
            App::Push<OptionBox>(msg, "OK"_i18n);
            return;
        }
        if (candidates.size() == 1) {
            on_target_resolved(candidates.front());
        } else {
            ResolveRestoreTarget(current_seed, nullptr, on_target_resolved);
        }
        return;
    }

    const auto resolve_for_uid = [this, &current_seed, on_target_resolved](const AccountUid* uid) {
        const auto candidates = FindLiveRestoreCandidates(current_seed, uid);
        if (candidates.empty()) {
            const auto archive_path = current_seed.backup_members.empty()
                ? current_seed.backup_path
                : current_seed.backup_members.front().path;
            PlanRestoreCreation(current_seed, uid ? *uid : AccountUid{}, archive_path, on_target_resolved);
            return;
        }
        if (candidates.size() == 1) {
            on_target_resolved(candidates.front());
        } else {
            ResolveRestoreTarget(current_seed, uid, on_target_resolved);
        }
    };

    if (accounts->empty()) {
        App::Push<OptionBox>("No user accounts found on this console."_i18n, "OK"_i18n);
        return;
    }

    if (accounts->size() == 1) {
        resolve_for_uid(&(*accounts)[0].uid);
        return;
    }

    const auto items = AccountPickerItems(*accounts);
    const auto prompt = BackupPickerTitle(current_seed, *accounts);

    auto popup = std::make_unique<PopupList>(prompt, items, [resolve_for_uid, accounts](auto op_index) {
        if (!op_index || *op_index >= static_cast<s64>(accounts->size())) {
            return;
        }
        const auto chosen_uid = (*accounts)[*op_index].uid;
        resolve_for_uid(&chosen_uid);
    });

    const auto& first = seeds->front();
    for (const auto& entry : m_entries) {
        if (entry.is_game_parent) {
            const bool match = IsSystemLikeSave(first.save_data_type)
                ? (entry.system_save_data_id == first.system_save_data_id)
                : (entry.application_id == first.application_id && !std::memcmp(&entry.uid, &first.uid, sizeof(AccountUid)));
            if (match) {
                auto* raw = popup.get();
                popup->SetAction(Button::B, Action{"Back"_i18n, [this, raw, entry]() {
                    raw->SetPop();
                    OpenGameBackupGroup(entry);
                }});
                break;
            }
        }
    }

    App::Push(std::move(popup));
}

} // namespace sphaira::ui::menu::save
