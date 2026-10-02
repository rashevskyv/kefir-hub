#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "dumper.hpp"
#include "title_info.hpp"

#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"

#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"

#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {

void Menu::PromptSaveAction() {
    if (m_entries.empty()) {
        return;
    }

    const auto seeds = GetSelectedEntries();
    if (seeds.empty()) {
        return;
    }

    bool has_live = false;
    bool has_backup = false;
    for (const auto& s : seeds) {
        if (s.is_backup) {
            has_backup = true;
        } else {
            has_live = true;
        }
    }

    if (has_live && has_backup) {
        App::Push<OptionBox>("Please select only live saves or only backups."_i18n, "OK"_i18n);
        return;
    }

    if (has_live) {
        PromptLiveSaveAction(seeds);
    } else {
        if (seeds.size() == 1 && seeds.front().is_game_parent) {
            OpenGameBackupGroup(seeds.front());
            return;
        }
        PromptBackupGroupAction(seeds);
    }
}

void Menu::PromptLiveSaveAction(const std::vector<Entry>& seeds) {
    std::set<u64> selected_apps;
    std::set<u64> selected_sys;
    for (const auto& s : seeds) {
        if (!s.is_backup) {
            if (IsSystemLikeSave(s.save_data_type)) {
                selected_sys.insert(s.system_save_data_id);
            } else if (s.application_id != 0) {
                selected_apps.insert(s.application_id);
            }
        }
    }
    if (selected_apps.empty() && selected_sys.empty() && m_index < m_entries.size() && !m_entries[m_index].is_backup) {
        const auto& cur = m_entries[m_index];
        if (IsSystemLikeSave(cur.save_data_type)) {
            selected_sys.insert(cur.system_save_data_id);
        } else if (cur.application_id != 0) {
            selected_apps.insert(cur.application_id);
        }
    }

    PopupList::Items items;
    items.emplace_back("Create backup"_i18n);
    items.emplace_back("Create backup if newer"_i18n);
    items.emplace_back("Restore"_i18n);
    items.emplace_back("Open in file browser"_i18n);
    items.emplace_back("Delete"_i18n);
    const std::string select_game_label = ((selected_apps.size() + selected_sys.size()) > 1)
        ? "Select all saves for selected games"_i18n
        : "Select all saves for this game"_i18n;
    items.emplace_back(select_game_label);

    auto popup = std::make_unique<PopupList>("Save Action"_i18n, items, [this, seeds, selected_apps, selected_sys](auto op_index) {
        if (!op_index) {
            return;
        }

        switch (*op_index) {
            case 0:
                PromptSaveTypeOptions(SaveOp::Backup);
                break;

            case 1:
                CreateBackupIfNewer(seeds);
                break;

            case 2:
                PromptSaveTypeOptions(SaveOp::Restore);
                break;

            case 3:
                App::Push<OptionBox>("Live save filesystem browsing is not currently supported."_i18n, "OK"_i18n);
                break;

            case 4:
                PromptSaveTypeOptions(SaveOp::Delete);
                break;

            case 5: {
                for (auto& e : m_entries) {
                    if (!e.is_backup) {
                        const bool match = IsSystemLikeSave(e.save_data_type)
                            ? selected_sys.contains(e.system_save_data_id)
                            : selected_apps.contains(e.application_id);
                        if (match && !e.selected) {
                            e.selected = true;
                            m_selected_count++;
                        }
                    }
                }
                break;
            }

            default:
                break;
        }
    });
    popup->SetMenuStyle(true);
    App::Push(std::move(popup));
}

void Menu::CreateBackupIfNewer(const std::vector<Entry>& seeds) {
    auto to_backup_count = std::make_shared<size_t>(0);
    auto up_to_date_count = std::make_shared<size_t>(0);

    App::Push<ProgressBox>(0, "Create backup if newer"_i18n, "", [this, seeds, to_backup_count, up_to_date_count](auto pbox) -> Result {
        fs::FsNativeSd sd_fs;
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

        std::vector<Entry> to_backup;

        for (size_t i = 0; i < seeds.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            const auto& e = seeds[i];
            pbox->SetTitle(e.GetName());
            pbox->UpdateTransfer(i + 1, seeds.size());

            const auto archives = CollectGroupArchives(&sd_fs, e, backup_root);
            if (archives.empty()) {
                to_backup.emplace_back(e);
                continue;
            }

            const auto& newest = archives.front();
            const auto slash = std::strrchr(newest.path.s, '/');
            BackupArchiveInfo binfo{};
            if (!InspectBackupArchive(&sd_fs, newest.path, slash ? slash + 1 : newest.path.s, "", binfo)) {
                to_backup.emplace_back(e);
                continue;
            }

            const auto space_id = static_cast<FsSaveDataSpaceId>(e.save_data_space_id);

            FsSaveDataExtraData live_extra{};
            const auto rc = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&live_extra, sizeof(live_extra), space_id, e.save_data_id);
            if (R_FAILED(rc)) {
                to_backup.emplace_back(e);
                continue;
            }

            const bool is_up_to_date =
                live_extra.timestamp != 0 &&
                binfo.source_timestamp != 0 &&
                live_extra.commit_id != 0 &&
                binfo.commit_id != 0 &&
                live_extra.timestamp == binfo.source_timestamp &&
                live_extra.commit_id == binfo.commit_id;

            if (is_up_to_date) {
                (*up_to_date_count)++;
            } else {
                to_backup.emplace_back(e);
            }
        }

        *to_backup_count = to_backup.size();

        if (to_backup.empty()) {
            R_SUCCEED();
        }

        const auto location = MakeSdCardDumpLocation();
        for (size_t i = 0; i < to_backup.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            auto& e = to_backup[i];
            detail::LoadControlEntry(e);
            pbox->SetTitle(e.GetName());
            if (e.image) {
                pbox->SetImage(e.image);
            } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
                pbox->SetImageDataConst(data->icon);
            } else {
                pbox->SetImage(0);
            }
            pbox->UpdateTransfer(i + 1, to_backup.size());
            R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root));
        }

        R_SUCCEED();
    }, [this, to_backup_count, up_to_date_count](Result rc) {
        if (R_SUCCEEDED(rc)) {
            if (*to_backup_count == 0 && *up_to_date_count > 0) {
                App::Push<OptionBox>("All selected saves are already up to date."_i18n, "OK"_i18n);
            } else if (*to_backup_count > 0 && *up_to_date_count > 0) {
                const std::string msg = std::to_string(*to_backup_count) + " " + "backup(s) created, "_i18n +
                    std::to_string(*up_to_date_count) + " " + "already up to date."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
            } else {
                App::Notify("Backup successful!"_i18n);
            }
        } else {
            App::PushErrorBox(rc, "Backup failed!"_i18n);
        }
        ClearSelection();
        ScanHomebrew();
    });
}

void Menu::PromptBackupGroupAction(const std::vector<Entry>& seeds) {
    if (seeds.empty()) {
        return;
    }
    const auto& focused = seeds.front();

    enum class ActionType {
        VerifyIntegrity,
        DeleteOlder,
        Restore,
        OpenFileBrowser,
        Delete,
        SelectUser,
        SelectGame,
    };

    struct ActionItem {
        ActionType type;
        std::string label;
    };

    std::vector<ActionItem> actions;
    actions.push_back({ActionType::Restore, "Restore"_i18n});

    actions.push_back({ActionType::VerifyIntegrity, "Verify integrity"_i18n});
    actions.push_back({ActionType::DeleteOlder, "Delete older backups"_i18n});
    actions.push_back({ActionType::OpenFileBrowser, "Open in file browser"_i18n});

    const bool has_user_select = (m_category != Category::Backups) &&
        focused.is_backup &&
        focused.save_data_type == FsSaveDataType_Account &&
        (focused.uid.uid[0] != 0 || focused.uid.uid[1] != 0);
    if (has_user_select) {
        actions.push_back({ActionType::SelectUser, "Select all backups for this user"_i18n});
    }

    actions.push_back({ActionType::SelectGame, "Select all backups for this game"_i18n});
    actions.push_back({ActionType::Delete, "Delete"_i18n});

    PopupList::Items items;
    for (const auto& a : actions) {
        items.emplace_back(a.label);
    }

    auto popup = std::make_unique<PopupList>("Backup Action"_i18n, items, [this, seeds, focused, actions](auto op_index) {
        if (!op_index || *op_index >= static_cast<s64>(actions.size())) {
            return;
        }

        const auto actual_seeds = ExpandGameGroups(seeds);
        switch (actions[*op_index].type) {
            case ActionType::VerifyIntegrity:
                VerifyIntegrity(actual_seeds);
                break;

            case ActionType::DeleteOlder:
                DeleteOlderBackups(actual_seeds);
                break;

            case ActionType::Restore:
                RestoreBackupGroups(actual_seeds, false, true);
                break;

            case ActionType::OpenFileBrowser: {
                const auto& target = actual_seeds.empty() ? seeds.front() : actual_seeds.front();
                const auto slash = std::strrchr(target.backup_path.s, '/');
                if (slash) {
                    std::string dir(target.backup_path.s, slash - target.backup_path.s);
                    if (dir.empty()) dir = "/";
                    const filebrowser::FsEntry sd{"microSD card", "/", filebrowser::FsType::Sd};
                    App::Push<filebrowser::Menu>(MenuFlag_None, sd, dir.c_str());
                }
                break;
            }

            case ActionType::Delete:
                DeleteBackupGroups(actual_seeds);
                break;

            case ActionType::SelectUser: {
                for (auto& e : m_entries) {
                    if (e.is_backup && e.save_data_type == FsSaveDataType_Account &&
                        !std::memcmp(&e.uid, &focused.uid, sizeof(AccountUid))) {
                        if (!e.selected) {
                            e.selected = true;
                            m_selected_count++;
                        }
                    }
                }
                break;
            }

            case ActionType::SelectGame: {
                const bool is_sys = IsSystemLikeSave(focused.save_data_type);
                for (auto& e : m_entries) {
                    if (e.is_backup) {
                        const bool match = is_sys
                            ? (IsSystemLikeSave(e.save_data_type) && e.system_save_data_id == focused.system_save_data_id)
                            : (!IsSystemLikeSave(e.save_data_type) && e.application_id == focused.application_id);
                        if (match && !e.selected) {
                            e.selected = true;
                            m_selected_count++;
                        }
                    }
                }
                break;
            }
        }
    });

    if (m_category == Category::Backups && seeds.size() == 1 && !seeds.front().is_game_parent) {
        const auto& first = seeds.front();
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
    }

    popup->SetMenuStyle(true);
    App::Push(std::move(popup));
}

void Menu::VerifyIntegrity(const std::vector<Entry>& seeds) {
    auto valid_count = std::make_shared<size_t>(0);
    auto invalid_count = std::make_shared<size_t>(0);
    auto unsupported_raw_count = std::make_shared<size_t>(0);
    auto failed_names = std::make_shared<std::vector<std::string>>();

    App::Push<ProgressBox>(0, "Verify integrity"_i18n, "", [this, seeds, valid_count, invalid_count, unsupported_raw_count, failed_names](auto pbox) -> Result {
        fs::FsNativeSd sd_fs;
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

        std::vector<fs::FsPath> all_archives;
        std::set<std::string> seen;
        for (const auto& g : seeds) {
            const auto archives = CollectGroupArchives(&sd_fs, g, backup_root);
            for (const auto& a : archives) {
                if (seen.insert(a.path.toString()).second) {
                    all_archives.emplace_back(a.path);
                }
            }
        }

        for (size_t i = 0; i < all_archives.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            const auto& archive_path = all_archives[i];
            const auto slash = std::strrchr(archive_path.s, '/');
            const std::string name = slash ? (slash + 1) : archive_path.s;

            pbox->SetTitle(name);
            pbox->UpdateTransfer(i + 1, all_archives.size());

            if (std::string_view{archive_path.s}.ends_with(".zip")) {
                if (VerifyZipIntegrity(archive_path)) {
                    (*valid_count)++;
                } else {
                    (*invalid_count)++;
                    failed_names->emplace_back(name);
                }
            } else if (IsRawSaveCandidate(&sd_fs, archive_path, name)) {
                (*unsupported_raw_count)++;
            } else {
                (*invalid_count)++;
                failed_names->emplace_back(name);
            }
        }

        R_SUCCEED();
    }, [valid_count, invalid_count, unsupported_raw_count, failed_names](Result rc) {
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Integrity verification failed!"_i18n);
            return;
        }

        if (*unsupported_raw_count == 0) {
            if (*invalid_count == 0) {
                const std::string msg = "Integrity verified: all "_i18n + std::to_string(*valid_count) + " archive(s) are valid."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
            } else {
                std::string msg = "Integrity check failed: "_i18n + std::to_string(*invalid_count) + " corrupt archive(s) found:\n"_i18n;
                for (size_t i = 0; i < std::min<size_t>(5, failed_names->size()); i++) {
                    msg += "• " + (*failed_names)[i] + "\n";
                }
                if (failed_names->size() > 5) {
                    msg += "...and " + std::to_string(failed_names->size() - 5) + " more.";
                }
                App::Push<OptionBox>(msg, "OK"_i18n);
            }
        } else {
            std::string msg;
            if (*valid_count > 0 || *invalid_count > 0) {
                if (*invalid_count == 0) {
                    msg = "ZIP archives verified: "_i18n + std::to_string(*valid_count) + " valid.\n"_i18n;
                } else {
                    msg = "Integrity check failed: "_i18n + std::to_string(*invalid_count) + " corrupt archive(s) found:\n"_i18n;
                    for (size_t i = 0; i < std::min<size_t>(5, failed_names->size()); i++) {
                        msg += "• " + (*failed_names)[i] + "\n";
                    }
                    if (failed_names->size() > 5) {
                        msg += "...and " + std::to_string(failed_names->size() - 5) + " more.\n";
                    }
                    if (*valid_count > 0) {
                        msg += std::to_string(*valid_count) + " valid ZIP archive(s).\n"_i18n;
                    }
                }
            }
            msg += std::to_string(*unsupported_raw_count) + " RAW save container(s) are unsupported for verification."_i18n;
            App::Push<OptionBox>(msg, "OK"_i18n);
        }
    });
}

void Menu::DeleteOlderBackups(const std::vector<Entry>& seeds) {
    fs::FsNativeSd sd_fs;
    const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

    struct PruneGroup {
        Entry group;
        std::vector<BackupCandidate> older;
    };
    std::vector<PruneGroup> to_prune;
    size_t total_older = 0;

    for (const auto& g : seeds) {
        const auto archives = CollectGroupArchives(&sd_fs, g, backup_root);
        if (archives.size() > 1) {
            std::vector<BackupCandidate> older(archives.begin() + 1, archives.end());
            total_older += older.size();
            to_prune.emplace_back(PruneGroup{g, std::move(older)});
        }
    }

    if (total_older == 0) {
        App::Notify("No older backups to delete."_i18n);
        return;
    }

    const std::string prompt = "Delete "_i18n + std::to_string(total_older) +
        " older backup archive(s)? The newest backup for each group will be kept."_i18n;

    App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [this, to_prune](auto choice) {
        if (choice && *choice == 1) {
            App::PopToMenu();
            auto deleted_count = std::make_shared<size_t>(0);
            auto failed_count = std::make_shared<size_t>(0);
            auto first_error = std::make_shared<Result>(0);

            App::Push<ProgressBox>(0, "Delete older backups"_i18n, "", [to_prune, deleted_count, failed_count, first_error](auto pbox) -> Result {
                fs::FsNativeSd sd_fs;
                for (const auto& item : to_prune) {
                    for (const auto& cand : item.older) {
                        const auto rc = sd_fs.DeleteFile(cand.path);
                        if (R_SUCCEEDED(rc)) {
                            (*deleted_count)++;
                            const auto slash = std::strrchr(cand.path.s, '/');
                            if (slash) {
                                std::string dir(cand.path.s, slash - cand.path.s);
                                sd_fs.DeleteDirectory(dir.c_str());
                            }
                        } else {
                            (*failed_count)++;
                            if (*first_error == 0) {
                                *first_error = rc;
                            }
                        }
                    }
                }
                return *first_error;
            }, [this, deleted_count, failed_count](Result rc) {
                if (*failed_count == 0 && *deleted_count > 0) {
                    App::Notify("Delete successful!"_i18n);
                } else if (*deleted_count > 0 && *failed_count > 0) {
                    const std::string msg = std::to_string(*deleted_count) + " " + "deleted, "_i18n +
                        std::to_string(*failed_count) + " " + "failed."_i18n;
                    App::PushErrorBox(rc, msg);
                } else {
                    App::PushErrorBox(rc, "Delete failed!"_i18n);
                }
                ClearSelection();
                ScanHomebrew();
            });
        }
    }, seeds.front().image);
}

void Menu::DeleteBackupGroups(const std::vector<Entry>& groups) {
    const auto prompt = groups.size() == 1
        ? "Are you sure you want to delete all backups for "_i18n + groups.front().GetName() + "?"
        : "Are you sure you want to delete all backups for the selected games?"_i18n;

    App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [this, groups](auto choice) {
        if (choice && *choice == 1) {
            App::PopToMenu();
            auto deleted_count = std::make_shared<size_t>(0);
            auto failed_count = std::make_shared<size_t>(0);
            auto first_error = std::make_shared<Result>(0);

            App::Push<ProgressBox>(0, "Delete backups"_i18n, "", [this, groups, deleted_count, failed_count, first_error](auto pbox) -> Result {
                fs::FsNativeSd sd_fs;
                const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};
                for (const auto& g : groups) {
                    auto archives = CollectGroupArchives(&sd_fs, g, backup_root);
                    for (const auto& a : archives) {
                        const auto rc = sd_fs.DeleteFile(a.path);
                        if (R_SUCCEEDED(rc)) {
                            (*deleted_count)++;
                            const auto slash = std::strrchr(a.path.s, '/');
                            if (slash) {
                                std::string dir(a.path.s, slash - a.path.s);
                                sd_fs.DeleteDirectory(dir.c_str());
                            }
                        } else {
                            (*failed_count)++;
                            if (*first_error == 0) {
                                *first_error = rc;
                            }
                        }
                    }
                }
                return *first_error;
            }, [this, deleted_count, failed_count](Result rc) {
                if (*failed_count == 0 && *deleted_count > 0) {
                    App::Notify("Delete successful!"_i18n);
                } else if (*deleted_count > 0 && *failed_count > 0) {
                    const std::string msg = std::to_string(*deleted_count) + " " + "deleted, "_i18n +
                        std::to_string(*failed_count) + " " + "failed."_i18n;
                    App::PushErrorBox(rc, msg);
                } else {
                    App::PushErrorBox(rc, "Delete failed!"_i18n);
                }
                ClearSelection();
                ScanHomebrew();
            });
        }
    }, groups.front().image);
}

} // namespace sphaira::ui::menu::save
