#include "ui/menus/save_menu.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include <vector>
#include <cstring>

namespace sphaira::ui::menu::save {
namespace {

auto DeleteLiveSaveEntry(const Entry& e) -> Result {
    if (e.save_data_id == 0) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    const auto space_id = static_cast<FsSaveDataSpaceId>(e.save_data_space_id);
    const Result rc = fsDeleteSaveDataFileSystemBySaveDataSpaceId(space_id, e.save_data_id);
    log_write("[SAVE] fsDeleteSaveDataFileSystemBySaveDataSpaceId(0x%x, 0x%016lX): 0x%x\n", space_id, e.save_data_id, rc);
    return rc;
}

} // namespace

auto Menu::DeleteSavesOn(ProgressBox* pbox, std::vector<Entry> entries) -> Result {
    for (size_t i = 0; i < entries.size(); i++) {
        R_TRY(pbox->ShouldExitResult());
        auto& e = entries[i];
        detail::LoadControlEntry(e);
        pbox->SetTitle(e.GetName());
        pbox->UpdateTransfer(i + 1, entries.size());
        pbox->SetActionName("Deleting save data..."_i18n);

        R_TRY(DeleteLiveSaveEntry(e));
    }
    R_SUCCEED();
}

void Menu::DeleteSaves(std::vector<Entry> entries) {
    if (entries.empty()) {
        return;
    }

    auto deleted_count = std::make_shared<size_t>(0);

    App::Push<ProgressBox>(0, "Deleting saves..."_i18n, "", [this, entries, deleted_count](auto pbox) mutable -> Result {
        fs::FsNativeSd sd_fs;
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

        for (size_t i = 0; i < entries.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            auto& e = entries[i];
            detail::LoadControlEntry(e);
            pbox->SetTitle(e.GetName());
            if (e.image) {
                pbox->SetImage(e.image);
            } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
                pbox->SetImageDataConst(data->icon);
            } else {
                pbox->SetImage(0);
            }
            pbox->UpdateTransfer(i + 1, entries.size());

            if (e.is_backup) {
                pbox->SetActionName("Deleting backup files..."_i18n);
                const auto backups = CollectBackups(&sd_fs, e, backup_root);
                for (const auto& b : backups) {
                    R_TRY(pbox->ShouldExitResult());
                    R_TRY(sd_fs.DeleteFile(b.path));
                    (*deleted_count)++;
                }

                // Also clean up empty game directories in DBI and dumps
                if (!IsSystemLikeSave(e.save_data_type)) {
                    const auto dbi_game_dir = fs::AppendPath(sd_fs.Root(), fs::AppendPath(fs::FsPath{DBI_SAVES_PATH}, BuildDbiGameFolderName(e)));
                    sd_fs.DeleteDirectory(dbi_game_dir);
                }
                const auto sphaira_dir = fs::AppendPath(sd_fs.Root(), BuildSaveBasePath(e, false, backup_root));
                sd_fs.DeleteDirectory(sphaira_dir);
                const auto sphaira_id_dir = fs::AppendPath(sd_fs.Root(), BuildSaveBasePath(e, true, backup_root));
                sd_fs.DeleteDirectory(sphaira_id_dir);

                // Custom search paths clean up
                for (const auto& custom_path_str : GetBackupSearchPaths()) {
                    const fs::FsPath custom_root{custom_path_str};
                    const auto custom_sphaira_dir = fs::AppendPath(sd_fs.Root(), BuildSaveBasePath(e, false, custom_root));
                    sd_fs.DeleteDirectory(custom_sphaira_dir);
                    const auto custom_sphaira_id_dir = fs::AppendPath(sd_fs.Root(), BuildSaveBasePath(e, true, custom_root));
                    sd_fs.DeleteDirectory(custom_sphaira_id_dir);
                }
            } else {
                pbox->SetActionName("Deleting save data..."_i18n);
                R_TRY(DeleteLiveSaveEntry(e));
                (*deleted_count)++;
            }
        }
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Delete failed!"_i18n);
        } else {
            App::Notify("Delete successful!"_i18n);
        }

        ClearSelection();
        ScanHomebrew();
    });
}

} // namespace sphaira::ui::menu::save
