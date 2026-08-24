#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "title_info.hpp"
#include "fs.hpp"
#include "option.hpp"
#include "dumper.hpp"
#include <array>
#include <memory>
#include <vector>
#include <span>
#include <unordered_set>

namespace sphaira::ui::menu::save {

struct Entry final : FsSaveDataInfo {
    NacpLanguageEntry lang{};
    int image{};
    bool selected{};
    // true for a synthesized tile that stands for a backup archive on disk
    // rather than live save data on the console. drawn with a yellow inner
    // border and grouped below the "Backups" divider.
    bool is_backup{};
    title::NacpLoadStatus status{title::NacpLoadStatus::None};

    auto GetName() const -> const char* {
        return lang.name;
    }

    auto GetAuthor() const -> const char* {
        return lang.author;
    }
};

enum SortType {
    SortType_Updated,
};

enum OrderType {
    OrderType_Descending,
    OrderType_Ascending,
};

using LayoutType = grid::LayoutType;

// a folder the user previously confirmed via "Choose Folder...".
struct RecentBackupDir {
    bool stdio{};
    std::string mount{};
    std::string name{};
    fs::FsPath path{};
};

// one restorable backup archive found for a save, used to build the restore
// picker. ts is the YYYYMMDDHHMMSS key parsed from the file name (for sorting
// and display); source is a stable tie-break for equal timestamps (lower
// wins: dbi format beats sphaira new/legacy, matching the old single-best
// FindLatestBackupPath behaviour; path is the final tie-break in the sorter).
struct BackupCandidate {
    u64 ts{};
    fs::FsPath path{};
    int source{};
};

enum class Category {
    All,
    Installed,
    Deleted,
    Backups,
};

enum class SaveOp {
    Backup,
    Restore,
    Delete,
};

void SignalChange();

struct Menu final : grid::Menu {
    // app_id_filter limits the grid to one game's saves (entered from the game
    // details menu); 0 shows everything, as the standalone menu does.
    Menu(u32 flags, u64 app_id_filter = 0, Category category = Category::All);
    ~Menu();

    auto GetShortTitle() const -> const char* override {
        switch (m_category) {
            case Category::Installed: return "Installed Games";
            case Category::Deleted:   return "Deleted Games";
            case Category::Backups:   return "Backups";
            default:                  return "Saves";
        }
    }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void ScanHomebrew();
    void Sort();
    void SortAndFindLastFile(bool scan);
    void FreeEntries();
    void OnLayoutChange();
    void DisplaySaveOptions();
    void DisplayAccountOptions();
    void DisplayDataTypeOptions();
    void DisplayShowSavesOptions();
    void ToggleCurrentSelection();
    void InvertSelection();
    void ChangeCategory(s64 delta);
    void SetCategory(Category category);

    // populates m_installed_app_ids from the console's application records, used
    // to tell installed-game saves apart from orphaned (deleted-game) saves.
    void BuildInstalledAppIds();
    // scans the SD card for backup archives and appends one tile per game that
    // has a backup (deduped by application id). is_backup is set on each.
    void ReadBackupEntries(std::vector<Entry>& out) const;

    // the saves grid keeps all live saves first, then all backup tiles. a full
    // empty row is inserted between the two so the "Backups" divider label has
    // somewhere to live. because List is a uniform grid, that gap is expressed
    // as extra "display" slots the cursor steps over; the helpers below map
    // between entry indices (into m_entries) and those display slots.
    struct GridSections {
        s64 row{1};
        s64 live_count{};
        s64 backup_count{};
        s64 base_fill{};            // fillers padding the last live row
        s64 pad{};                  // base_fill + one empty divider row
        s64 first_backup_display{}; // display index of the first backup tile
        s64 display_count{};
        bool has_backups{};
        bool horizontal{};          // HOME (HbMenu) layout scrolls sideways
    };
    auto ComputeGridSections() const -> GridSections;
    auto EntryToDisplay(s64 entry, const GridSections& g) const -> s64;
    auto DisplayToEntry(s64 display, const GridSections& g) const -> s64; // -1 == filler
    auto ResolveDisplay(s64 display, s64 from, const GridSections& g) const -> s64;
    void DrawCategoryBorder(NVGcontext* vg, Theme* theme, const Vec4& v, const Entry& e) const;
    void DrawSectionDivider(NVGcontext* vg, Theme* theme, const Vec4& first_backup_v, const GridSections& g) const;

    auto GetSelectedEntries() const {
        std::vector<Entry> out;
        for (auto& e : m_entries) {
            if (e.selected) {
                out.emplace_back(e);
            }
        }

        if (!m_entries.empty() && out.empty()) {
            out.emplace_back(m_entries[m_index]);
        }

        return out;
    }

    void ClearSelection() {
        for (auto& e : m_entries) {
            e.selected = false;
        }

        m_selected_count = 0;
    }

    void BackupSaves(std::vector<std::reference_wrapper<Entry>>& entries);
    void BackupSaves(std::vector<Entry> entries);
    void BackupSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root);
    void RestoreSaves(std::vector<Entry> entries);
    void RestoreSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root);
    // entry point from "Start Restore": handles the optional remote pre-sync,
    // and shows the backup picker for a single selected save.
    void StartRestore(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root);
    // collect (off the UI thread, via ProgressBox) + show the picker for one
    // save, then restore the chosen archive. remote_names are archive file
    // names just downloaded from WebDAV (flagged with a cloud marker); empty
    // when remote restore is off.
    void ShowRestorePicker(Entry e, const dump::DumpLocation& location, const fs::FsPath& backup_root, std::vector<std::string> remote_names);
    // builds and pushes the "no backups" message or the picker popup itself,
    // once CollectBackups has already run. UI-thread only.
    void ShowRestorePickerPopup(Entry e, const dump::DumpLocation& location, const fs::FsPath& backup_root, std::vector<std::string> remote_names, std::vector<BackupCandidate> candidates);
    void RestoreSavesPicked(Entry e, const dump::DumpLocation& location, const fs::FsPath& backup_root, fs::FsPath chosen);
    Result DownloadRemoteBackupsForEntry(ProgressBox* pbox, const location::Entry& loc, const dump::DumpLocation& location, Entry e, const fs::FsPath& backup_root, std::vector<std::string>* out_downloaded) const;
    void DeleteSaves(std::vector<Entry> entries);
    void PromptSaveAction();
    void PromptSaveTypeOptions(SaveOp op);
    void SyncSavesRemote();
    void SyncSavesRemoteWithLocation(const location::Entry& loc);

    auto BuildSavePath(const Entry& e, bool is_auto, const fs::FsPath& backup_root) const -> fs::FsPath;
    Result RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path) const;
    Result BackupSaveInternal(ProgressBox* pbox, const dump::DumpLocation& location, const Entry& e, bool compressed, bool is_auto = false, const fs::FsPath& backup_root = "/dumps") const;
    bool FindLatestBackupPath(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root, fs::FsPath& path_out) const;
    // every restorable archive for e across all backup formats/locations,
    // newest first. generalises FindLatestBackupPath.
    auto CollectBackups(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root) const -> std::vector<BackupCandidate>;
    auto GetAccountName(const AccountUid& uid) const -> std::string;
    auto GetAccountSummary() const -> std::string;
    auto GetDataTypeSummary() const -> std::string;
    auto GetSelectedAccountIndexes() const -> std::vector<s64>;
    auto GetSelectedSaveTypes() const -> std::vector<u8>;
    auto CollectActionEntries(const std::vector<Entry>& seeds, const std::vector<u8>& types, const std::vector<s64>& account_indexes) -> std::vector<Entry>;
    void ReadSaveEntries(u8 data_type, s64 account_index, std::vector<Entry>& out) const;
    void MarkFiltersChanged();
    auto GetRecentBackupDirs() -> std::vector<RecentBackupDir>;
    void AddRecentBackupDir(const RecentBackupDir& dir);

private:
    static constexpr inline const char* INI_SECTION = "saves";
    static constexpr inline std::array<u8, 7> SAVE_TYPES{
        FsSaveDataType_System,
        FsSaveDataType_Account,
        FsSaveDataType_Bcat,
        FsSaveDataType_Device,
        FsSaveDataType_Temporary,
        FsSaveDataType_Cache,
        FsSaveDataType_SystemBcat,
    };

    std::vector<Entry> m_entries{};
    const u64 m_app_id_filter{};
    s64 m_index{}; // where i am in the array
    s64 m_selected_count{};
    // index in m_entries where the backup tiles begin; live saves occupy
    // [0, m_backup_start), backup tiles occupy [m_backup_start, size).
    s64 m_backup_start{};
    // application ids currently installed on the console (base title ids).
    std::unordered_set<u64> m_installed_app_ids{};
    std::vector<u64> m_installed_apps{};
    std::unique_ptr<List> m_list{};
    bool m_is_reversed{};
    bool m_dirty{};

    std::vector<AccountProfileBase> m_accounts{};
    s64 m_account_index{};
    bool m_all_accounts{true};
    std::vector<u8> m_account_enabled{};
    std::array<u8, SAVE_TYPES.size()> m_save_type_enabled{};

    option::OptionLong m_sort{INI_SECTION, "sort", SortType::SortType_Updated};
    option::OptionLong m_order{INI_SECTION, "order", OrderType::OrderType_Descending};
    option::OptionLong m_layout{INI_SECTION, "layout", LayoutType::LayoutType_Grid};
    Category m_category{Category::All};
};

} // namespace sphaira::ui::menu::save
