// Auto backup of changed saves on Hub start. The Hub is hbmenu, so it starts often: a save that changed since its
// last automatic copy gets a new one in /dumps (the Saves → Backups library). The older automatic copy goes to the
// trash (trash.hpp), so a save broken by a game still has its earlier copy for 30 days.
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_auto_backup_state.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "option.hpp"
#include "trash.hpp"

#include <atomic>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {
namespace {

constexpr s64 MIN_FREE = 2LL * 1024 * 1024 * 1024; // the copies stop below 2 GB free on the card
constexpr const char* EMPTY = "-"; // an empty save: nothing to copy, nothing to look for

std::atomic<bool> g_running{false};
std::atomic<bool> g_stop{false}; // an install over MTP or the network asks the job to give way

auto Enabled() -> option::OptionBool& {
    static option::OptionBool opt{"saves", "auto_backup_changed", true};
    return opt;
}

auto LoadStates() -> autobackup::States {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(autobackup::STATE_PATH, data))) {
        return {};
    }
    return autobackup::Parse(std::string_view{reinterpret_cast<const char*>(data.data()), data.size()});
}

void SaveStates(const autobackup::States& states) {
    const auto text = autobackup::Format(states);
    fs::FsNativeSd().write_entire_file(autobackup::STATE_PATH, std::vector<u8>(text.begin(), text.end()));
}

} // namespace

auto Menu::AutoBackupChangedOn(ProgressBox* pbox, const std::vector<AccountUid>& uids, size_t* copied) -> Result {
    fs::FsNativeSd sd;
    auto states = LoadStates();
    std::string old_bin; // the trash item for the older copies, made on the first one
    for (const auto& uid : uids) {
        if (g_stop.load()) {
            break;
        }
        for (auto& e : ListAccountSaves(uid)) {
            R_TRY(pbox->ShouldExitResult());
            if (g_stop.load()) {
                log_write("[AUTOBACKUP] stop: an install starts\n");
                break;
            }
            FsSaveDataExtraData extra{};
            if (R_FAILED(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), static_cast<FsSaveDataSpaceId>(e.save_data_space_id), e.save_data_id))) {
                continue;
            }
            const auto it = states.find(e.save_data_id);
            const auto old = it == states.end() ? std::string{} : it->second.path;
            const bool exists = old == EMPTY || (!old.empty() && sd.FileExists(old.c_str()));
            if (!autobackup::NeedsCopy(states, e.save_data_id, extra.commit_id, exists)) {
                continue;
            }
            s64 free{};
            if (R_SUCCEEDED(sd.GetFreeSpace("/", &free)) && free < MIN_FREE) {
                log_write("[AUTOBACKUP] stop: %lld bytes free\n", static_cast<long long>(free));
                break;
            }
            detail::LoadControlEntry(e);
            fs::FsPath out{};
            // a save that a suspended game holds open cannot be read now: it is copied on a later start.
            const auto rc = BackupSaveInternal(pbox, MakeSdCardDumpLocation(), e, true, true, DEFAULT_BACKUP_ROOT, &out);
            if (R_FAILED(rc)) {
                log_write("[AUTOBACKUP] %016lX skipped: 0x%X\n", e.save_data_id, rc);
                continue;
            }
            const std::string now_path = out.s[0] ? out.s : EMPTY;
            if (!old.empty() && old != EMPTY && old != now_path && sd.FileExists(old.c_str())) {
                if (old_bin.empty()) {
                    old_bin = trash::NewItem(trash::SD, "backup", "Older automatic save copies"_i18n, std::time(nullptr));
                }
                trash::Move(trash::SD, old_bin, old);
            }
            states[e.save_data_id] = autobackup::State{extra.commit_id, now_path};
            SaveStates(states);
            if (now_path != EMPTY) {
                (*copied)++;
            }
        }
    }
    if (!old_bin.empty()) {
        trash::Journal(trash::SD, "auto backup: older copies", "Older automatic save copies"_i18n, old_bin, std::time(nullptr));
    }
    R_SUCCEED();
}

void StartAutoBackup() {
    if (!Enabled().Get() || App::HasActiveTransfer() || App::GetProgressActive()) {
        return;
    }
    std::vector<AccountUid> uids;
    for (const auto& acc : App::GetAccountList()) {
        uids.push_back(acc.uid);
    }
    if (uids.empty()) {
        return;
    }
    // made on the UI thread; the job runs in the box's own thread, as a corner badge (R3 opens it, B stops it).
    auto helper = std::make_shared<Menu>(MenuFlag_None);
    auto copied = std::make_shared<size_t>(0);
    auto pbox = std::make_unique<ProgressBox>(0, "Auto backup of changed saves"_i18n, "",
        [helper, uids, copied](ProgressBox* pbox) -> Result {
            return helper->AutoBackupChangedOn(pbox, uids, copied.get());
        }, [copied](Result rc) {
            g_running = false;
            log_write("[AUTOBACKUP] done 0x%X, copied %zu\n", rc, *copied);
            if (*copied) {
                App::Notify("Saves backed up: "_i18n + std::to_string(*copied));
            }
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
    pbox->SetMinimized(true);
    g_stop = false;
    g_running = true;
    if (!App::PushTransfer(std::move(pbox))) {
        g_running = false;
    }
}

auto StopAutoBackup() -> bool {
    if (!g_running.load()) {
        return false;
    }
    g_stop = true;
    return true;
}

auto GetAutoBackupEnabled() -> bool {
    return Enabled().Get();
}

void SetAutoBackupEnabled(bool enable) {
    Enabled().Set(enable);
}

} // namespace sphaira::ui::menu::save
