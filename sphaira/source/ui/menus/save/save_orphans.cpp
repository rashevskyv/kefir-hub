#include "ui/menus/save/save_orphans.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "evman.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <atomic>
#include <cstdio>
#include <memory>

namespace sphaira::ui::menu::save {
namespace {

auto SavePath(u64 save_data_id) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "/save/%016llX", static_cast<unsigned long long>(save_data_id));
    return path;
}

// only "not there" counts as missing: any other error leaves the record alone.
auto IsMissing(fs::FsNativeBis& bis, u64 save_data_id) -> bool {
    FsDirEntryType type{};
    return R_VALUE(bis.GetEntryType(SavePath(save_data_id), &type)) == FsError_PathNotFound;
}

} // namespace

auto FindRecordsWithoutData(const std::vector<FsSaveDataInfo>& saves) -> std::vector<FsSaveDataInfo> {
    std::vector<FsSaveDataInfo> out;
    fs::FsNativeBis bis(FsBisPartitionId_User);
    if (R_FAILED(bis.GetFsOpenResult())) {
        log_write("[SAVE] user partition not readable: 0x%X\n", bis.GetFsOpenResult());
        return out;
    }
    for (const auto& info : saves) {
        if (info.save_data_space_id == FsSaveDataSpaceId_User && info.save_data_id && IsMissing(bis, info.save_data_id)) {
            out.push_back(info);
        }
    }
    return out;
}

auto DeleteRecordWithoutData(u64 save_data_id) -> Result {
    fs::FsNativeBis bis(FsBisPartitionId_User);
    R_TRY(bis.GetFsOpenResult());
    const auto path = SavePath(save_data_id);
    R_TRY(bis.CreateFile(path, 0, 0));
    const Result rc = fsDeleteSaveDataFileSystemBySaveDataSpaceId(FsSaveDataSpaceId_User, save_data_id);
    log_write("[SAVE] record without data 0x%016lX: delete with a stand-in file 0x%X\n", save_data_id, rc);
    if (R_FAILED(rc) && !IsMissing(bis, save_data_id)) {
        bis.DeleteFile(path);
    }
    return rc;
}

void OfferToRemoveRecordsWithoutData(const std::vector<FsSaveDataInfo>& saves) {
    static std::atomic<bool> s_checked{false};
    if (s_checked.exchange(true)) {
        return;
    }
    auto records = std::make_shared<std::vector<FsSaveDataInfo>>(FindRecordsWithoutData(saves));
    log_write("[SAVE] records without data: %zu\n", records->size());
    if (records->empty()) {
        return;
    }

    // after the scan that found them: the menu is drawn by then.
    evman::push(evman::FunctionalEventData{[records]() {
        App::Push<OptionBox>(
            std::string{"Found saves without data: "_i18n} + std::to_string(records->size()) + "\n\n" +
            "Their data is no longer on the console, only the record is left. This happens when the user partition was formatted, for example when a compressed emuMMC was made. These saves cannot be backed up or restored."_i18n,
            "Keep"_i18n, "Remove"_i18n, 1, [records](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }
            auto removed = std::make_shared<size_t>(0);
            App::Push<ProgressBox>(0, "Removing saves without data"_i18n, "", [records, removed](auto pbox) -> Result {
                for (size_t i = 0; i < records->size(); i++) {
                    R_TRY(pbox->ShouldExitResult());
                    pbox->UpdateTransfer(i + 1, records->size());
                    if (R_SUCCEEDED(DeleteRecordWithoutData((*records)[i].save_data_id))) {
                        (*removed)++;
                    }
                }
                R_SUCCEED();
            }, [records, removed](Result) {
                SignalChange();
                App::Notify("Removed: "_i18n + std::to_string(*removed) + " / " + std::to_string(records->size()));
            });
        });
    }}, false);
}

} // namespace sphaira::ui::menu::save
