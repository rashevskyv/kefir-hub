// Auto backup of changed saves on Hub start. The Hub is hbmenu, so it starts often: a save that changed since its
// last automatic copy gets a new one in /dumps (the Saves → Backups library). The new copy replaces the older
// automatic one: one automatic copy per save, they do not pile up.
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
#include "defines.hpp"

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
    pbox->SetTitle("Auto backup of changed saves"_i18n);

    // first the changed saves (a quick read of each save's commit id), so the badge can show "N / total"
    struct Job { Entry e; u64 commit; std::string old; };
    std::vector<Job> jobs;
    for (const auto& uid : uids) {
        for (auto& e : ListAccountSaves(uid)) {
            FsSaveDataExtraData extra{};
            if (R_FAILED(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), static_cast<FsSaveDataSpaceId>(e.save_data_space_id), e.save_data_id))) {
                continue;
            }
            const auto it = states.find(e.save_data_id);
            auto old = it == states.end() ? std::string{} : it->second.path;
            const bool exists = old == EMPTY || (!old.empty() && sd.FileExists(old.c_str()));
            if (autobackup::NeedsCopy(states, e.save_data_id, extra.commit_id, exists)) {
                jobs.push_back(Job{e, extra.commit_id, std::move(old)});
            }
        }
    }

    const auto total = static_cast<s64>(jobs.size());
    ON_SCOPE_EXIT(pbox->SetOverall(0, 0));
    for (s64 i = 0; i < total; i++) {
        auto& [e, commit, old] = jobs[i];
        pbox->SetOverall(i, total);
        R_TRY(pbox->ShouldExitResult());
        if (g_stop.load()) {
            log_write("[AUTOBACKUP] stop: an install starts\n");
            break;
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
        if (now_path == EMPTY) {
            // the save is empty now (a game may have wiped it): the last copy stays, it may be the only good one
            states[e.save_data_id] = autobackup::State{commit, old.empty() ? EMPTY : old};
            SaveStates(states);
            continue;
        }
        // one automatic copy per save: the new one replaces the old one, the copies do not pile up
        if (!old.empty() && old != EMPTY && old != now_path) {
            sd.DeleteFile(old.c_str());
        }
        states[e.save_data_id] = autobackup::State{commit, now_path};
        SaveStates(states);
        (*copied)++;
    }
    pbox->SetOverall(total, total);
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
