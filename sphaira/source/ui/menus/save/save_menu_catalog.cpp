#include "fs.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_folder_discovery.hpp"
#include "ui/menus/save/save_bundle_util.hpp"
#include "ui/menus/save_list_info.hpp"
#include "path_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sphaira::ui::menu::save {

void Menu::ReadBackupEntries(std::vector<Entry>& out, const BackupScanProgress& progress, fs::Fs* custom_fs, const fs::FsPath& custom_root) const {
    fs::FsNativeSd sd_fs;
    fs::Fs* probe_fs = custom_fs ? custom_fs : static_cast<fs::Fs*>(&sd_fs);
    struct GroupScanMeta {
        size_t index{};
        int rep_source{};
    };
    std::unordered_map<std::string, GroupScanMeta> group_map;
    std::vector<Entry> groups;
    std::unordered_set<std::string> seen_paths;

    const auto process_candidate = [&](const fs::FsPath& path, std::string_view name, std::string_view dbi_game_dir_name, int source_prio, bool is_dir) {
        std::string lower_path = path.s;
        for (char& c : lower_path) {
            c = std::tolower(static_cast<unsigned char>(c));
        }
        if (!seen_paths.insert(lower_path).second) {
            return;
        }

        BackupArchiveInfo info{};
        bool ok = false;
        if (is_dir) {
            ok = InspectBackupFolder(probe_fs, path, name, dbi_game_dir_name, info);
        } else {
            ok = InspectBackupArchive(probe_fs, path, name, dbi_game_dir_name, info);
        }
        if (!ok) {
            return;
        }

        info.source = source_prio;
        const auto key = std::to_string(static_cast<int>(info.backup_source)) + ":" + BackupGroupKey(info);
        auto it = group_map.find(key);
        if (it == group_map.end()) {
            Entry e{};
            e.application_id = info.application_id;
            e.system_save_data_id = info.system_save_data_id;
            e.save_data_type = info.save_data_type;
            e.save_types_mask = SaveTypeToMask(info.save_data_type);
            if (info.source_space.has_value()) {
                e.save_data_space_id = *info.source_space;
                e.backup_space_known = true;
            }
            e.uid = info.uid;
            e.save_data_index = info.save_data_index;
            e.save_data_rank = info.save_data_rank;
            e.backup_rank_known = info.rank_known;
            e.is_backup = true;
            e.backup_source = info.backup_source;
            e.backup_timestamp = info.timestamp;
            e.backup_count = 1;
            e.backup_path = path;
            e.backup_is_directory = is_dir;
            e.dbi_game_dir = info.dbi_game_dir;
            e.backup_owner_name = info.owner_name;
            e.source_timestamp = info.source_timestamp;
            e.commit_id = info.commit_id;
            e.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio, is_dir});

            if (IsSystemLikeSave(e.save_data_type)) {
                detail::FakeNacpEntryForSystem(e);
            } else if (!e.dbi_game_dir.empty() && !IsHex16(e.dbi_game_dir)) {
                std::strncpy(e.lang.name, e.dbi_game_dir.c_str(), sizeof(e.lang.name) - 1);
                e.lang.name[sizeof(e.lang.name) - 1] = '\0';
            }

            group_map.emplace(key, GroupScanMeta{groups.size(), source_prio});
            groups.emplace_back(std::move(e));
        } else {
            auto& meta = it->second;
            auto& existing = groups[meta.index];
            if (is_dir) {
                existing.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio, true});
            } else {
                existing.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio});
            }
            existing.backup_count = existing.backup_members.size();

            bool is_newer = false;
            if (info.timestamp != existing.backup_timestamp) {
                is_newer = info.timestamp > existing.backup_timestamp;
            } else if (source_prio != meta.rep_source) {
                is_newer = source_prio < meta.rep_source;
            } else {
                is_newer = (path.toString() < existing.backup_path.toString());
            }

            if (is_newer) {
                existing.backup_timestamp = info.timestamp;
                existing.backup_path = path;
                existing.backup_is_directory = is_dir;
                existing.source_timestamp = info.source_timestamp;
                existing.commit_id = info.commit_id;
                meta.rep_source = source_prio;
                if (info.source_space.has_value()) {
                    existing.save_data_space_id = *info.source_space;
                    existing.backup_space_known = true;
                }
            }

            if (existing.backup_owner_name.empty()) {
                existing.backup_owner_name = info.owner_name;
            }
            if (existing.dbi_game_dir.empty() && !info.dbi_game_dir.empty()) {
                existing.dbi_game_dir = info.dbi_game_dir;
                if (existing.lang.name[0] == '\0' && !IsHex16(existing.dbi_game_dir)) {
                    std::strncpy(existing.lang.name, existing.dbi_game_dir.c_str(), sizeof(existing.lang.name) - 1);
                    existing.lang.name[sizeof(existing.lang.name) - 1] = '\0';
                }
            }
        }
    };

    // the walk below only lists folders; candidates are opened afterwards so the progress can
    // show "N / total" for the slow part (each archive is opened and its metadata read).
    struct Candidate {
        fs::FsPath path;
        std::string name;
        std::string dbi_game_dir;
        int source_prio;
        bool is_dir;
    };
    std::vector<Candidate> candidates;

    const auto process_archive = [&](const fs::FsPath& path, std::string_view filename, std::string_view dbi_game_dir_name, int source_prio) {
        candidates.emplace_back(Candidate{path, std::string(filename), std::string(dbi_game_dir_name), source_prio, false});
    };

    const auto process_folder = [&](const fs::FsPath& path, std::string_view folder_name, std::string_view dbi_game_dir_name, int source_prio) {
        candidates.emplace_back(Candidate{path, std::string(folder_name), std::string(dbi_game_dir_name), source_prio, true});
    };

    // 1. Scan DBI-format game backups: /switch/DBI/saves, /DBISaves, and custom paths
    const auto scan_dbi_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto dbi_root = fs::AppendPath(probe_fs->Root(), root_path);
        filebrowser::FsDirCollection games{};
        filebrowser::FsView::get_collection(probe_fs, dbi_root, "", games, false, true, false);
        for (const auto& game : games.dirs) {
            const auto game_dir = fs::AppendPath(dbi_root, game.name);
            filebrowser::FsDirCollection dates{};
            filebrowser::FsView::get_collection(probe_fs, game_dir, "", dates, true, true, false);
            for (const auto& file : dates.files) {
                process_archive(fs::AppendPath(dates.path, file.name), file.name, game.name, prio_base);
            }
            for (const auto& date : dates.dirs) {
                filebrowser::FsDirCollection files{};
                filebrowser::FsView::get_collection(probe_fs, fs::AppendPath(game_dir, date.name), "", files, true, false, false);
                for (const auto& file : files.files) {
                    process_archive(fs::AppendPath(files.path, file.name), file.name, game.name, prio_base);
                }
            }
        }
    };

    // 2. Scan Sphaira root (/dumps) and custom search paths
    const auto scan_sphaira_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto root = fs::AppendPath(probe_fs->Root(), root_path);
        filebrowser::FsDirCollection l1{};
        filebrowser::FsView::get_collection(probe_fs, root, "", l1, true, true, false);
        for (const auto& file : l1.files) {
            process_archive(fs::AppendPath(l1.path, file.name), file.name, "", prio_base);
        }
        for (const auto& dir1 : l1.dirs) {
            const auto dir1_path = fs::AppendPath(root, dir1.name);
            filebrowser::FsDirCollection l2{};
            filebrowser::FsView::get_collection(probe_fs, dir1_path, "", l2, true, true, false);
            for (const auto& file : l2.files) {
                process_archive(fs::AppendPath(l2.path, file.name), file.name, "", prio_base + 1);
            }
            for (const auto& dir2 : l2.dirs) {
                const auto dir2_path = fs::AppendPath(dir1_path, dir2.name);
                filebrowser::FsDirCollection l3{};
                filebrowser::FsView::get_collection(probe_fs, dir2_path, "", l3, true, false, false);
                for (const auto& file : l3.files) {
                    process_archive(fs::AppendPath(l3.path, file.name), file.name, "", prio_base + 2);
                }
            }
        }
    };

    // 3. Scan JKSV folder backups: /JKSV and /switch/JKSV
    const auto scan_jksv_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto jksv_root = fs::AppendPath(probe_fs->Root(), root_path);
        filebrowser::FsDirCollection l1{};
        filebrowser::FsView::get_collection(probe_fs, jksv_root, "", l1, false, true, false);
        for (const auto& d1 : l1.dirs) {
            const auto d1_path = fs::AppendPath(jksv_root, d1.name);
            const bool is_category = path::EqualsIC(d1.name, "Device Saves") ||
                                     path::EqualsIC(d1.name, "System Saves") ||
                                     path::EqualsIC(d1.name, "BCAT Saves");
            if (is_category) {
                filebrowser::FsDirCollection games{};
                filebrowser::FsView::get_collection(probe_fs, d1_path, "", games, false, true, false);
                for (const auto& game : games.dirs) {
                    const auto game_dir = fs::AppendPath(d1_path, game.name);
                    filebrowser::FsDirCollection backups{};
                    filebrowser::FsView::get_collection(probe_fs, game_dir, "", backups, false, true, false);
                    for (const auto& b : backups.dirs) {
                        process_folder(fs::AppendPath(game_dir, b.name), b.name, game.name, prio_base + 2);
                    }
                }
            } else {
                filebrowser::FsDirCollection backups{};
                filebrowser::FsView::get_collection(probe_fs, d1_path, "", backups, false, true, false);
                for (const auto& b : backups.dirs) {
                    process_folder(fs::AppendPath(d1_path, b.name), b.name, d1.name, prio_base + 1);
                }
            }
        }
    };

    // 4. Scan Checkpoint folder backups: /switch/Checkpoint/saves and /Checkpoint/saves
    const auto scan_checkpoint_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto cp_root = fs::AppendPath(probe_fs->Root(), root_path);
        filebrowser::FsDirCollection games{};
        filebrowser::FsView::get_collection(probe_fs, cp_root, "", games, false, true, false);
        for (const auto& game : games.dirs) {
            const auto game_dir = fs::AppendPath(cp_root, game.name);
            filebrowser::FsDirCollection backups{};
            filebrowser::FsView::get_collection(probe_fs, game_dir, "", backups, false, true, false);
            for (const auto& b : backups.dirs) {
                process_folder(fs::AppendPath(game_dir, b.name), b.name, game.name, prio_base + 1);
            }
        }
    };

    // 5. Scan custom search paths for folder backups
    const auto scan_custom_folder_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto root = fs::AppendPath(probe_fs->Root(), root_path);
        filebrowser::FsDirCollection l1{};
        filebrowser::FsView::get_collection(probe_fs, root, "", l1, false, true, false);
        for (const auto& d1 : l1.dirs) {
            const auto d1_path = fs::AppendPath(root, d1.name);
            filebrowser::FsDirCollection l2{};
            filebrowser::FsView::get_collection(probe_fs, d1_path, "", l2, false, true, false);
            if (!l2.dirs.empty()) {
                for (const auto& d2 : l2.dirs) {
                    process_folder(fs::AppendPath(d1_path, d2.name), d2.name, d1.name, prio_base + 2);
                }
            } else {
                process_folder(d1_path, d1.name, "", prio_base + 1);
            }
        }
    };

    if (custom_fs) {
        if (!custom_root.empty()) {
            scan_dbi_root(custom_root, 0);
            scan_sphaira_root(custom_root, 1);
        }
    } else {
        scan_dbi_root(fs::FsPath{GetDbiSavesPath()}, 0);
        scan_dbi_root(fs::FsPath{DBI_SAVES_ROOT_PATH}, 0);
        for (const auto& custom_path_str : GetBackupSearchPaths()) {
            scan_dbi_root(fs::FsPath{custom_path_str}, 10);
        }

        scan_sphaira_root(fs::FsPath{DEFAULT_BACKUP_ROOT}, 1);
        int custom_prio = 10;
        for (const auto& custom_path_str : GetBackupSearchPaths()) {
            scan_sphaira_root(fs::FsPath{custom_path_str}, custom_prio);
            custom_prio += 5;
        }

        scan_jksv_root(fs::FsPath{JKSV_PATH}, 2);
        scan_jksv_root(fs::FsPath{JKSV_SWITCH_PATH}, 2);
        scan_checkpoint_root(fs::FsPath{CHECKPOINT_SAVES_PATH}, 3);
        scan_checkpoint_root(fs::FsPath{CHECKPOINT_ROOT_SAVES_PATH}, 3);

        int folder_custom_prio = 10;
        for (const auto& custom_path_str : GetBackupSearchPaths()) {
            scan_custom_folder_root(fs::FsPath{custom_path_str}, folder_custom_prio);
            folder_custom_prio += 5;
        }
    }

    for (size_t i = 0; i < candidates.size(); i++) {
        const auto& c = candidates[i];
        if (progress && !progress(i, candidates.size(), c.path)) {
            break;
        }
        process_candidate(c.path, c.name, c.dbi_game_dir, c.source_prio, c.is_dir);
    }

    // Sort backups by source precedence first, then newest first
    std::ranges::sort(groups, [](const Entry& a, const Entry& b) {
        if (a.backup_source != b.backup_source) {
            return static_cast<u8>(a.backup_source) < static_cast<u8>(b.backup_source);
        }
        if (a.backup_timestamp != b.backup_timestamp) {
            return a.backup_timestamp > b.backup_timestamp;
        }
        return a.application_id < b.application_id;
    });

    for (auto& g : groups) {
        std::ranges::sort(g.backup_members, [](const BackupCandidate& a, const BackupCandidate& b) {
            if (a.ts != b.ts) {
                return a.ts > b.ts;
            }
            if (a.source != b.source) {
                return a.source < b.source;
            }
            return a.path.toString() < b.path.toString();
        });
        out.emplace_back(std::move(g));
    }
}

void Menu::Sort() {
    const auto sort = m_sort.Get();
    const auto order = m_order.Get();

    if (sort != SortType_Updated) {
        // names of installed games are loaded lazily by Draw; sorting needs them now.
        for (auto& e : m_entries) {
            if (!e.is_backup && m_installed_app_ids.contains(e.application_id)) {
                detail::LoadControlEntry(e);
            }
        }
        // descending = A to Z / biggest first, as in the games menu. backups have no size and
        // fall back to the name.
        const auto cmp = [sort, order](const Entry& a, const Entry& b) {
            if (sort == SortType_Size && a.sort_size != b.sort_size) {
                return order == OrderType_Descending ? a.sort_size > b.sort_size : a.sort_size < b.sort_size;
            }
            const auto r = strcasecmp(a.GetName(), b.GetName());
            return order == OrderType_Descending ? r < 0 : r > 0;
        };
        // live saves and every backup section are sorted on their own so the sections stay apart.
        const auto mid = std::clamp<s64>(m_backup_start, 0, static_cast<s64>(m_entries.size()));
        std::stable_sort(m_entries.begin(), m_entries.begin() + mid, cmp);
        if (m_category == Category::Backups) {
            for (const auto& sec : ComputeGridSections().sections) {
                std::stable_sort(m_entries.begin() + sec.entry_start, m_entries.begin() + sec.entry_start + sec.entry_count, cmp);
            }
        } else {
            std::stable_sort(m_entries.begin() + mid, m_entries.end(), cmp);
        }
        return;
    }

    const bool want_reversed = order == OrderType_Ascending;

    if (want_reversed != m_is_reversed) {
        if (m_category == Category::Backups) {
            const auto g = ComputeGridSections();
            for (const auto& sec : g.sections) {
                if (sec.entry_count > 1) {
                    std::reverse(m_entries.begin() + sec.entry_start,
                                 m_entries.begin() + sec.entry_start + sec.entry_count);
                }
            }
        } else {
            // reverse the live-save and backup sections independently so the two
            // stay partitioned (live first, backups after) regardless of order.
            const auto mid = std::clamp<s64>(m_backup_start, 0, static_cast<s64>(m_entries.size()));
            std::reverse(m_entries.begin(), m_entries.begin() + mid);
            std::reverse(m_entries.begin() + mid, m_entries.end());
        }
        m_is_reversed = want_reversed;
    }
}

void Menu::SortAndFindLastFile(bool scan) {
    if (m_entries.empty()) {
        if (scan) {
            ScanHomebrew();
        } else {
            Sort();
            SetIndex(0);
        }
        return;
    }

    const auto last_key = DisplayEntryKey(m_entries[m_index]);
    if (scan) {
        ScanHomebrew();
    } else {
        Sort();
    }
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries.size(); i++) {
        if (last_key == DisplayEntryKey(m_entries[i])) {
            index = i;
            break;
        }
    }

    if (index >= 0) {
        const auto g = ComputeGridSections();
        const auto disp = EntryToDisplay(index, g);
        const auto row = m_list->GetRow();
        const auto page = m_list->GetPage();
        // guesstimate where the position is (in display slots, which include
        // the divider gap between live saves and backups).
        if (disp >= page) {
            m_list->SetYoff((((disp - page) + row) / row) * m_list->GetMaxY());
        } else {
            m_list->SetYoff(0);
        }
        SetIndex(index);
    }
}

auto Menu::CollectGroupArchives(fs::Fs* fs, const Entry& group, const fs::FsPath& backup_root) const -> std::vector<BackupCandidate> {
    if (group.is_backup && !group.backup_members.empty()) {
        std::unordered_set<std::string> seen_paths;
        std::vector<BackupCandidate> out;

        for (const auto& m : group.backup_members) {
            if (m.path.empty() || !seen_paths.insert(m.path.s).second) {
                continue;
            }

            const auto slash = std::strrchr(m.path.s, '/');
            const auto fname = slash ? (slash + 1) : m.path.s;
            BackupArchiveInfo info{};
            const bool inspected = m.is_directory
                ? InspectBackupFolder(fs, m.path, fname, group.dbi_game_dir, info)
                : InspectBackupArchive(fs, m.path, fname, group.dbi_game_dir, info);
            if (!inspected) {
                continue;
            }

            info.source = m.source;
            if (!MatchesSelectedBackup(info, group)) {
                continue;
            }

            out.emplace_back(BackupCandidate{info.timestamp, m.path, m.source, m.is_directory});
        }

        std::ranges::sort(out, [](const BackupCandidate& a, const BackupCandidate& b) {
            if (a.ts != b.ts) {
                return a.ts > b.ts;
            }
            if (a.source != b.source) {
                return a.source < b.source;
            }
            return a.path.toString() < b.path.toString();
        });

        return out;
    }

    const auto candidates = CollectBackups(fs, group, backup_root);
    const bool group_space_known = group.is_backup ? group.backup_space_known : true;
    const bool group_rank_known = group.is_backup ? group.backup_rank_known : true;

    std::vector<BackupCandidate> out;
    for (const auto& c : candidates) {
        const auto slash = std::strrchr(c.path.s, '/');
        const auto fname = slash ? (slash + 1) : c.path.s;
        BackupArchiveInfo info{};
        const bool inspected = c.is_directory
            ? InspectBackupFolder(fs, c.path, fname, group.dbi_game_dir, info)
            : InspectBackupArchive(fs, c.path, fname, group.dbi_game_dir, info);
        if (!inspected) {
            continue;
        }

        if (group.is_backup) {
            if (MatchesSelectedBackup(info, group)) {
                out.emplace_back(c);
            }
        } else {
            if (bundle::AreBackupSlotIdentitiesCompatible(
                    IsSystemLikeSave(group.save_data_type), group.save_data_type,
                    IsSystemLikeSave(group.save_data_type) ? group.system_save_data_id : group.application_id,
                    group_space_known, group.save_data_space_id,
                    group.uid.uid, group.save_data_index, group_rank_known, group.save_data_rank,
                    IsSystemLikeSave(info.save_data_type), info.save_data_type,
                    IsSystemLikeSave(info.save_data_type) ? info.system_save_data_id : info.application_id,
                    info.source_space.has_value(), info.source_space.value_or(0),
                    info.uid.uid, info.save_data_index, info.rank_known, info.save_data_rank)) {
                out.emplace_back(c);
            }
        }
    }

    if (out.empty() && !group.backup_path.empty()) {
        out.emplace_back(BackupCandidate{group.backup_timestamp, group.backup_path, 0, group.backup_is_directory});
    }

    return out;
}

void Menu::ReadBackupNames(std::unordered_map<u64, std::string>& out) const {
    fs::FsNativeSd fs;
    // <root>/<Game name>/[<date>/]<TID>_<type>_….zip: the game folder gives the name, the first
    // archive name the title id. two listings per game; nothing is opened.
    const auto scan_root = [&](const fs::FsPath& root_path) {
        const auto root = fs::AppendPath(fs.Root(), root_path);
        filebrowser::FsDirCollection games{};
        filebrowser::FsView::get_collection(&fs, root, "", games, false, true, false);
        for (const auto& game : games.dirs) {
            if (IsHex16(game.name)) {
                continue;
            }
            const auto game_dir = fs::AppendPath(root, game.name);
            filebrowser::FsDirCollection dates{};
            filebrowser::FsView::get_collection(&fs, game_dir, "", dates, true, true, false);
            const auto tid_of = [](const filebrowser::FsDirCollection& c) -> u64 {
                for (const auto& f : c.files) {
                    const std::string_view name = f.name;
                    if (name.size() > 16 && IsHex16(name.substr(0, 16))) {
                        return ParseHex16(name.substr(0, 16));
                    }
                }
                return 0;
            };
            u64 tid = tid_of(dates);
            if (!tid && !dates.dirs.empty()) {
                filebrowser::FsDirCollection files{};
                filebrowser::FsView::get_collection(&fs, fs::AppendPath(game_dir, dates.dirs.front().name), "", files, true, false, false);
                tid = tid_of(files);
            }
            if (tid) {
                out.try_emplace(tid, game.name);
            }
        }
    };

    scan_root(fs::FsPath{DEFAULT_BACKUP_ROOT});
    scan_root(fs::FsPath{GetDbiSavesPath()});
    scan_root(fs::FsPath{DBI_SAVES_ROOT_PATH});
}

} // namespace sphaira::ui::menu::save
