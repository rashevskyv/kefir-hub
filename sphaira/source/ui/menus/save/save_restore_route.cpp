#include "ui/menus/save_menu.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/save/save_batch_util.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_slot_backend.hpp"
#include "ui/menus/save/save_bundle_util.hpp"
#include "ui/menus/save/save_folder_discovery.hpp"
#include "save_menu_internal.hpp"
#include "minizip_helper.hpp"
#include <minizip/unzip.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>
namespace sphaira::ui::menu::save {
namespace {

struct ProbeFilesystem {
    fs::FsStdio stdio_fs;
    fs::FsNativeSd sd_fs;
    auto Get(const dump::DumpLocation& loc, const fs::FsPath& root) -> fs::Fs* {
        return (loc.entry.type == dump::DumpLocationType_Stdio || root.starts_with("ums"))
            ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);
    }
    auto Get(const fs::FsPath& path) -> fs::Fs* {
        return path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);
    }
};

} // namespace

auto Menu::MakeBackupGroupFromLiveEntry(const Entry& live, const dump::DumpLocation& location, const fs::FsPath& backup_root, std::optional<BackupSource> source_filter) const -> Entry {
    Entry group = live;
    group.is_backup = true;
    const bool live_space_known = live.is_backup ? live.backup_space_known : true;
    const bool live_rank_known = live.is_backup ? live.backup_rank_known : true;
    group.backup_space_known = live_space_known;
    group.backup_rank_known = live_rank_known;
    group.backup_members.clear();
    group.backup_count = 0;
    group.backup_path.clear();
    group.backup_timestamp = 0;
    group.backup_is_directory = false;
    if (!IsSystemLikeSave(group.save_data_type)) {
        group.dbi_game_dir = BuildDbiGameFolderName(live).s;
    }

    ProbeFilesystem pfs;
    fs::Fs* probe_fs = pfs.Get(location, backup_root);

    const auto candidates = CollectBackups(probe_fs, live, backup_root);
    const BackupSource target_source = source_filter.value_or(BackupSource::KefirHub);
    group.backup_source = target_source;

    std::vector<std::pair<BackupCandidate, BackupArchiveInfo>> retained;
    std::unordered_set<std::string> seen_paths;
    for (const auto& c : candidates) {
        if (c.path.empty() || !seen_paths.insert(c.path.s).second) continue;
        const auto slash = std::strrchr(c.path.s, '/');
        const auto fname = slash ? (slash + 1) : c.path.s;
        BackupArchiveInfo info{};
        const bool inspected = c.is_directory
            ? InspectBackupFolder(probe_fs, c.path, fname, group.dbi_game_dir, info)
            : InspectBackupArchive(probe_fs, c.path, fname, group.dbi_game_dir, info);
        if (!inspected || info.backup_source != target_source) continue;
        if (!bundle::AreBackupSlotIdentitiesCompatible(
                IsSystemLikeSave(live.save_data_type), live.save_data_type,
                IsSystemLikeSave(live.save_data_type) ? live.system_save_data_id : live.application_id,
                live_space_known, live.save_data_space_id,
                live.uid.uid, live.save_data_index, live_rank_known, live.save_data_rank,
                IsSystemLikeSave(info.save_data_type), info.save_data_type,
                IsSystemLikeSave(info.save_data_type) ? info.system_save_data_id : info.application_id,
                info.source_space.has_value(), info.source_space.value_or(0),
                info.uid.uid, info.save_data_index, info.rank_known, info.save_data_rank)) {
            continue;
        }
        retained.emplace_back(c, info);
    }

    std::ranges::sort(retained, [](const auto& a, const auto& b) {
        return (a.first.ts != b.first.ts) ? (a.first.ts > b.first.ts) : (a.first.path.toString() < b.first.path.toString());
    });

    if (!retained.empty()) {
        const auto& newest = retained.front();
        const auto target_group_key = BackupGroupKey(newest.second);
        group.backup_path = newest.first.path;
        group.backup_timestamp = newest.first.ts;
        group.backup_is_directory = newest.first.is_directory;
        if (newest.second.source_space.has_value()) {
            group.save_data_space_id = *newest.second.source_space;
            group.backup_space_known = true;
        } else {
            group.save_data_space_id = 0;
            group.backup_space_known = false;
        }
        group.backup_rank_known = newest.second.rank_known;
        group.save_data_rank = newest.second.save_data_rank;
        group.backup_owner_name = newest.second.owner_name;
        group.source_timestamp = newest.second.source_timestamp;
        group.commit_id = newest.second.commit_id;
        for (const auto& item : retained) {
            if (BackupGroupKey(item.second) == target_group_key) {
                group.backup_members.emplace_back(item.first);
            }
        }
    }
    group.backup_count = group.backup_members.size();
    return group;
}

auto Menu::MakeBackupGroupFromLiveEntry(const Entry& live, const fs::FsPath& backup_root, std::optional<BackupSource> source_filter) const -> Entry {
    return MakeBackupGroupFromLiveEntry(live, MakeSdCardDumpLocation(), backup_root, source_filter);
}

void Menu::StartRestore(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root, std::optional<BackupSource> source_filter) {
    entries = ExpandGameGroups(entries);
    if (entries.empty()) return;

    if (m_category == Category::Backups || entries.front().is_backup) {
        RestoreBackupGroups(std::move(entries), false, location, backup_root);
        return;
    }

    if (entries.size() == 1 && App::GetSaveRestoreIncludeRemote()) {
        const auto webdav_locations = GetWebdavLocations();
        if (!webdav_locations.empty()) {
            Entry e = entries.front();
            const auto run = [this, e, location, backup_root, source_filter](const location::Entry& loc) {
                auto downloaded = std::make_shared<std::vector<std::string>>();
                App::Push<ProgressBox>(0, "Syncing saves..."_i18n, "",
                    [this, e, loc, location, backup_root, downloaded](auto pbox) mutable -> Result {
                        pbox->SetHideSpeed(true);
                        return DownloadRemoteBackupsForEntry(pbox, loc, location, e, backup_root, downloaded.get());
                    },
                    [this, e, location, backup_root, source_filter](Result rc) mutable {
                        if (R_FAILED(rc)) {
                            App::PushErrorBox(rc, "Sync failed!"_i18n);
                        }
                        auto group = MakeBackupGroupFromLiveEntry(e, location, backup_root, source_filter);
                        RestoreBackupGroups({std::move(group)}, false, location, backup_root);
                    });
            };

            if (webdav_locations.size() == 1) {
                run(webdav_locations.front());
                return;
            }
            PopupList::Items items;
            for (const auto& loc : webdav_locations) {
                std::string proto = loc.protocol.empty() ? "webdav" : loc.protocol;
                std::transform(proto.begin(), proto.end(), proto.begin(), ::toupper);
                items.emplace_back(loc.name + " (" + proto + ")");
            }
            App::Push<PopupList>("Select Sync Location"_i18n, items, [webdav_locations, run](auto op_index) {
                if (op_index) run(webdav_locations[*op_index]);
            });
            return;
        }
    }

    std::vector<Entry> groups = CollectBackupEntriesForRestore(entries, location, backup_root, source_filter);
    if (groups.empty()) {
        groups.reserve(entries.size());
        for (const auto& e : entries) {
            groups.emplace_back(MakeBackupGroupFromLiveEntry(e, location, backup_root, source_filter));
        }
    }
    RestoreBackupGroups(std::move(groups), false, location, backup_root);
}

void Menu::ShowRestorePickerPopup(Entry e, const Entry& group, const dump::DumpLocation& location, const fs::FsPath& backup_root, std::vector<std::string> remote_names, std::vector<BackupCandidate> candidates) {
    if (candidates.empty()) {
        App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
        return;
    }

    if (candidates.size() == 1) {
        RestoreSavesPicked(std::move(e), group, location, backup_root, candidates.front().path);
        return;
    }

    const std::set<std::string> remote_set{remote_names.begin(), remote_names.end()};

    const auto label_for = [](const BackupCandidate& c) -> std::string {
        if (c.ts == 0) {
            const auto name = std::strrchr(c.path.s, '/');
            return name ? name + 1 : c.path.s;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04u.%02u.%02u  %02u:%02u:%02u",
            (u32)(c.ts / 10000000000ULL), (u32)(c.ts / 100000000ULL % 100), (u32)(c.ts / 1000000ULL % 100),
            (u32)(c.ts / 10000ULL % 100), (u32)(c.ts / 100ULL % 100), (u32)(c.ts % 100));
        return buf;
    };

    std::vector<std::string> raw_labels;
    raw_labels.reserve(candidates.size());
    std::unordered_map<std::string, size_t> label_counts;
    for (const auto& c : candidates) {
        auto lbl = label_for(c);
        label_counts[lbl]++;
        raw_labels.emplace_back(std::move(lbl));
    }

    PopupList::Items items;
    std::vector<bool> markers;
    for (size_t i = 0; i < candidates.size(); i++) {
        const auto& c = candidates[i];
        const auto name = std::strrchr(c.path.s, '/');
        const std::string base = name ? name + 1 : c.path.s;

        std::string label = raw_labels[i];
        if (label_counts[raw_labels[i]] > 1) {
            label += " (" + std::string(c.path.s) + ")";
        }

        items.emplace_back(std::move(label));
        markers.emplace_back(remote_set.contains(base));
    }

    const bool any_remote = std::ranges::any_of(markers, [](bool b){ return b; });

    auto popup = std::make_unique<PopupList>("Select backup"_i18n, items,
        [this, e = std::move(e), group, location, backup_root, candidates](auto op_index) mutable {
            if (op_index) {
                RestoreSavesPicked(std::move(e), group, location, backup_root, candidates[*op_index].path);
            }
        });
    if (any_remote) {
        popup->SetRemoteMarkers(std::move(markers));
    }
    App::Push(std::move(popup));
}

static void PickArchiveFromCandidates(const std::vector<BackupCandidate>& candidates, std::function<void(std::optional<fs::FsPath>)> cb) {
    if (candidates.empty()) { cb(std::nullopt); return; }
    if (candidates.size() == 1) { cb(candidates.front().path); return; }
    PopupList::Items items;
    for (const auto& c : candidates) {
        const auto name = std::strrchr(c.path.s, '/');
        items.emplace_back(name ? name + 1 : c.path.s);
    }
    auto popup = std::make_unique<PopupList>("Select backup"_i18n, items, [candidates, cb](auto op_index) {
        if (!op_index || *op_index >= static_cast<s64>(candidates.size())) { cb(std::nullopt); return; }
        cb(candidates[*op_index].path);
    });
    App::Push(std::move(popup));
}

void PlanRestoreCreation(const Entry& group, const AccountUid& dest_uid, const fs::FsPath& archive_path, std::function<void(std::optional<Entry>)> cb) {

    const auto outcome = bundle::EvaluateRestoreDestination(
        false, group.save_data_type, group.save_data_rank, group.save_data_index, group.application_id);
    if (outcome == bundle::RestoreDestinationOutcome::UnsupportedMissingDestination) {
        if (group.save_data_type != FsSaveDataType_Account && group.save_data_type != FsSaveDataType_Device && group.save_data_type != FsSaveDataType_Bcat) {
            App::Push<OptionBox>("Save slot creation is only supported for Account, Device and BCAT saves."_i18n, "OK"_i18n);
        } else if (group.application_id == 0) {
            App::Push<OptionBox>("Save slot creation is only supported for installed titles."_i18n, "OK"_i18n);
        } else {
            App::Push<OptionBox>("Save slot creation is only supported for primary save slots."_i18n, "OK"_i18n);
        }
        cb(std::nullopt); return;
    }

    struct PlanContext {
        SaveArchiveAdmissionResult admission{};
        SaveCreationRequest req{};
        SaveBackendStatus status{SaveBackendStatus::Success};
        Result plan_rc{0};
        bool reinspect_ok{false};
        bool is_folder{false};
    };
    auto ctx = std::make_shared<PlanContext>();

    App::Push<ProgressBox>(0, "Checking backup..."_i18n, "",
        [archive_path, group, dest_uid, ctx](auto pbox) -> Result {
            pbox->SetHideSpeed(true);
            pbox->SetTransfer("Verifying archive..."_i18n);

            ProbeFilesystem pfs;
            fs::Fs* probe_fs = pfs.Get(archive_path);

            FsDirEntryType ptype{};
            ctx->is_folder = (R_SUCCEEDED(probe_fs->GetEntryType(archive_path, &ptype)) && ptype == FsDirEntryType_Dir);

            if (ctx->is_folder) {
                ctx->admission = InspectSaveFolderAdmission(archive_path, pbox, false);
                // a JKSV / Checkpoint folder has no metadata: the save is then sized from the installed game
                // (PlanAccountSaveCreation reports "not installed" when it is not).
                if (!ctx->admission.admitted) return ctx->admission.rc ? ctx->admission.rc : static_cast<Result>(FsError_PathNotFound);
            } else {
                ctx->admission = InspectSaveArchiveAdmission(archive_path, pbox, false);
                if (!ctx->admission.admitted) return ctx->admission.rc ? ctx->admission.rc : static_cast<Result>(FsError_PathNotFound);
            }
            if (pbox->ShouldExit()) return Result_TransferCancelled;

            ctx->plan_rc = PlanAccountSaveCreation(
                group.application_id, dest_uid,
                &ctx->admission.sizing,
                ctx->req, &ctx->status, group.save_data_type);
            if (R_FAILED(ctx->plan_rc) || ctx->status != SaveBackendStatus::Success) {
                return ctx->plan_rc ? ctx->plan_rc : static_cast<Result>(FsError_InvalidSize);
            }

            const char* filename = std::strrchr(archive_path.s, '/');
            filename = filename ? filename + 1 : archive_path.s;
            BackupArchiveInfo check_info{};
            const bool reinspect_ok = ctx->is_folder
                ? InspectBackupFolder(probe_fs, archive_path, filename, group.dbi_game_dir, check_info)
                : InspectBackupArchive(probe_fs, archive_path, filename, group.dbi_game_dir, check_info);
            if (reinspect_ok && MatchesSelectedBackup(check_info, group)) {
                ctx->reinspect_ok = true;
            }
            return 0;
        },
        [group, ctx, cb](Result rc) mutable {
            if (rc == Result_TransferCancelled) {
                cb(std::nullopt);
                return;
            }
            if (!ctx->admission.admitted) {
                App::Push<OptionBox>("Invalid or corrupt save backup archive."_i18n, "OK"_i18n);
                cb(std::nullopt); return;
            }
            if (R_FAILED(ctx->plan_rc) || ctx->status != SaveBackendStatus::Success) {
                App::Push<OptionBox>(GetBackendStatusMessage(ctx->status, group.GetName()), "OK"_i18n);
                cb(std::nullopt); return;
            }
            if (!ctx->reinspect_ok) {
                App::Push<OptionBox>("Selected backup archive has changed or is no longer available."_i18n, "OK"_i18n);
                cb(std::nullopt); return;
            }

            Entry target = group;
            target.is_backup = false;
            target.is_planned_create = true;
            target.creation_request = ctx->req;
            target.uid = ctx->req.attr.uid;
            target.save_data_id = 0;
            target.save_data_space_id = ctx->req.space_id;
            target.save_data_type = ctx->req.attr.save_data_type;
            target.save_data_rank = ctx->req.attr.save_data_rank;
            target.save_data_index = ctx->req.attr.save_data_index;
            target.size = ctx->req.data_size;
            cb(std::move(target));
        }
    );
}

void Menu::RestoreSingleBackupGroup(Entry group, const AccountUid* explicit_dest_uid, bool force_user_picker, const dump::DumpLocation& location, const fs::FsPath& backup_root, bool return_to_actions) {
    if (group.backup_members.empty()) {
        ProbeFilesystem pfs;
        fs::Fs* probe_fs = pfs.Get(location, backup_root);
        group.backup_members = CollectGroupArchives(probe_fs, group, backup_root);
        group.backup_count = group.backup_members.size();
        if (!group.backup_members.empty()) {
            group.backup_path = group.backup_members.front().path;
            group.backup_timestamp = group.backup_members.front().ts;
        }
    }

    if (group.backup_members.empty()) {
        App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
        return;
    }

    const auto on_account_ready = [this, group, location, backup_root](const AccountUid* explicit_uid) {
        const auto candidates = FindLiveRestoreCandidates(group, explicit_uid);
        if (candidates.empty()) {
            const auto on_archive_selected = [this, group, explicit_uid, location, backup_root](const fs::FsPath& chosen_archive) {
                PlanRestoreCreation(group, explicit_uid ? *explicit_uid : AccountUid{}, chosen_archive,
                    [this, group, location, backup_root, chosen_archive](std::optional<Entry> target) mutable {
                        if (!target) return;
                        RestoreSavesPicked(std::move(*target), group, location, backup_root, chosen_archive);
                    });
            };

            if (group.backup_members.size() == 1) {
                on_archive_selected(group.backup_members.front().path);
            } else {
                PickArchiveFromCandidates(group.backup_members, [on_archive_selected](std::optional<fs::FsPath> picked) {
                    if (!picked) return;
                    on_archive_selected(*picked);
                });
            }
            return;
        }

        ResolveRestoreTarget(group, explicit_uid, [this, group, location, backup_root](std::optional<Entry> target) {
            if (!target) {
                return;
            }

            if (group.backup_members.size() == 1) {
                RestoreSavesPicked(std::move(*target), group, location, backup_root, group.backup_members.front().path);
            } else {
                ShowRestorePickerPopup(std::move(*target), group, location, backup_root, {}, group.backup_members);
            }
        });
    };

    if (explicit_dest_uid) {
        on_account_ready(explicit_dest_uid);
        return;
    }

    if (group.save_data_type == FsSaveDataType_Account) {
        const auto accounts = App::GetAccountList();
        if (accounts.empty()) {
            App::Push<OptionBox>("No user accounts found on this console."_i18n, "OK"_i18n);
            return;
        }

        const auto items = AccountPickerItems(accounts);

        auto popup = std::make_unique<PopupList>(BackupPickerTitle(group, accounts), items, [this, group, accounts, location, backup_root](auto op_index) mutable {
            if (!op_index || *op_index >= static_cast<s64>(accounts.size())) {
                return;
            }
            const auto chosen_uid = accounts[*op_index].uid;
            RestoreSingleBackupGroup(std::move(group), &chosen_uid, false, location, backup_root);
        }, AccountIndexOf(group.uid, accounts));
        if (return_to_actions) {
            auto* raw = popup.get();
            popup->SetAction(Button::B, Action{"Back"_i18n, [this, raw, group]() {
                raw->SetPop();
                PromptBackupGroupAction({group});
            }});
        }
        App::Push(std::move(popup));
        return;
    }

    on_account_ready(nullptr);
}

void Menu::RestoreSingleBackupGroup(Entry group, const AccountUid* explicit_dest_uid, bool force_user_picker) {
    RestoreSingleBackupGroup(std::move(group), explicit_dest_uid, force_user_picker, MakeSdCardDumpLocation(), DEFAULT_BACKUP_ROOT);
}

void Menu::RestoreBackupGroups(std::vector<Entry> groups, bool force_user_picker, const dump::DumpLocation& location, const fs::FsPath& backup_root, bool return_to_actions) {
    groups = ExpandGameGroups(groups);
    if (groups.empty()) return;

    ProbeFilesystem pfs;
    fs::Fs* probe_fs = pfs.Get(location, backup_root);

    for (auto& g : groups) {
        if (g.backup_members.empty()) {
            g.backup_members = CollectGroupArchives(probe_fs, g, backup_root);
            g.backup_count = g.backup_members.size();
            if (!g.backup_members.empty()) {
                g.backup_path = g.backup_members.front().path;
                g.backup_timestamp = g.backup_members.front().ts;
            }
        }
    }
    KeepNewestPerSlot(groups);
    std::erase_if(groups, [](const Entry& g) { return g.backup_members.empty(); });
    if (groups.empty()) {
        App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
        return;
    }

    if (groups.size() == 1) {
        RestoreSingleBackupGroup(std::move(groups.front()), nullptr, force_user_picker, location, backup_root, return_to_actions);
        return;
    }

    // ask "which user?" once for the whole batch, unless two saves would collide in one account.
    std::vector<std::string> account_slots;
    for (const auto& g : groups) {
        if (g.save_data_type == FsSaveDataType_Account) {
            account_slots.emplace_back(std::to_string(g.application_id) + ':' + std::to_string(g.save_data_index) + ':' + std::to_string(g.save_data_rank));
        }
    }
    auto shared_uid = CanShareAccount(account_slots) ? std::make_shared<std::optional<AccountUid>>() : nullptr;

    const auto accounts = App::GetAccountList();
    auto resolved_targets = std::make_shared<std::vector<Entry>>(groups.size());
    auto seen_target_keys = std::make_shared<std::set<std::string>>();
    PromptBatchRestoreTargets(std::make_shared<std::vector<Entry>>(std::move(groups)), 0,
        std::make_shared<std::vector<AccountProfileBase>>(accounts), resolved_targets, seen_target_keys,
        location, backup_root, return_to_actions, shared_uid);
}

void Menu::RestoreBackupGroups(std::vector<Entry> groups, bool force_user_picker, bool return_to_actions) {
    RestoreBackupGroups(std::move(groups), force_user_picker, MakeSdCardDumpLocation(), DEFAULT_BACKUP_ROOT, return_to_actions);
}

void Menu::PromptBatchRestoreTargets(
    std::shared_ptr<std::vector<Entry>> seeds, size_t step,
    std::shared_ptr<std::vector<AccountProfileBase>> accounts,
    std::shared_ptr<std::vector<Entry>> resolved_targets,
    std::shared_ptr<std::set<std::string>> seen_target_keys,
    const dump::DumpLocation& location, const fs::FsPath& backup_root,
    bool return_to_actions, std::shared_ptr<std::optional<AccountUid>> shared_uid) {

    if (step >= seeds->size()) {
        RestoreSaves(std::move(*seeds), std::move(*resolved_targets), location, backup_root);
        return;
    }

    const auto on_target_resolved = [this, seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, return_to_actions, shared_uid](std::optional<Entry> target) {
        if (!target) {
            return;
        }

        const auto key = SaveEntryKey(*target);
        if (!seen_target_keys->insert(key).second) {
            // ask this save again, one by one (a shared answer would just repeat the clash).
            App::Push<OptionBox>("This user already gets another backup of this game. Choose another user, or restore one backup on its own."_i18n, "OK"_i18n,
                [this, seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, return_to_actions](auto) {
                    if ((*seeds)[step].save_data_type == FsSaveDataType_Account) PromptBatchRestoreTargets(seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, return_to_actions);
                });
            return;
        }

        (*resolved_targets)[step] = std::move(*target);
        PromptBatchRestoreTargets(seeds, step + 1, accounts, resolved_targets, seen_target_keys, location, backup_root, return_to_actions, shared_uid);
    };

    const auto& current_seed = (*seeds)[step];
    const auto resolve_for_uid = [this, &current_seed, on_target_resolved](const AccountUid* uid) {
        const auto candidates = FindLiveRestoreCandidates(current_seed, uid);
        if (candidates.empty()) {
            const auto archive_path = current_seed.backup_members.empty()
                ? current_seed.backup_path
                : current_seed.backup_members.front().path;
            PlanRestoreCreation(current_seed, uid ? *uid : AccountUid{}, archive_path, on_target_resolved);
            return;
        }
        ResolveRestoreTarget(current_seed, uid, on_target_resolved);
    };

    if (current_seed.save_data_type != FsSaveDataType_Account) {
        resolve_for_uid(nullptr);
        return;
    }

    if (accounts->empty()) {
        App::Push<OptionBox>("No user accounts found on this console."_i18n, "OK"_i18n);
        return;
    }

    // shared_uid: null = ask for every save; empty = ask once now; set = the user already answered.
    if (shared_uid && shared_uid->has_value()) {
        resolve_for_uid(&shared_uid->value());
        return;
    }

    auto items = AccountPickerItems(*accounts);

    const std::string prompt = shared_uid ? "Restore for user"_i18n : BackupPickerTitle(current_seed, *accounts);
    if (shared_uid) {
        items.emplace_back("Choose for each save"_i18n);
    }

    auto popup = std::make_unique<PopupList>(prompt, items, [this, resolve_for_uid, seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, return_to_actions, shared_uid](auto op_index) {
        if (!op_index) {
            return;
        }
        if (shared_uid && *op_index == static_cast<s64>(accounts->size())) {
            PromptBatchRestoreTargets(seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, return_to_actions);
            return;
        }
        if (*op_index >= static_cast<s64>(accounts->size())) {
            return;
        }
        const auto chosen_uid = (*accounts)[*op_index].uid;
        if (shared_uid) {
            *shared_uid = chosen_uid;
        }
        resolve_for_uid(&chosen_uid);
    }, AccountIndexOf(current_seed.uid, *accounts));
    if (return_to_actions) {
        auto* raw = popup.get();
        popup->SetAction(Button::B, Action{"Back"_i18n, [this, raw, seeds]() {
            raw->SetPop();
            PromptBackupGroupAction(*seeds);
        }});
    }
    App::Push(std::move(popup));
}

} // namespace sphaira::ui::menu::save
