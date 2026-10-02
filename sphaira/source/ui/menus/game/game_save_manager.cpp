#include "ui/menus/game/game_save_manager.hpp"
#include "ui/menus/save/save_slot_backend.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

namespace sphaira::ui::menu::game {

using grid::FormatBytes;

auto GetSaveSpaceLabel(u8 space_id) -> std::string {
    switch (static_cast<FsSaveDataSpaceId>(space_id)) {
        case FsSaveDataSpaceId_System: return "System"_i18n;
        case FsSaveDataSpaceId_User: return "User"_i18n;
        case FsSaveDataSpaceId_SdSystem: return "SD System"_i18n;
        case FsSaveDataSpaceId_Temporary: return "Temporary"_i18n;
        case FsSaveDataSpaceId_SdUser: return "SD User"_i18n;
        case FsSaveDataSpaceId_ProperSystem: return "Proper System"_i18n;
        case FsSaveDataSpaceId_SafeMode: return "Safe Mode"_i18n;
        default: return "Unknown"_i18n;
    }
}

auto FormatSaveInfoMessage(const GameSaveRow& row) -> std::string {
    std::string msg;
    msg += "User: "_i18n + row.account + "\n";
    msg += "Space: "_i18n + GetSaveSpaceLabel(row.info.save_data_space_id) + "\n";
    msg += "Type: "_i18n + i18n::get(save::GetSaveTypeLabel(row.info.save_data_type)) + "\n";
    msg += "Rank: "_i18n + (row.info.save_data_rank == FsSaveDataRank_Primary ? "Primary"_i18n : "Secondary"_i18n) + "\n";
    msg += "Index: "_i18n + std::to_string(row.info.save_data_index) + "\n";
    char id_buf[32];
    std::snprintf(id_buf, sizeof(id_buf), "%016lX", row.info.save_data_id);
    msg += "Save ID: "_i18n + std::string(id_buf) + "\n";
    msg += "Allocated: "_i18n + FormatBytes(row.info.size) + "\n";
    if (row.has_extra) {
        msg += "Data size: "_i18n + FormatBytes(row.extra.data_size) + "\n";
        msg += "Journal size: "_i18n + FormatBytes(row.extra.journal_size);
    } else {
        char err_buf[64];
        std::snprintf(err_buf, sizeof(err_buf), ("Extra data unreadable (0x%X)"_i18n).c_str(), row.extra_rc);
        msg += err_buf;
    }
    return msg;
}

void LoadGameSaves(u64 app_id, std::vector<GameSaveRow>& out_saves, u64& out_allocated_size) {
    out_saves.clear();
    out_allocated_size = 0;

    const auto all_saves = save::DiscoverSaveDataInfo(nullptr, std::nullopt);
    const auto accounts = App::GetAccountList();

    for (const auto& save_info : all_saves) {
        if (save_info.application_id != app_id) continue;

        if (std::ranges::any_of(out_saves, [&save_info](const auto& row) {
            return row.info.save_data_id == save_info.save_data_id &&
                   row.info.save_data_space_id == save_info.save_data_space_id;
        })) {
            continue;
        }

        GameSaveRow row{};
        row.info = save_info;
        row.account = i18n::get(save::GetSaveTypeLabel(save_info.save_data_type));
        if (save_info.save_data_type == FsSaveDataType_Account) {
            const auto account = std::ranges::find_if(accounts, [&save_info](const auto& candidate) {
                return !std::memcmp(&candidate.uid, &save_info.uid, sizeof(save_info.uid));
            });
            if (account != accounts.end()) {
                row.account = account->nickname;
            }
        }

        FsSaveDataExtraData extra{};
        const auto extra_rc = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
            &extra, sizeof(extra), static_cast<FsSaveDataSpaceId>(save_info.save_data_space_id), save_info.save_data_id);
        row.extra = extra;
        row.extra_rc = extra_rc;
        row.has_extra = R_SUCCEEDED(extra_rc);

        out_allocated_size += save_info.size;
        out_saves.emplace_back(std::move(row));
    }
}

void PromptCreateSaveSlot(
    u64 app_id,
    const std::string& game_name,
    const std::vector<GameSaveRow>& current_saves,
    std::function<void()> on_refresh
) {
    const auto accounts = App::GetAccountList();
    if (accounts.empty()) {
        App::Notify("No local users found on console."_i18n);
        return;
    }

    PopupList::Items user_items;
    for (const auto& acc : accounts) {
        user_items.emplace_back(acc.nickname);
    }

    App::Push<PopupList>("Select user to create save for"_i18n, user_items, [=](auto user_idx) {
        if (!user_idx) return;
        const auto& selected_account = accounts[*user_idx];
        const auto selected_uid = selected_account.uid;
        const auto user_nickname = selected_account.nickname;

        const bool duplicate = std::ranges::any_of(current_saves, [&](const auto& r) {
            return r.info.save_data_space_id == FsSaveDataSpaceId_User &&
                   r.info.save_data_type == FsSaveDataType_Account &&
                   r.info.application_id == app_id &&
                   r.info.save_data_rank == FsSaveDataRank_Primary &&
                   r.info.save_data_index == 0 &&
                   !std::memcmp(&r.info.uid, &selected_uid, sizeof(AccountUid));
        });
        if (duplicate) {
            App::Notify("A save slot for this user already exists."_i18n);
            return;
        }

        save::SaveCreationRequest planned_req{};
        save::SaveBackendStatus plan_status = save::SaveBackendStatus::Success;
        const auto plan_rc = save::PlanAccountSaveCreation(app_id, selected_uid, nullptr, planned_req, &plan_status);
        if (R_FAILED(plan_rc) || plan_status != save::SaveBackendStatus::Success) {
            App::PushErrorBox(plan_rc, save::GetBackendStatusMessage(plan_status));
            return;
        }

        if (planned_req.attr.save_data_type != FsSaveDataType_Account ||
            planned_req.space_id != FsSaveDataSpaceId_User ||
            planned_req.attr.save_data_rank != FsSaveDataRank_Primary ||
            planned_req.attr.save_data_index != 0) {
            App::Notify("Save slot creation contract unsupported."_i18n);
            return;
        }

        struct Preset {
            std::string label;
            s64 data_size;
        };
        std::vector<Preset> presets;

        char buf[128];
        std::snprintf(buf, sizeof(buf), ("Default (%s)"_i18n).c_str(), FormatBytes(planned_req.data_size).c_str());
        presets.push_back({buf, planned_req.data_size});

        constexpr s64 kDelta16 = 16 * 1024 * 1024;
        if (planned_req.data_size <= std::numeric_limits<s64>::max() - kDelta16) {
            const s64 sz16 = planned_req.data_size + kDelta16;
            if (sz16 % 0x4000 == 0 && sz16 >= planned_req.data_size) {
                std::snprintf(buf, sizeof(buf), ("+16 MiB (%s)"_i18n).c_str(), FormatBytes(sz16).c_str());
                presets.push_back({buf, sz16});
            }
        }

        constexpr s64 kDelta64 = 64 * 1024 * 1024;
        if (planned_req.data_size <= std::numeric_limits<s64>::max() - kDelta64) {
            const s64 sz64 = planned_req.data_size + kDelta64;
            if (sz64 % 0x4000 == 0 && sz64 >= planned_req.data_size) {
                std::snprintf(buf, sizeof(buf), ("+64 MiB (%s)"_i18n).c_str(), FormatBytes(sz64).c_str());
                presets.push_back({buf, sz64});
            }
        }

        PopupList::Items preset_items;
        for (const auto& p : presets) {
            preset_items.emplace_back(p.label);
        }

        App::Push<PopupList>("Select initial save size"_i18n, preset_items, [=](auto preset_idx) {
            if (!preset_idx) return;
            auto req = planned_req;
            req.data_size = presets[*preset_idx].data_size;

            std::string conf = "Create save slot?"_i18n + "\n\n";
            conf += "Game: "_i18n + game_name + "\n";
            conf += "User: "_i18n + user_nickname + "\n";
            conf += "Space: "_i18n + GetSaveSpaceLabel(req.space_id) + "\n";
            conf += "Type: "_i18n + i18n::get(save::GetSaveTypeLabel(req.attr.save_data_type)) + "\n";
            conf += "Rank: "_i18n + (req.attr.save_data_rank == FsSaveDataRank_Primary ? "Primary"_i18n : "Secondary"_i18n) + "\n";
            conf += "Index: "_i18n + std::to_string(req.attr.save_data_index) + "\n";
            conf += "Data size: "_i18n + FormatBytes(req.data_size) + "\n";
            conf += "Journal size: "_i18n + FormatBytes(req.journal_size);

            App::Push<OptionBox>(conf, "Back"_i18n, "Create"_i18n, 1, [=](auto op_index) {
                if (!op_index || !*op_index) return;

                struct WorkerResult {
                    save::SaveCreationResult result{};
                };
                auto wr = std::make_shared<WorkerResult>();

                App::Push<ProgressBox>(
                    0,
                    "Creating save slot..."_i18n,
                    game_name,
                    [req, wr](auto pbox) -> Result {
                        wr->result = save::CreateSaveDataChecked(req, [pbox]() {
                            return pbox && R_FAILED(pbox->ShouldExitResult());
                        });
                        if (!wr->result.verified) {
                            return R_FAILED(wr->result.rc) ? wr->result.rc : static_cast<Result>(FsError_PathNotFound);
                        }
                        return 0;
                    },
                    [=](Result rc) {
                        const auto& res = wr->result;
                        if (res.verified) {
                            if (on_refresh) on_refresh();
                            std::string msg = "Save slot created successfully."_i18n + "\n";
                            msg += "Data size: "_i18n + FormatBytes(res.verified_extra.data_size) + "\n";
                            msg += "Journal size: "_i18n + FormatBytes(res.verified_extra.journal_size);
                            App::Notify(msg);
                        } else if (res.ipc_executed) {
                            if (on_refresh) on_refresh();
                            const auto err_msg = save::GetBackendStatusMessage(res.status);
                            App::PushErrorBox(res.rc, err_msg);
                        } else {
                            if (rc != Result_TransferCancelled && res.status != save::SaveBackendStatus::Cancelled) {
                                const auto err_msg = save::GetBackendStatusMessage(res.status);
                                App::PushErrorBox(res.rc ? res.rc : rc, err_msg);
                            }
                        }
                    }
                );
            });
        });
    });
}

void PromptIncreaseSaveSize(
    const std::string& game_name,
    const GameSaveRow& row,
    std::function<void()> on_refresh
) {
    if (!row.has_extra) {
        App::PushErrorBox(row.extra_rc, "Cannot increase save size: extra data unreadable."_i18n);
        return;
    }

    const s64 cur_data = row.extra.data_size;
    const s64 cur_journal = row.extra.journal_size;

    if (cur_data <= 0 || (cur_data % 0x4000 != 0) ||
        cur_journal < 0 || (cur_journal % 0x4000 != 0)) {
        App::Notify("Save slot sizing is invalid or unaligned."_i18n);
        return;
    }

    struct GrowPreset {
        std::string label;
        s64 target_data;
    };
    std::vector<GrowPreset> presets;

    char buf[128];
    constexpr s64 kDelta16 = 16 * 1024 * 1024;
    if (cur_data <= std::numeric_limits<s64>::max() - kDelta16) {
        const s64 p16 = cur_data + kDelta16;
        if (p16 % 0x4000 == 0 && p16 > cur_data) {
            std::snprintf(buf, sizeof(buf), ("+16 MiB (%s)"_i18n).c_str(), FormatBytes(p16).c_str());
            presets.push_back({buf, p16});
        }
    }

    constexpr s64 kDelta64 = 64 * 1024 * 1024;
    if (cur_data <= std::numeric_limits<s64>::max() - kDelta64) {
        const s64 p64 = cur_data + kDelta64;
        if (p64 % 0x4000 == 0 && p64 > cur_data) {
            std::snprintf(buf, sizeof(buf), ("+64 MiB (%s)"_i18n).c_str(), FormatBytes(p64).c_str());
            presets.push_back({buf, p64});
        }
    }

    if (presets.empty()) {
        App::Notify("Preset size cannot be represented safely."_i18n);
        return;
    }

    PopupList::Items items;
    for (const auto& p : presets) {
        items.emplace_back(p.label);
    }

    App::Push<PopupList>("Select new save size"_i18n, items, [=](auto opt_idx) {
        if (!opt_idx) return;
        const s64 new_data = presets[*opt_idx].target_data;
        const s64 new_journal = cur_journal;

        if (new_data <= cur_data) {
            App::Notify("Save size increase would not change size."_i18n);
            return;
        }

        std::string conf = "Increase save size?"_i18n + "\n\n";
        conf += "Game: "_i18n + game_name + "\n";
        conf += "User: "_i18n + row.account + "\n";
        conf += "Space: "_i18n + GetSaveSpaceLabel(row.info.save_data_space_id) + "\n";
        conf += "Type: "_i18n + i18n::get(save::GetSaveTypeLabel(row.info.save_data_type)) + "\n";
        conf += "Rank: "_i18n + (row.info.save_data_rank == FsSaveDataRank_Primary ? "Primary"_i18n : "Secondary"_i18n) + "\n";
        conf += "Index: "_i18n + std::to_string(row.info.save_data_index) + "\n";
        conf += "Current data size: "_i18n + FormatBytes(cur_data) + "\n";
        conf += "New data size: "_i18n + FormatBytes(new_data) + "\n";
        conf += "Current journal size: "_i18n + FormatBytes(cur_journal) + "\n";
        conf += "New journal size: "_i18n + FormatBytes(new_journal);

        App::Push<OptionBox>(conf, "Back"_i18n, "Increase"_i18n, 1, [=](auto op_index) {
            if (!op_index || !*op_index) return;

            save::SaveGrowRequest grow_req{};
            grow_req.target_info = row.info;
            grow_req.space_id = static_cast<FsSaveDataSpaceId>(row.info.save_data_space_id);
            grow_req.requested_data_size = new_data;
            grow_req.requested_journal_size = new_journal;

            struct GrowWorkerResult {
                save::SaveGrowResult result{};
            };
            auto wr = std::make_shared<GrowWorkerResult>();

            App::Push<ProgressBox>(
                0,
                "Increasing save size..."_i18n,
                game_name,
                [grow_req, wr](auto pbox) -> Result {
                    wr->result = save::ExtendSaveDataChecked(grow_req, [pbox]() {
                        return pbox && R_FAILED(pbox->ShouldExitResult());
                    });
                    if (!wr->result.verified) {
                        return R_FAILED(wr->result.rc) ? wr->result.rc : static_cast<Result>(FsError_PathNotFound);
                    }
                    return 0;
                },
                [=](Result rc) {
                    const auto& res = wr->result;
                    if (res.verified) {
                        if (on_refresh) on_refresh();
                        std::string msg = "Save size increased successfully."_i18n + "\n";
                        msg += "Data size: "_i18n + FormatBytes(res.actual_data_size) + "\n";
                        msg += "Journal size: "_i18n + FormatBytes(res.actual_journal_size);
                        App::Notify(msg);
                    } else if (res.ipc_executed) {
                        if (on_refresh) on_refresh();
                        const auto err_msg = save::GetBackendStatusMessage(res.status);
                        App::PushErrorBox(res.rc, err_msg);
                    } else {
                        if (rc != Result_TransferCancelled && res.status != save::SaveBackendStatus::Cancelled) {
                            const auto err_msg = save::GetBackendStatusMessage(res.status);
                            App::PushErrorBox(res.rc ? res.rc : rc, err_msg);
                        }
                    }
                }
            );
        });
    });
}

} // namespace sphaira::ui::menu::game
