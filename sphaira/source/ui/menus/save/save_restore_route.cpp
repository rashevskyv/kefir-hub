#include "ui/menus/save_menu.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "haze.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_slot_backend.hpp"
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

auto Menu::MakeBackupGroupFromLiveEntry(const Entry& live, const dump::DumpLocation& location, const fs::FsPath& backup_root) const -> Entry {
    Entry group{};
    group.application_id = live.application_id;
    group.system_save_data_id = live.system_save_data_id;
    group.save_data_type = live.save_data_type;
    group.uid = live.uid;
    group.save_data_index = live.save_data_index;
    group.save_data_rank = live.save_data_rank;
    group.backup_rank_known = true;
    group.is_backup = true;
    if (!IsSystemLikeSave(group.save_data_type)) {
        group.dbi_game_dir = BuildDbiGameFolderName(live).s;
    }
    if (live.GetName() && live.GetName()[0] != '\0') {
        std::strncpy(group.lang.name, live.GetName(), sizeof(group.lang.name) - 1);
        group.lang.name[sizeof(group.lang.name) - 1] = '\0';
    }
    group.image = live.image;

    fs::FsStdio stdio_fs;
    fs::FsNativeSd sd_fs;
    fs::Fs* probe_fs = (location.entry.type == dump::DumpLocationType_Stdio || backup_root.starts_with("ums"))
        ? static_cast<fs::Fs*>(&stdio_fs)
        : static_cast<fs::Fs*>(&sd_fs);

    const auto candidates = CollectBackups(probe_fs, live, backup_root);
    const auto target_key = BackupGroupKey(group);

    std::vector<BackupCandidate> retained;
    std::unordered_set<std::string> seen_paths;
    for (const auto& c : candidates) {
        if (c.path.empty() || !seen_paths.insert(c.path.s).second) {
            continue;
        }
        const auto slash = std::strrchr(c.path.s, '/');
        const auto fname = slash ? (slash + 1) : c.path.s;
        BackupArchiveInfo info{};
        bool inspected = c.is_directory
            ? InspectBackupFolder(probe_fs, c.path, fname, group.dbi_game_dir, info)
            : InspectBackupArchive(probe_fs, c.path, fname, group.dbi_game_dir, info);
        if (inspected && BackupGroupKey(info) == target_key) {
            retained.emplace_back(c);
        }
    }

    std::ranges::sort(retained, [](const BackupCandidate& a, const BackupCandidate& b) {
        if (a.ts != b.ts) return a.ts > b.ts;
        if (a.source != b.source) return a.source < b.source;
        return a.path.toString() < b.path.toString();
    });

    group.backup_members = std::move(retained);
    group.backup_count = group.backup_members.size();
    if (!group.backup_members.empty()) {
        group.backup_path = group.backup_members.front().path;
        group.backup_timestamp = group.backup_members.front().ts;
    }
    return group;
}

auto Menu::MakeBackupGroupFromLiveEntry(const Entry& live, const fs::FsPath& backup_root) const -> Entry {
    return MakeBackupGroupFromLiveEntry(live, MakeSdCardDumpLocation(), backup_root);
}

void Menu::StartRestore(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    entries = ExpandGameGroups(entries);
    if (entries.empty()) {
        return;
    }

    if (m_category == Category::Backups || entries.front().is_backup) {
        RestoreBackupGroups(std::move(entries), false, location, backup_root);
        return;
    }

    if (entries.size() == 1 && App::GetSaveRestoreIncludeRemote()) {
        const auto webdav_locations = GetWebdavLocations();
        if (!webdav_locations.empty()) {
            Entry e = entries.front();
            const auto run = [this, e, location, backup_root](const location::Entry& loc) {
                auto downloaded = std::make_shared<std::vector<std::string>>();
                App::Push<ProgressBox>(0, "Syncing saves..."_i18n, "",
                    [this, e, loc, location, backup_root, downloaded](auto pbox) mutable -> Result {
                        pbox->SetHideSpeed(true);
                        return DownloadRemoteBackupsForEntry(pbox, loc, location, e, backup_root, downloaded.get());
                    },
                    [this, e, location, backup_root](Result rc) mutable {
                        if (R_FAILED(rc)) {
                            App::PushErrorBox(rc, "Sync failed!"_i18n);
                        }
                        auto group = MakeBackupGroupFromLiveEntry(e, location, backup_root);
                        RestoreBackupGroups({std::move(group)}, false, location, backup_root);
                    });
            };

            if (webdav_locations.size() == 1) {
                run(webdav_locations.front());
                return;
            } else {
                PopupList::Items items;
                for (const auto& loc : webdav_locations) {
                    std::string proto = loc.protocol;
                    if (proto.empty()) {
                        if (loc.url.starts_with("webdav://") || loc.url.starts_with("webdavs://")) proto = "webdav";
                        else if (loc.url.starts_with("http://") || loc.url.starts_with("https://")) proto = "webdav";
                    }
                    std::string proto_upper = proto;
                    std::transform(proto_upper.begin(), proto_upper.end(), proto_upper.begin(), ::toupper);
                    items.emplace_back(loc.name + " (" + proto_upper + ")");
                }
                App::Push<PopupList>("Select Sync Location"_i18n, items, [webdav_locations, run](auto op_index) {
                    if (op_index) {
                        run(webdav_locations[*op_index]);
                    }
                });
                return;
            }
        }
    }

    std::vector<Entry> groups;
    groups.reserve(entries.size());
    for (const auto& e : entries) {
        groups.emplace_back(MakeBackupGroupFromLiveEntry(e, location, backup_root));
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

namespace {

void PickArchiveFromCandidates(const std::vector<BackupCandidate>& candidates, std::function<void(std::optional<fs::FsPath>)> cb) {
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

} // namespace

void PlanRestoreCreation(
    const Entry& group,
    const AccountUid& dest_uid,
    const fs::FsPath& archive_path,
    std::function<void(std::optional<Entry>)> cb) {

    if (group.save_data_type != FsSaveDataType_Account) {
        App::Push<OptionBox>("Save slot creation is only supported for Account saves."_i18n, "OK"_i18n);
        cb(std::nullopt); return;
    }
    if (group.save_data_rank != FsSaveDataRank_Primary || group.save_data_index != 0) {
        App::Push<OptionBox>("Save slot creation is only supported for primary save slots."_i18n, "OK"_i18n);
        cb(std::nullopt); return;
    }
    if (group.application_id == 0) {
        App::Push<OptionBox>("Save slot creation is only supported for installed titles."_i18n, "OK"_i18n);
        cb(std::nullopt); return;
    }

    struct PlanContext {
        SaveArchiveAdmissionResult admission{};
        SaveCreationRequest req{};
        SaveBackendStatus status{SaveBackendStatus::Success};
        Result plan_rc{0};
        bool reinspect_ok{false};
        bool is_folder{false};
        bool folder_meta_missing{false};
    };
    auto ctx = std::make_shared<PlanContext>();

    App::Push<ProgressBox>(0, "Checking backup..."_i18n, "",
        [archive_path, group, dest_uid, ctx](auto pbox) -> Result {
            pbox->SetHideSpeed(true);
            pbox->SetTransfer("Verifying archive..."_i18n);

            fs::FsStdio stdio_fs;
            fs::FsNativeSd sd_fs;
            fs::Fs* probe_fs = archive_path.starts_with("ums") ? static_cast<fs::Fs*>(&stdio_fs) : static_cast<fs::Fs*>(&sd_fs);

            FsDirEntryType ptype{};
            ctx->is_folder = (R_SUCCEEDED(probe_fs->GetEntryType(archive_path, &ptype)) && ptype == FsDirEntryType_Dir);

            if (ctx->is_folder) {
                ctx->admission = InspectSaveFolderAdmission(archive_path, pbox, false);
                if (!ctx->admission.admitted) return ctx->admission.rc ? ctx->admission.rc : FsError_PathNotFound;
                if (!ctx->admission.sizing.has_metadata) {
                    ctx->folder_meta_missing = true;
                    return 0;
                }
            } else {
                ctx->admission = InspectSaveArchiveAdmission(archive_path, pbox, false);
                if (!ctx->admission.admitted) return ctx->admission.rc ? ctx->admission.rc : FsError_PathNotFound;
            }
            if (pbox->ShouldExit()) return Result_TransferCancelled;

            ctx->plan_rc = PlanAccountSaveCreation(
                group.application_id, dest_uid,
                &ctx->admission.sizing,
                ctx->req, &ctx->status);
            if (R_FAILED(ctx->plan_rc) || ctx->status != SaveBackendStatus::Success) {
                return ctx->plan_rc ? ctx->plan_rc : FsError_InvalidSize;
            }

            const char* filename = std::strrchr(archive_path.s, '/');
            filename = filename ? filename + 1 : archive_path.s;
            BackupArchiveInfo check_info{};
            bool reinspect_ok = false;
            if (ctx->is_folder) {
                reinspect_ok = InspectBackupFolder(probe_fs, archive_path, filename, group.dbi_game_dir, check_info);
            } else {
                reinspect_ok = InspectBackupArchive(probe_fs, archive_path, filename, group.dbi_game_dir, check_info);
            }
            if (reinspect_ok && BackupGroupKey(check_info) == BackupGroupKey(group)) {
                ctx->reinspect_ok = true;
            }
            return 0;
        },
        [group, ctx, cb](Result rc) mutable {
            if (rc == Result_TransferCancelled) {
                cb(std::nullopt);
                return;
            }
            if (ctx->folder_meta_missing) {
                App::Push<OptionBox>("Backup folder metadata is missing or incomplete for save slot creation."_i18n, "OK"_i18n);
                cb(std::nullopt); return;
            }
            if (!ctx->admission.admitted) {
                App::Push<OptionBox>("Invalid or corrupt save backup archive."_i18n, "OK"_i18n);
                cb(std::nullopt); return;
            }
            if (R_FAILED(ctx->plan_rc) || ctx->status != SaveBackendStatus::Success) {
                App::Push<OptionBox>(GetBackendStatusMessage(ctx->status), "OK"_i18n);
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
        fs::FsStdio stdio_fs;
        fs::FsNativeSd sd_fs;
        fs::Fs* probe_fs = (location.entry.type == dump::DumpLocationType_Stdio || backup_root.starts_with("ums"))
            ? static_cast<fs::Fs*>(&stdio_fs)
            : static_cast<fs::Fs*>(&sd_fs);
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

        PopupList::Items items;
        for (const auto& acc : accounts) {
            items.emplace_back(acc.nickname);
        }

        auto popup = std::make_unique<PopupList>("Restore for user"_i18n, items, [this, group, accounts, location, backup_root](auto op_index) mutable {
            if (!op_index || *op_index >= static_cast<s64>(accounts.size())) {
                return;
            }
            const auto chosen_uid = accounts[*op_index].uid;
            RestoreSingleBackupGroup(std::move(group), &chosen_uid, false, location, backup_root);
        });
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
    if (groups.empty()) {
        return;
    }

    fs::FsStdio stdio_fs;
    fs::FsNativeSd sd_fs;
    fs::Fs* probe_fs = (location.entry.type == dump::DumpLocationType_Stdio || backup_root.starts_with("ums"))
        ? static_cast<fs::Fs*>(&stdio_fs)
        : static_cast<fs::Fs*>(&sd_fs);

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

    if (groups.size() == 1) {
        RestoreSingleBackupGroup(std::move(groups.front()), nullptr, force_user_picker, location, backup_root, return_to_actions);
        return;
    }

    const auto accounts = App::GetAccountList();
    auto resolved_targets = std::make_shared<std::vector<Entry>>(groups.size());
    auto seen_target_keys = std::make_shared<std::set<std::string>>();
    PromptBatchRestoreTargets(std::make_shared<std::vector<Entry>>(std::move(groups)), 0,
        std::make_shared<std::vector<AccountProfileBase>>(accounts), resolved_targets, seen_target_keys,
        location, backup_root, return_to_actions);
}

void Menu::RestoreBackupGroups(std::vector<Entry> groups, bool force_user_picker, bool return_to_actions) {
    RestoreBackupGroups(std::move(groups), force_user_picker, MakeSdCardDumpLocation(), DEFAULT_BACKUP_ROOT, return_to_actions);
}

void Menu::PromptBatchRestoreTargets(
    std::shared_ptr<std::vector<Entry>> seeds,
    size_t step,
    std::shared_ptr<std::vector<AccountProfileBase>> accounts,
    std::shared_ptr<std::vector<Entry>> resolved_targets,
    std::shared_ptr<std::set<std::string>> seen_target_keys,
    const dump::DumpLocation& location,
    const fs::FsPath& backup_root,
    bool return_to_actions) {

    if (step >= seeds->size()) {
        RestoreSaves(std::move(*seeds), std::move(*resolved_targets), location, backup_root);
        return;
    }

    const auto on_target_resolved = [this, seeds, step, accounts, resolved_targets, seen_target_keys, location, backup_root, return_to_actions](std::optional<Entry> target) {
        if (!target) {
            return;
        }

        const auto key = SaveEntryKey(*target);
        if (!seen_target_keys->insert(key).second) {
            App::Push<OptionBox>("Duplicate restore target slot selected."_i18n, "OK"_i18n);
            return;
        }

        (*resolved_targets)[step] = std::move(*target);
        PromptBatchRestoreTargets(seeds, step + 1, accounts, resolved_targets, seen_target_keys, location, backup_root, return_to_actions);
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

    PopupList::Items items;
    for (const auto& acc : *accounts) {
        items.emplace_back(acc.nickname);
    }

    std::string prompt = "Restore for user"_i18n;
    if (current_seed.GetName() && current_seed.GetName()[0] != '\0') {
        prompt += " (" + std::string(current_seed.GetName()) + ")";
    }

    auto popup = std::make_unique<PopupList>(prompt, items, [resolve_for_uid, accounts](auto op_index) {
        if (!op_index || *op_index >= static_cast<s64>(accounts->size())) {
            return;
        }
        const auto chosen_uid = (*accounts)[*op_index].uid;
        resolve_for_uid(&chosen_uid);
    });
    if (return_to_actions) {
        auto* raw = popup.get();
        popup->SetAction(Button::B, Action{"Back"_i18n, [this, raw, seeds]() {
            raw->SetPop();
            PromptBackupGroupAction(*seeds);
        }});
    }
    App::Push(std::move(popup));
}

void Menu::PromptBatchRestoreTargets(
    std::vector<Entry> seeds, size_t step, std::vector<AccountProfileBase> accounts,
    std::shared_ptr<std::vector<Entry>> resolved_targets, std::shared_ptr<std::set<std::string>> seen_target_keys,
    const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    PromptBatchRestoreTargets(std::make_shared<std::vector<Entry>>(std::move(seeds)), step,
        std::make_shared<std::vector<AccountProfileBase>>(std::move(accounts)),
        resolved_targets, seen_target_keys, location, backup_root);
}

void Menu::PromptBatchRestoreTargets(
    std::shared_ptr<std::vector<Entry>> seeds, size_t step, std::shared_ptr<std::vector<AccountProfileBase>> accounts,
    std::shared_ptr<std::vector<Entry>> resolved_targets, std::shared_ptr<std::set<std::string>> seen_target_keys) {
    PromptBatchRestoreTargets(seeds, step, accounts, resolved_targets, seen_target_keys, MakeSdCardDumpLocation(), DEFAULT_BACKUP_ROOT);
}

void Menu::PromptBatchRestoreTargets(
    std::vector<Entry> seeds, size_t step, std::vector<AccountProfileBase> accounts,
    std::shared_ptr<std::vector<Entry>> resolved_targets, std::shared_ptr<std::set<std::string>> seen_target_keys) {
    PromptBatchRestoreTargets(std::move(seeds), step, std::move(accounts), resolved_targets, seen_target_keys, MakeSdCardDumpLocation(), DEFAULT_BACKUP_ROOT);
}

} // namespace sphaira::ui::menu::save
