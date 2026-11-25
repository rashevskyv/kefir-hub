// Auto backup of changed saves on Hub start. The Hub is hbmenu, so it starts often: a save that changed since its
// last backup gets a new automatic copy in /dumps (the Saves → Backups library). The new copy replaces the older
// automatic one: one automatic copy per save, they do not pile up. The settings page (Saves → Auto backup) is here
// too, because this file owns the options.
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_auto_backup_state.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "save_remote_sync.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "option.hpp"
#include "defines.hpp"
#include "title_info.hpp"

#include <atomic>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {
namespace {

constexpr s64 MIN_FREE = 2LL * 1024 * 1024 * 1024; // the copies stop below 2 GB free on the card
constexpr const char* EMPTY = "-"; // no own copy to keep: an empty save, or a save with a fresh manual backup

std::atomic<bool> g_running{false};
std::atomic<bool> g_stop{false}; // an install over MTP or the network asks the job to give way
std::atomic<s64> g_done{0}, g_total{0}; // the job's overall progress, shown by the waiting box and its screensaver

auto Enabled() -> option::OptionBool& {
    static option::OptionBool opt{"saves", "auto_backup_changed", true};
    return opt;
}

auto Upload() -> option::OptionBool& {
    static option::OptionBool opt{"saves", "auto_backup_upload", false};
    return opt;
}

auto SizeLimit() -> option::OptionLong& {
    static option::OptionLong opt{"saves", "auto_backup_size_limit", 0};
    return opt;
}

auto ReadText(const char* path) -> std::string {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(path, data))) {
        return {};
    }
    return std::string{data.begin(), data.end()};
}

void WriteText(const char* path, const std::string& text) {
    fs::FsNativeSd().write_entire_file(path, std::vector<u8>(text.begin(), text.end()));
}

auto LoadStates() -> autobackup::States {
    return autobackup::Parse(ReadText(autobackup::STATE_PATH));
}

void SaveStates(const autobackup::States& states) {
    WriteText(autobackup::STATE_PATH, autobackup::Format(states));
}

auto LoadExclusions() -> std::vector<autobackup::Exclusion> {
    return autobackup::ParseExclusions(ReadText(autobackup::EXCLUDE_PATH));
}

void SaveExclusions(const std::vector<autobackup::Exclusion>& list) {
    WriteText(autobackup::EXCLUDE_PATH, autobackup::FormatExclusions(list));
}

auto AccountUids() -> std::vector<AccountUid> {
    std::vector<AccountUid> uids;
    for (const auto& acc : App::GetAccountList()) {
        uids.push_back(acc.uid);
    }
    return uids;
}

// the saves of these users and the device and BCAT saves of all games: what the auto backup copies.
// (BCAT saves, e.g. Smash, Tetris 99 and the Online classics, were left out: a manual backup then made them anew.)
auto ListSaves(const std::vector<AccountUid>& uids) -> std::vector<Entry> {
    std::vector<Entry> out;
    for (const auto& uid : uids) {
        auto part = Menu::ListAccountSaves(uid);
        out.insert(out.end(), part.begin(), part.end());
    }
    for (const u8 type : {FsSaveDataType_Device, FsSaveDataType_Bcat}) {
        for (const auto& info : DiscoverSaveDataInfo(nullptr, type)) {
            out.emplace_back(info);
        }
    }
    return out;
}

auto TypeLabel(int type) -> std::string {
    if (type == autobackup::ALL_TYPES) {
        return "All"_i18n;
    }
    return type == FsSaveDataType_Device ? "Device"_i18n : "Account"_i18n;
}

} // namespace

auto Menu::AutoBackupChangedOn(ProgressBox* pbox, const std::vector<AccountUid>& uids, size_t* copied) -> Result {
    fs::FsNativeSd sd;
    auto states = LoadStates();
    const auto exclusions = LoadExclusions();
    const auto limit = SizeLimit().Get();
    pbox->SetTitle("Auto backup of changed saves"_i18n);

    // first the changed saves (a quick read of each save's commit id), so the badge can show "N / total"
    struct Job { Entry e; u64 commit; std::string old; };
    std::vector<Job> jobs;
    for (auto& e : ListSaves(uids)) {
        if (autobackup::IsExcluded(exclusions, e.application_id, e.save_data_type) || autobackup::TooBig(e.size, limit)) {
            continue;
        }
        FsSaveDataExtraData extra{};
        if (R_FAILED(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), static_cast<FsSaveDataSpaceId>(e.save_data_space_id), e.save_data_id))) {
            continue;
        }
        const auto it = states.find(e.save_data_id);
        auto old = it == states.end() ? std::string{} : it->second.path;
        const bool exists = old == EMPTY || (!old.empty() && sd.FileExists(old.c_str()));
        if (autobackup::NeedsCopy(states, e.save_data_id, extra.commit_id, exists)) {
            jobs.push_back(Job{e, extra.commit_id, exists ? std::move(old) : std::string{}});
        }
    }

    // the cloud: found and checked once, on the first copy to upload
    const bool upload = App::GetSaveAutosync() || Upload().Get();
    std::optional<location::Entry> cloud;
    bool cloud_checked = false;

    const auto total = static_cast<s64>(jobs.size());
    ON_SCOPE_EXIT(pbox->SetOverall(0, 0));
    g_total = total;
    for (s64 i = 0; i < total; i++) {
        auto& [e, commit, old] = jobs[i];
        pbox->SetOverall(i, total);
        g_done = i;
        R_TRY(pbox->ShouldExitResult());
        if (g_stop.load()) {
            log_write("[AUTOBACKUP] stop: asked by an install or the user\n");
            break;
        }
        s64 free{};
        if (R_SUCCEEDED(sd.GetFreeSpace("/", &free)) && free < MIN_FREE) {
            log_write("[AUTOBACKUP] stop: %lld bytes free\n", static_cast<long long>(free));
            break;
        }
        detail::LoadControlEntry(e);
        // a backup made by hand after the change already holds this state: no second copy of it
        if (IsBackupUpToDate(&sd, e, DEFAULT_BACKUP_ROOT)) {
            states[e.save_data_id] = autobackup::State{commit, old.empty() ? EMPTY : old};
            SaveStates(states);
            continue;
        }
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
        std::string old_name;
        if (!old.empty() && old != EMPTY && old != now_path) {
            sd.DeleteFile(old.c_str());
            old_name = old.substr(old.find_last_of('/') + 1);
        }
        states[e.save_data_id] = autobackup::State{commit, now_path};
        SaveStates(states);
        (*copied)++;

        if (upload && !cloud_checked) {
            cloud_checked = true;
            cloud = PickSyncLocation();
            if (cloud && R_FAILED(ProbeWebdavLocation(*cloud))) {
                cloud.reset();
            }
            if (!cloud) {
                log_write("[AUTOBACKUP] no WebDAV location to upload to\n");
            }
        }
        if (cloud) {
            // ponytail: a failed upload is not retried; the copy stays on the card, Sync uploads it later.
            pbox->NewTransfer("Local → WebDAV"_i18n);
            const auto res = UploadSdBackup(*cloud, e, out, curl::OnProgress{[pbox](s64, s64, s64 ultotal, s64 ulnow) -> bool {
                pbox->UpdateTransfer(ulnow, ultotal);
                return !pbox->ShouldExit();
            }});
            if (!res.success) {
                log_write("[AUTOBACKUP] upload failed: %s (HTTP %ld)\n", out.s, res.code);
            } else if (!old_name.empty()) {
                // the cloud keeps one automatic copy too; else Sync with remote would bring the old one back
                DeleteRemoteBackup(*cloud, e, old_name);
            }
        }
    }
    pbox->SetOverall(total, total);
    R_SUCCEED();
}

void StartAutoBackup() {
    if (!Enabled().Get() || App::HasActiveTransfer() || App::GetProgressActive()) {
        return;
    }
    const auto uids = AccountUids();
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
    g_done = g_total = 0;
    g_running = true;
    if (!App::PushTransfer(std::move(pbox))) {
        g_running = false;
    }
}

void OpenWhenAutoBackupEnds(std::function<void()> open) {
    if (!g_running.load()) {
        open();
        return;
    }
    // the auto backup holds saves open: a backup or restore started now fails. Ask: wait for it, or stop it.
    // ponytail: only the entry to the saves list waits; the auto backup starts on Hub start, before anyone is inside.
    App::Push<OptionBox>("Auto backup of saves is running."_i18n, "Wait until it ends"_i18n, "Stop auto backup"_i18n, 0, [open](auto op) {
        // B picks button 0 (OptionBox with 2 buttons): it opens the waiting box, where B gives up.
        if (op && *op == 1) {
            g_stop = true; // the job ends after the current save; the box below waits for that
        }
        App::Push<ProgressBox>(0, "Auto backup of changed saves"_i18n, "", [](ProgressBox* pbox) -> Result {
            pbox->NewTransfer("Auto backup of saves is running. Wait until it ends."_i18n);
            while (g_running.load()) { // the UI thread clears it when the badge box closes
                R_TRY(pbox->ShouldExitResult());
                const auto total = g_total.load();
                pbox->SetOverall(g_done.load(), total);
                pbox->UpdateTransfer(g_done.load(), total > 0 ? total : 1);
                svcSleepThread(100'000'000);
            }
            R_SUCCEED();
        }, [open](Result rc) {
            if (R_SUCCEEDED(rc)) {
                open();
            }
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false); // it only sleeps: no CPU boost
    });
}

auto StopAutoBackup() -> bool {
    if (!g_running.load()) {
        return false;
    }
    g_stop = true;
    return true;
}

} // namespace sphaira::ui::menu::save

namespace sphaira::ui::menu::settings {
namespace {

using save::autobackup::Exclusion;

// one row per game with saves on this console, named; for the "Add game" list.
auto GamesWithSaves() -> std::vector<Exclusion> {
    std::vector<Exclusion> out;
    std::set<u64> seen;
    title::Init();
    for (auto& e : save::ListSaves(save::AccountUids())) {
        if (e.application_id && seen.insert(e.application_id).second) {
            save::detail::LoadControlEntry(e);
            out.push_back(Exclusion{e.application_id, save::autobackup::ALL_TYPES, e.GetName()});
        }
    }
    title::Exit();
    return out;
}

void AddExcludedGame() {
    auto games = std::make_shared<std::vector<Exclusion>>(GamesWithSaves());
    if (games->empty()) {
        App::Notify("No saves found"_i18n);
        return;
    }
    PopupList::Items names;
    for (const auto& g : *games) {
        names.push_back(g.name);
    }
    App::Push<PopupList>("Add game"_i18n, std::move(names), [games](std::optional<s64> game) {
        if (!game || *game < 0 || *game >= static_cast<s64>(games->size())) {
            return;
        }
        const auto picked = (*games)[static_cast<size_t>(*game)];
        static constexpr int TYPES[]{save::autobackup::ALL_TYPES, FsSaveDataType_Account, FsSaveDataType_Device};
        PopupList::Items types;
        for (const auto t : TYPES) {
            types.push_back(save::TypeLabel(t));
        }
        App::Push<PopupList>(picked.name, std::move(types), [picked](std::optional<s64> type) {
            if (!type || *type < 0 || *type >= static_cast<s64>(std::size(TYPES))) {
                return;
            }
            auto list = save::LoadExclusions();
            auto x = picked;
            x.type = TYPES[*type];
            if (!save::autobackup::IsExcluded(list, x.app_id, x.type)) {
                list.push_back(x);
                save::SaveExclusions(list);
            }
        });
    });
}

auto BuildExcludedItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;
    items.emplace_back(SettingsItem{"Add game"_i18n, "Games and save types that the auto backup leaves out."_i18n,
        [](){ return std::string{}; }, AddExcludedGame, SettingsItemKind::Folder});

    const auto list = save::LoadExclusions();
    for (size_t i = 0; i < list.size(); i++) {
        const auto& x = list[i];
        char id[17];
        std::snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(x.app_id));
        const auto name = x.name.empty() ? std::string{id} : x.name;
        items.emplace_back(SettingsItem{name, "Games and save types that the auto backup leaves out."_i18n,
            [type = x.type](){ return save::TypeLabel(type); },
            [x, name](){
                App::Push<OptionBox>("Remove from the list?"_i18n + "\n\n" + name, "Back"_i18n, "Remove"_i18n, 0, [x](auto op) {
                    if (op && *op == 1) {
                        auto now = save::LoadExclusions();
                        std::erase_if(now, [&](const Exclusion& y) { return y.app_id == x.app_id && y.type == x.type; });
                        save::SaveExclusions(now);
                    }
                });
            }});
    }
    return items;
}

} // namespace

auto BuildAutoBackupItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;
    items.emplace_back(MakeOptionItem("Auto backup of changed saves"_i18n,
        "On each Hub start, copy the saves that changed since their last backup. The new copy replaces the older automatic one."_i18n,
        save::Enabled()));
    items.emplace_back(MakeBoolItem("Auto-sync after backup"_i18n,
        "Shared with Backup: upload every new backup, manual or automatic, to the save sync location."_i18n,
        App::GetSaveAutosync, App::SetSaveAutosync));
    items.emplace_back(MakeOptionItem("Upload auto backups"_i18n,
        "Upload each new automatic copy to the save sync location, also when \"Auto-sync after backup\" is off."_i18n,
        save::Upload()));
    items.emplace_back(SettingsItem{"Size limit"_i18n, "Saves bigger than this are left out of the auto backup."_i18n,
        [](){
            const auto i = ClampIndex(save::SizeLimit().Get(), static_cast<long>(save::autobackup::SIZE_LIMITS.size()));
            return i ? std::string{save::autobackup::SIZE_LIMIT_LABELS[i]} : "Off"_i18n;
        },
        [](){
            PopupList::Items labels;
            for (size_t i = 0; i < save::autobackup::SIZE_LIMIT_LABELS.size(); i++) {
                labels.push_back(i ? std::string{save::autobackup::SIZE_LIMIT_LABELS[i]} : "Off"_i18n);
            }
            App::Push<PopupList>("Size limit"_i18n, std::move(labels), [](std::optional<s64> op) {
                if (op) {
                    save::SizeLimit().Set(static_cast<long>(*op));
                }
            }, save::SizeLimit().Get());
        }});
    // on this page, not a folder of its own: the settings menu keeps one open folder, B would skip this page
    items.emplace_back(MakeHeader("Excluded games"_i18n));
    auto excluded = BuildExcludedItems();
    items.insert(items.end(), std::make_move_iterator(excluded.begin()), std::make_move_iterator(excluded.end()));
    return items;
}

} // namespace sphaira::ui::menu::settings
