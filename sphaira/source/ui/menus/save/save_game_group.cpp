#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_slot_backend.hpp"
#include "save_menu_internal.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"

#include <algorithm>
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
            App::Push<OptionBox>("Duplicate restore target slot selected."_i18n, "OK"_i18n);
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

    PopupList::Items items;
    for (const auto& acc : *accounts) {
        items.emplace_back(acc.nickname);
    }

    std::string prompt = "Restore for user"_i18n;
    if (current_seed.save_data_index != 0) {
        prompt += " (slot "_i18n + std::to_string(current_seed.save_data_index) + ")";
    }

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
                : (entry.application_id == first.application_id);
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
