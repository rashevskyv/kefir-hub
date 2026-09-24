#include "ui/menus/users/users_manage_internal.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <string_view>

namespace sphaira::ui::menu::users {

void ManageBackupsMenu::PromptAction() {
    if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
        return;
    }

    const auto& cur = m_entries[m_index].pack;
    const bool is_cur_selected = m_entries[m_index].selected;

    enum ActionType {
        Action_Restore,
        Action_Duplicate,
        Action_Rename,
        Action_Delete,
        Action_Send,
        Action_ToggleSelect,
        Action_SelectAll,
        Action_ClearSelection,
        Action_InvertSelection,
    };

    struct ActionItem {
        ActionType type;
        std::string text;
        std::optional<ActionIcon> icon;
    };

    std::vector<ActionItem> actions;
    actions.push_back({Action_Restore, "Restore"_i18n, ActionIcon::Save});
    actions.push_back({Action_Duplicate, "Duplicate"_i18n, ActionIcon::Copy});
    actions.push_back({Action_Rename, "Rename"_i18n, ActionIcon::Edit});
    actions.push_back({Action_Delete, "Delete"_i18n, ActionIcon::Delete});
    actions.push_back({Action_Send, "Send to another console"_i18n, ActionIcon::Move});
    actions.push_back({Action_ToggleSelect, is_cur_selected ? "Deselect"_i18n : "Select"_i18n, ActionIcon::Toggle});
    actions.push_back({Action_SelectAll, "Select All"_i18n, ActionIcon::Range});
    if (m_selected_count > 0) {
        actions.push_back({Action_ClearSelection, "Clear selection"_i18n, ActionIcon::Undo});
    }
    actions.push_back({Action_InvertSelection, "Invert"_i18n, ActionIcon::Refresh});

    PopupList::Items items;
    std::vector<std::optional<ActionIcon>> icons;
    items.reserve(actions.size());
    icons.reserve(actions.size());
    for (const auto& a : actions) {
        items.push_back(a.text);
        icons.push_back(a.icon);
    }

    auto popup = std::make_unique<PopupList>(cur.nickname, items, [this, actions](auto op_index) {
        if (!op_index || *op_index >= actions.size()) {
            return;
        }
        switch (actions[*op_index].type) {
            case Action_Restore:
                RestoreSelected();
                break;
            case Action_Duplicate:
                DuplicateCurrent();
                break;
            case Action_Rename:
                RenameCurrent();
                break;
            case Action_Delete:
                ConfirmDeletePacks();
                break;
            case Action_Send:
                menu::StartConsoleTransferShareUserBackups();
                break;
            case Action_ToggleSelect:
                ToggleCurrentSelection(false);
                break;
            case Action_SelectAll:
                SelectAll();
                break;
            case Action_ClearSelection:
                ClearSelection();
                break;
            case Action_InvertSelection:
                InvertSelection();
                break;
        }
    });
    popup->SetMenuStyle(true);
    popup->SetIcons(std::move(icons));
    App::Push(std::move(popup));
}

void ManageBackupsMenu::RestoreSelected() {
    if (m_entries.empty()) {
        return;
    }
    std::vector<account_user::Pack> picked;
    if (m_selected_count > 0) {
        for (const auto& e : m_entries) {
            if (e.selected) {
                picked.push_back(e.pack);
            }
        }
    } else {
        picked.push_back(m_entries[m_index].pack);
    }
    if (picked.empty()) {
        return;
    }
    auto cb = m_on_restore;
    SetPop();
    if (cb) {
        cb(std::move(picked));
    }
}

void ManageBackupsMenu::DuplicateCurrent() {
    if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
        return;
    }
    DuplicatePack(m_entries[m_index].pack);
}

void ManageBackupsMenu::RenameCurrent() {
    if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
        return;
    }
    RenamePack(m_entries[m_index].pack);
}

void ManageBackupsMenu::DuplicatePack(const account_user::Pack& pack) {
    if (!pack.is_archive) {
        App::Push<OptionBox>("Duplicating legacy directory backups is not supported."_i18n, "OK"_i18n);
        return;
    }

    const auto src_path = pack.dir;
    fs::FsNativeSd sd;
    if (!sd.FileExists(src_path.c_str())) {
        App::Push<OptionBox>("Backup file does not exist."_i18n, "OK"_i18n);
        return;
    }

    std::string parent_dir = account_user::GetUserPacksRoot();
    if (const auto slash = src_path.find_last_of("/\\"); slash != std::string::npos) {
        parent_dir = src_path.substr(0, slash);
    }

    std::string name_stem = pack.folder_name;
    constexpr std::string_view zip_ext = ".kefir-user.zip";
    if (name_stem.size() >= zip_ext.size() && name_stem.ends_with(zip_ext)) {
        name_stem.resize(name_stem.size() - zip_ext.size());
    }

    std::string final_path = parent_dir + "/" + name_stem + "_copy" + std::string(zip_ext);
    int suffix = 1;
    while (sd.FileExists(final_path.c_str()) || sd.DirExists(final_path.c_str())) {
        final_path = parent_dir + "/" + name_stem + "_copy_" + std::to_string(suffix++) + std::string(zip_ext);
    }

    const std::string part_path = final_path + ".part";
    if (sd.FileExists(part_path.c_str())) {
        sd.DeleteFile(part_path.c_str());
    }

    App::Push<ProgressBox>(
        0,
        "Duplicate backup"_i18n,
        name_stem,
        [src_path, part_path, final_path](auto pbox) -> Result {
            pbox->NewTransfer("Duplicating backup..."_i18n);
            fs::FsNativeSd sd;
            bool success = false;
            bool renamed = false;
            ON_SCOPE_EXIT(
                if (!success) {
                    sd.DeleteFile(part_path.c_str());
                    if (renamed) {
                        sd.DeleteFile(final_path.c_str());
                    }
                }
            );

            auto rc = pbox->CopyFile(src_path, part_path);
            if (R_FAILED(rc)) {
                return rc;
            }
            if (pbox->ShouldExit()) {
                return Result_TransferCancelled;
            }

            rc = sd.RenameFile(part_path.c_str(), final_path.c_str());
            if (R_FAILED(rc)) {
                return rc;
            }
            renamed = true;

            const auto check_pack = account_user::FindUserPack(final_path);
            if (check_pack.dir.empty()) {
                return Result_FsInvalidType;
            }

            success = true;
            R_SUCCEED();
        },
        [this](Result rc) {
            if (rc == Result_TransferCancelled) {
                return;
            }
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("Could not duplicate the backup."_i18n, "OK"_i18n);
                return;
            }
            Refresh();
        },
        1, PRIO_PREEMPTIVE, 1024 * 128, false
    );
}

void ManageBackupsMenu::RenamePack(const account_user::Pack& pack) {
    if (!pack.is_archive) {
        App::Push<OptionBox>("Renaming legacy directory backups is not supported."_i18n, "OK"_i18n);
        return;
    }

    const auto src_path = pack.dir;
    fs::FsNativeSd sd;
    if (!sd.FileExists(src_path.c_str())) {
        App::Push<OptionBox>("Backup file does not exist."_i18n, "OK"_i18n);
        return;
    }

    std::string parent_dir = account_user::GetUserPacksRoot();
    if (const auto slash = src_path.find_last_of("/\\"); slash != std::string::npos) {
        parent_dir = src_path.substr(0, slash);
    }

    std::string name_stem = pack.folder_name;
    constexpr std::string_view zip_ext = ".kefir-user.zip";
    if (name_stem.size() >= zip_ext.size() && name_stem.ends_with(zip_ext)) {
        name_stem.resize(name_stem.size() - zip_ext.size());
    }

    std::string input;
    if (R_FAILED(swkbd::ShowText(input, "Rename backup"_i18n.c_str(), name_stem.c_str(), 1, 64)) || input.empty()) {
        return;
    }

    while (!input.empty() && (input.front() == ' ' || input.front() == '\t' || input.front() == '\r' || input.front() == '\n')) {
        input.erase(input.begin());
    }
    while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r' || input.back() == '\n')) {
        input.pop_back();
    }
    if (input.empty()) {
        return;
    }

    if (input.size() >= zip_ext.size() && input.ends_with(zip_ext)) {
        input.resize(input.size() - zip_ext.size());
    }

    for (auto& c : input) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }

    const std::string final_path = parent_dir + "/" + input + std::string(zip_ext);
    if (final_path == src_path) {
        return;
    }

    if (sd.FileExists(final_path.c_str()) || sd.DirExists(final_path.c_str())) {
        App::Push<OptionBox>("A backup with that name already exists."_i18n, "OK"_i18n);
        return;
    }

    if (R_FAILED(sd.RenameFile(src_path.c_str(), final_path.c_str()))) {
        App::Push<OptionBox>("Could not rename the backup."_i18n, "OK"_i18n);
        return;
    }

    const auto check_pack = account_user::FindUserPack(final_path);
    if (check_pack.dir.empty()) {
        sd.RenameFile(final_path.c_str(), src_path.c_str());
        App::Push<OptionBox>("Renamed backup is invalid."_i18n, "OK"_i18n);
        return;
    }

    Refresh();
}

void ManageBackupsMenu::ConfirmDeletePacks() {
    if (m_entries.empty()) {
        return;
    }
    std::vector<s64> idxs;
    if (m_selected_count > 0) {
        for (s64 i = 0; i < static_cast<s64>(m_entries.size()); i++) {
            if (m_entries[i].selected) {
                idxs.push_back(i);
            }
        }
    } else {
        idxs.push_back(m_index);
    }
    const auto msg = (idxs.size() > 1)
        ? "Delete the selected backups from the SD card?"_i18n
        : "Delete this backup from the SD card?"_i18n;
    App::Push<OptionBox>(msg, "Cancel"_i18n, "Delete"_i18n, 1, [this, idxs](auto op) {
        if (!op || *op != 1) {
            return;
        }
        for (auto it = idxs.rbegin(); it != idxs.rend(); ++it) {
            const auto i = *it;
            if (i < 0 || static_cast<size_t>(i) >= m_entries.size()) {
                continue;
            }
            if (R_FAILED(account_user::DeleteUserPack(m_entries[i].pack.dir))) {
                App::Push<OptionBox>("Could not delete the backup."_i18n, "OK"_i18n);
                return;
            }
            if (m_entries[i].image > 0) {
                nvgDeleteImage(App::GetVg(), m_entries[i].image);
            }
            if (m_entries[i].selected) {
                m_selected_count--;
            }
            m_entries.erase(m_entries.begin() + i);
        }
        if (m_index >= static_cast<s64>(m_entries.size())) {
            m_index = m_entries.empty() ? 0 : static_cast<s64>(m_entries.size()) - 1;
        }
        UpdateSubHeading();
    });
}

} // namespace sphaira::ui::menu::users
