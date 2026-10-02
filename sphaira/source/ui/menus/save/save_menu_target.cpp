#include "app.hpp"
#include "i18n.hpp"
#include "title_info.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/menus/save/save_paths.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {
namespace {

auto FormatTargetSlotLabel(const Entry& target, const std::vector<AccountProfileBase>& accounts) -> std::string {
    std::string game_name;
    if (target.GetName() && target.GetName()[0] != '\0' && !title::IsPlaceholderName(target.GetName())) {
        game_name = target.GetName();
    } else {
        auto data = title::Get(target.application_id);
        if (data && data->lang.name[0] != '\0' && !title::IsPlaceholderName(data->lang.name)) {
            game_name = data->lang.name;
        } else if (target.system_save_data_id != 0) {
            game_name = "System";
        } else {
            game_name = "Unknown";
        }
    }

    std::string type_acc;
    if (target.save_data_type == FsSaveDataType_Account) {
        std::string nickname;
        for (const auto& acc : accounts) {
            if (!std::memcmp(&target.uid, &acc.uid, sizeof(AccountUid))) {
                nickname = acc.nickname;
                break;
            }
        }
        type_acc = nickname.empty() ? "Account" : ("Account: " + nickname);
    } else {
        type_acc = GetSaveTypeLabel(target.save_data_type);
    }

    char id_str[96];
    std::snprintf(id_str, sizeof(id_str), " [idx:%u rk:%u sp:%u %016lX]",
        target.save_data_index, target.save_data_rank, target.save_data_space_id, target.save_data_id);

    return game_name + " (" + type_acc + ")" + id_str;
}

} // namespace

auto Menu::FindLiveRestoreCandidates(const Entry& backup, const AccountUid* explicit_uid) -> std::vector<Entry> {
    std::vector<FsSaveDataInfo> infos;
    if (backup.save_data_type == FsSaveDataType_Account) {
        if (explicit_uid) {
            infos = DiscoverSaveDataInfo(explicit_uid, FsSaveDataType_Account);
        } else if (backup.uid.uid[0] != 0 || backup.uid.uid[1] != 0) {
            infos = DiscoverSaveDataInfo(&backup.uid, FsSaveDataType_Account);
        } else {
            infos = DiscoverSaveDataInfo(nullptr, FsSaveDataType_Account);
        }
    } else if (backup.save_data_type != 0xFF) {
        infos = DiscoverSaveDataInfo(nullptr, backup.save_data_type);
    } else {
        infos = DiscoverSaveDataInfo(nullptr, std::nullopt);
    }

    std::vector<Entry> candidates;
    std::set<std::string> seen_keys;

    for (const auto& info : infos) {
        const bool id_match = IsSystemLikeSave(info.save_data_type)
            ? (backup.system_save_data_id != 0 && info.system_save_data_id == backup.system_save_data_id)
            : (backup.application_id != 0 && info.application_id == backup.application_id);
        if (!id_match) {
            continue;
        }

        if (backup.save_data_type != 0xFF && info.save_data_type != backup.save_data_type) {
            continue;
        }

        if (info.save_data_type == FsSaveDataType_Account) {
            if (explicit_uid && std::memcmp(&info.uid, explicit_uid, sizeof(AccountUid)) != 0) {
                continue;
            }
        }

        const auto key = SaveEntryKey(info);
        if (!seen_keys.insert(key).second) {
            continue;
        }

        Entry target{};
        static_cast<FsSaveDataInfo&>(target) = info;
        target.is_backup = false;
        std::memcpy(&target.lang, &backup.lang, sizeof(target.lang));
        target.image = backup.image;
        target.status = backup.status;
        candidates.emplace_back(std::move(target));
    }

    return candidates;
}

void Menu::ResolveRestoreTarget(const Entry& backup, const AccountUid* explicit_uid, std::function<void(std::optional<Entry>)> cb) {
    if (!backup.is_backup && backup.save_data_id != 0 && (!explicit_uid || std::memcmp(explicit_uid, &backup.uid, sizeof(AccountUid)) == 0)) {
        Entry target = backup;
        target.is_backup = false;
        cb(target);
        return;
    }

    const auto candidates = FindLiveRestoreCandidates(backup, explicit_uid);

    if (candidates.empty()) {
        if (backup.save_data_type != FsSaveDataType_Account) {
            App::Push<OptionBox>("Save slot creation is only supported for Account saves."_i18n, "OK"_i18n);
        } else if (backup.save_data_rank != FsSaveDataRank_Primary || backup.save_data_index != 0) {
            App::Push<OptionBox>("Save slot creation is only supported for primary save slots."_i18n, "OK"_i18n);
        } else if (backup.application_id == 0) {
            App::Push<OptionBox>("Save slot creation is only supported for installed titles."_i18n, "OK"_i18n);
        } else {
            App::Push<OptionBox>("No compatible live save slot found on console."_i18n, "OK"_i18n);
        }
        cb(std::nullopt);
        return;
    }

    if (candidates.size() == 1) {
        const auto accounts = App::GetAccountList();
        const auto label = FormatTargetSlotLabel(candidates.front(), accounts);
        App::Push<OptionBox>("Restore save data to\n" + label + "?", "No"_i18n, "Yes"_i18n, 0, [candidates, cb = std::move(cb)](auto choice) {
            if (!choice || *choice != 1) {
                cb(std::nullopt);
                return;
            }
            cb(candidates.front());
        });
        return;
    }

    const auto accounts = App::GetAccountList();
    PopupList::Items items;
    for (const auto& c : candidates) {
        items.emplace_back(FormatTargetSlotLabel(c, accounts));
    }

    auto popup = std::make_unique<PopupList>("Select restore target slot"_i18n, items, [candidates, cb = std::move(cb)](auto op_index) {
        if (!op_index || *op_index >= static_cast<s64>(candidates.size())) {
            cb(std::nullopt);
            return;
        }
        cb(candidates[*op_index]);
    });
    App::Push(std::move(popup));
}

} // namespace sphaira::ui::menu::save
