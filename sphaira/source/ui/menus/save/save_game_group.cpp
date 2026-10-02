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

    // one backup group (one save of one user): straight to its actions, as DBI does.
    // restoring several saves at once is done by selecting tiles with X.
    if (game.children.size() == 1) {
        PromptBackupGroupAction({game.children.front()});
        return;
    }

    PopupList::Items items;

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
        if (op_index && *op_index >= 0 && static_cast<size_t>(*op_index) < game.children.size()) {
            PromptBackupGroupAction({game.children[*op_index]});
        }
    });

    popup->SetMenuStyle(true);
    App::Push(std::move(popup));
}

} // namespace sphaira::ui::menu::save
