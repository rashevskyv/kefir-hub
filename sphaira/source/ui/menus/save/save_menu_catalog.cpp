#include "fs.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_folder_discovery.hpp"
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

void Menu::ReadBackupEntries(std::vector<Entry>& out) const {
    fs::FsNativeSd fs;
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
            ok = InspectBackupFolder(&fs, path, name, dbi_game_dir_name, info);
        } else {
            ok = InspectBackupArchive(&fs, path, name, dbi_game_dir_name, info);
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
            e.source_timestamp = info.source_timestamp;
            e.commit_id = info.commit_id;
            if (is_dir) {
                e.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio, true});
            } else {
                e.backup_members.emplace_back(BackupCandidate{info.timestamp, path, source_prio});
            }

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

    const auto process_archive = [&](const fs::FsPath& path, std::string_view filename, std::string_view dbi_game_dir_name, int source_prio) {
        process_candidate(path, filename, dbi_game_dir_name, source_prio, false);
    };

    const auto process_folder = [&](const fs::FsPath& path, std::string_view folder_name, std::string_view dbi_game_dir_name, int source_prio) {
        process_candidate(path, folder_name, dbi_game_dir_name, source_prio, true);
    };

    // 1. Scan DBI-format game backups: /switch/DBI/saves, /DBISaves, and custom paths
    const auto scan_dbi_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto dbi_root = fs::AppendPath(fs.Root(), root_path);
        filebrowser::FsDirCollection games{};
        filebrowser::FsView::get_collection(&fs, dbi_root, "", games, false, true, false);
        for (const auto& game : games.dirs) {
            const auto game_dir = fs::AppendPath(dbi_root, game.name);
            filebrowser::FsDirCollection dates{};
            filebrowser::FsView::get_collection(&fs, game_dir, "", dates, true, true, false);
            for (const auto& file : dates.files) {
                process_archive(fs::AppendPath(dates.path, file.name), file.name, game.name, prio_base);
            }
            for (const auto& date : dates.dirs) {
                filebrowser::FsDirCollection files{};
                filebrowser::FsView::get_collection(&fs, fs::AppendPath(game_dir, date.name), "", files, true, false, false);
                for (const auto& file : files.files) {
                    process_archive(fs::AppendPath(files.path, file.name), file.name, game.name, prio_base);
                }
            }
        }
    };

    scan_dbi_root(fs::FsPath{GetDbiSavesPath()}, 0);
    scan_dbi_root(fs::FsPath{DBI_SAVES_ROOT_PATH}, 0);
    for (const auto& custom_path_str : GetBackupSearchPaths()) {
        scan_dbi_root(fs::FsPath{custom_path_str}, 10);
    }

    // 2. Scan Sphaira root (/dumps) and custom search paths
    const auto scan_sphaira_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto root = fs::AppendPath(fs.Root(), root_path);
        filebrowser::FsDirCollection l1{};
        filebrowser::FsView::get_collection(&fs, root, "", l1, true, true, false);
        for (const auto& file : l1.files) {
            process_archive(fs::AppendPath(l1.path, file.name), file.name, "", prio_base);
        }
        for (const auto& dir1 : l1.dirs) {
            const auto dir1_path = fs::AppendPath(root, dir1.name);
            filebrowser::FsDirCollection l2{};
            filebrowser::FsView::get_collection(&fs, dir1_path, "", l2, true, true, false);
            for (const auto& file : l2.files) {
                process_archive(fs::AppendPath(l2.path, file.name), file.name, "", prio_base + 1);
            }
            for (const auto& dir2 : l2.dirs) {
                const auto dir2_path = fs::AppendPath(dir1_path, dir2.name);
                filebrowser::FsDirCollection l3{};
                filebrowser::FsView::get_collection(&fs, dir2_path, "", l3, true, false, false);
                for (const auto& file : l3.files) {
                    process_archive(fs::AppendPath(l3.path, file.name), file.name, "", prio_base + 2);
                }
            }
        }
    };

    scan_sphaira_root(fs::FsPath{DEFAULT_BACKUP_ROOT}, 1);

    int custom_prio = 10;
    for (const auto& custom_path_str : GetBackupSearchPaths()) {
        scan_sphaira_root(fs::FsPath{custom_path_str}, custom_prio);
        custom_prio += 5;
    }

    // 3. Scan JKSV folder backups: /JKSV and /switch/JKSV
    const auto scan_jksv_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto jksv_root = fs::AppendPath(fs.Root(), root_path);
        filebrowser::FsDirCollection l1{};
        filebrowser::FsView::get_collection(&fs, jksv_root, "", l1, false, true, false);
        for (const auto& d1 : l1.dirs) {
            const auto d1_path = fs::AppendPath(jksv_root, d1.name);
            const bool is_category = path::EqualsIC(d1.name, "Device Saves") ||
                                     path::EqualsIC(d1.name, "System Saves") ||
                                     path::EqualsIC(d1.name, "BCAT Saves");
            if (is_category) {
                filebrowser::FsDirCollection games{};
                filebrowser::FsView::get_collection(&fs, d1_path, "", games, false, true, false);
                for (const auto& game : games.dirs) {
                    const auto game_dir = fs::AppendPath(d1_path, game.name);
                    filebrowser::FsDirCollection backups{};
                    filebrowser::FsView::get_collection(&fs, game_dir, "", backups, false, true, false);
                    for (const auto& b : backups.dirs) {
                        process_folder(fs::AppendPath(game_dir, b.name), b.name, game.name, prio_base + 2);
                    }
                }
            } else {
                filebrowser::FsDirCollection backups{};
                filebrowser::FsView::get_collection(&fs, d1_path, "", backups, false, true, false);
                for (const auto& b : backups.dirs) {
                    process_folder(fs::AppendPath(d1_path, b.name), b.name, d1.name, prio_base + 1);
                }
            }
        }
    };

    scan_jksv_root(fs::FsPath{JKSV_PATH}, 2);
    scan_jksv_root(fs::FsPath{JKSV_SWITCH_PATH}, 2);

    // 4. Scan Checkpoint folder backups: /switch/Checkpoint/saves and /Checkpoint/saves
    const auto scan_checkpoint_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto cp_root = fs::AppendPath(fs.Root(), root_path);
        filebrowser::FsDirCollection games{};
        filebrowser::FsView::get_collection(&fs, cp_root, "", games, false, true, false);
        for (const auto& game : games.dirs) {
            const auto game_dir = fs::AppendPath(cp_root, game.name);
            filebrowser::FsDirCollection backups{};
            filebrowser::FsView::get_collection(&fs, game_dir, "", backups, false, true, false);
            for (const auto& b : backups.dirs) {
                process_folder(fs::AppendPath(game_dir, b.name), b.name, game.name, prio_base + 1);
            }
        }
    };

    scan_checkpoint_root(fs::FsPath{CHECKPOINT_SAVES_PATH}, 3);
    scan_checkpoint_root(fs::FsPath{CHECKPOINT_ROOT_SAVES_PATH}, 3);

    // 5. Scan custom search paths for folder backups
    const auto scan_custom_folder_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto root = fs::AppendPath(fs.Root(), root_path);
        filebrowser::FsDirCollection l1{};
        filebrowser::FsView::get_collection(&fs, root, "", l1, false, true, false);
        for (const auto& d1 : l1.dirs) {
            const auto d1_path = fs::AppendPath(root, d1.name);
            filebrowser::FsDirCollection l2{};
            filebrowser::FsView::get_collection(&fs, d1_path, "", l2, false, true, false);
            if (!l2.dirs.empty()) {
                for (const auto& d2 : l2.dirs) {
                    process_folder(fs::AppendPath(d1_path, d2.name), d2.name, d1.name, prio_base + 2);
                }
            } else {
                process_folder(d1_path, d1.name, "", prio_base + 1);
            }
        }
    };

    int folder_custom_prio = 10;
    for (const auto& custom_path_str : GetBackupSearchPaths()) {
        scan_custom_folder_root(fs::FsPath{custom_path_str}, folder_custom_prio);
        folder_custom_prio += 5;
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
    // const auto sort = m_sort.Get();
    const auto order = m_order.Get();
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
    if (group.is_backup) {
        const auto target_key = BackupGroupKey(group);
        std::unordered_set<std::string> seen_paths;
        std::vector<BackupCandidate> out;

        for (const auto& m : group.backup_members) {
            if (m.path.empty() || !seen_paths.insert(m.path.s).second) {
                continue;
            }

            const auto slash = std::strrchr(m.path.s, '/');
            const auto fname = slash ? (slash + 1) : m.path.s;
            BackupArchiveInfo info{};
            bool inspected = false;
            if (m.is_directory) {
                inspected = InspectBackupFolder(fs, m.path, fname, group.dbi_game_dir, info);
            } else {
                inspected = InspectBackupArchive(fs, m.path, fname, group.dbi_game_dir, info);
            }
            if (!inspected) {
                continue;
            }

            info.source = m.source;
            if (BackupGroupKey(info) != target_key) {
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
    const auto target_key = BackupGroupKey(group);

    std::vector<BackupCandidate> out;
    for (const auto& c : candidates) {
        const auto slash = std::strrchr(c.path.s, '/');
        const auto fname = slash ? (slash + 1) : c.path.s;
        BackupArchiveInfo info{};
        bool inspected = false;
        if (c.is_directory) {
            inspected = InspectBackupFolder(fs, c.path, fname, group.dbi_game_dir, info);
        } else {
            inspected = InspectBackupArchive(fs, c.path, fname, group.dbi_game_dir, info);
        }
        if (inspected && BackupGroupKey(info) == target_key) {
            out.emplace_back(c);
        }
    }

    if (out.empty() && !group.backup_path.empty()) {
        out.emplace_back(BackupCandidate{group.backup_timestamp, group.backup_path, 0, group.backup_is_directory});
    }

    return out;
}

} // namespace sphaira::ui::menu::save
