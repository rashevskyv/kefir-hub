#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "title_info.hpp"
#include "fs.hpp"
#include "option.hpp"
#include "dumper.hpp"
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>
#include <span>
#include <unordered_set>
#include <unordered_map>
#include "ui/menus/save/save_slot_backend.hpp"

namespace sphaira::ui::menu::save {

enum class BackupSource : u8 {
    KefirHub = 0,
    Dbi,
    Jksv,
    Checkpoint,
    Other,
};

inline auto GetBackupSourceLabel(BackupSource s) -> const char* {
    switch (s) {
        case BackupSource::KefirHub:   return "Kefir Hub";
        case BackupSource::Dbi:        return "DBI";
        case BackupSource::Jksv:       return "JKSV";
        case BackupSource::Checkpoint: return "Checkpoint";
        case BackupSource::Other:      return "Other";
        default:                       return "Other";
    }
}

// one restorable backup archive found for a save, used to build the restore
// picker. ts is the YYYYMMDDHHMMSS key parsed from the file name (for sorting
// and display); source is a stable tie-break for equal timestamps (lower
// wins: dbi format beats sphaira new/legacy; path is the final tie-break).
struct BackupCandidate {
    u64 ts{};
    fs::FsPath path{};
    int source{};
    bool is_directory{false};
};

struct Entry final : FsSaveDataInfo {
    NacpLanguageEntry lang{};
    int image{};
    bool selected{};
    // true for a synthesized tile that stands for a backup archive on disk
    // rather than live save data on the console. drawn with a yellow inner
    // border and grouped below the "Backups" divider.
    bool is_backup{};
    bool backup_rank_known{};
    title::NacpLoadStatus status{title::NacpLoadStatus::None};

    u64 backup_timestamp{};
    size_t backup_count{};
    fs::FsPath backup_path{};
    bool backup_is_directory{false};
    std::string dbi_game_dir{};
    u64 source_timestamp{};
    u64 commit_id{};
    std::vector<BackupCandidate> backup_members{};
    BackupSource backup_source{BackupSource::Other};

    bool is_planned_create{};
    SaveCreationRequest creation_request{};
    bool is_game_parent{};
    std::vector<Entry> children{};

    auto GetName() const -> const char* {
        return lang.name;
    }

    auto GetAuthor() const -> const char* {
        return lang.author;
    }
};
auto ExpandGameGroups(const std::vector<Entry>& entries) -> std::vector<Entry>;
// batch restore: groups that differ only by backup source target one save slot; keep the newest.
void KeepNewestPerSlot(std::vector<Entry>& groups);

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
Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path = nullptr, bool* out_mutation_started = nullptr);
Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty);
Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool allow_empty, bool* out_created_slot_retained);
Result RestoreSaveFolder(ProgressBox* pbox, const Entry& e, const fs::FsPath& folder_path, fs::FsPath* out_recovery_path = nullptr, bool* out_mutation_started = nullptr);
Result RestoreSaveFolder(ProgressBox* pbox, const Entry& e, const fs::FsPath& folder_path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool* out_created_slot_retained);

struct Menu final : grid::Menu {
    // app_id_filter limits the grid to one game's saves (entered from the game
    // details menu); 0 shows everything, as the standalone menu does.
    Menu(u32 flags, u64 app_id_filter = 0, Category category = Category::All);
    ~Menu();

    auto GetShortTitle() const -> const char* override {
        if (!m_app_id_filter) {
            return "Saves";
        }
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

    static auto ListAccountSaves(const AccountUid& uid) -> std::vector<Entry>;
    void BackupSaves(std::vector<Entry> entries);
    void DeleteSaves(std::vector<Entry> entries);
    auto BackupSavesOn(ProgressBox* pbox, std::vector<Entry> entries, const fs::FsPath& backup_root = "/dumps") -> Result;
    auto DeleteSavesOn(ProgressBox* pbox, std::vector<Entry> entries) -> Result;

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
    void DrawCategoryTabs(NVGcontext* vg, Theme* theme);

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
        struct Section {
            std::string label;
            s64 entry_start{};
            s64 entry_count{};
            s64 first_display{};
            bool has_divider{false};
        };

        s64 row{1};
        s64 live_count{};
        s64 backup_count{};
        s64 base_fill{};            // fillers padding the last live row
        s64 pad{};                  // base_fill + one empty divider row
        s64 first_backup_display{}; // display index of the first backup tile
        s64 display_count{};
        bool has_backups{};
        bool horizontal{};          // HOME (HbMenu) layout scrolls sideways
        std::vector<Section> sections{};
    };
    auto ComputeGridSections() const -> GridSections;
    auto EntryToDisplay(s64 entry, const GridSections& g) const -> s64;
    auto DisplayToEntry(s64 display, const GridSections& g) const -> s64; // -1 == filler
    auto ResolveDisplay(s64 display, s64 from, const GridSections& g) const -> s64;
    void DrawCategoryBorder(NVGcontext* vg, Theme* theme, const Vec4& v, const Entry& e);
    void DrawSectionDivider(NVGcontext* vg, Theme* theme, const Vec4& first_v, const GridSections& g, const std::string& label, bool align_above_tile) const;
    void DrawHbMenuTitle(NVGcontext* vg, const Vec4& v, bool selected, const char* name);
    struct BackupColumnLayout {
        float max_title_w{0.f};
        float max_account_w{0.f};
        float max_date_w{0.f};
    };
    void DrawBackupSecondaryColumns(NVGcontext* vg, Theme* theme, const Vec4& v, const Vec4& image_v, const Entry& e, const BackupColumnLayout& layout, const char* info) const;

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
    void BackupSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root);
    void RestoreSaves(std::vector<Entry> entries);
    void RestoreSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root);
    void RestoreSaves(std::vector<Entry> sources, std::vector<Entry> targets, const dump::DumpLocation& location, const fs::FsPath& backup_root);
    void ShowRestoreConfirmPage(
        std::shared_ptr<std::vector<Entry>> sources,
        std::shared_ptr<std::vector<Entry>> targets,
        const dump::DumpLocation& location,
        const fs::FsPath& backup_root,
        size_t page,
        size_t num_pages);
    void ExecuteRestore(
        std::shared_ptr<std::vector<Entry>> sources,
        std::shared_ptr<std::vector<Entry>> targets,
        const dump::DumpLocation& location,
        const fs::FsPath& backup_root);
    // entry point from "Start Restore": handles the optional remote pre-sync,
    // and shows the backup picker for a single selected save.
    void StartRestore(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root);
    // collect (off the UI thread, via ProgressBox) + show the picker for one
    // save, then restore the chosen archive. remote_names are archive file
    // names just downloaded from WebDAV (flagged with a cloud marker); empty
    // when remote restore is off.
    auto MakeBackupGroupFromLiveEntry(const Entry& live, const dump::DumpLocation& location, const fs::FsPath& backup_root) const -> Entry;
    auto MakeBackupGroupFromLiveEntry(const Entry& live, const fs::FsPath& backup_root) const -> Entry;
    void ShowRestorePickerPopup(Entry e, const Entry& group, const dump::DumpLocation& location, const fs::FsPath& backup_root, std::vector<std::string> remote_names, std::vector<BackupCandidate> candidates);
    void RestoreSavesPicked(Entry e, const Entry& group, const dump::DumpLocation& location, const fs::FsPath& backup_root, fs::FsPath chosen);
    Result DownloadRemoteBackupsForEntry(ProgressBox* pbox, const location::Entry& loc, const dump::DumpLocation& location, Entry e, const fs::FsPath& backup_root, std::vector<std::string>* out_downloaded) const;
    void PromptSaveAction();
    void PromptLiveSaveAction(const std::vector<Entry>& seeds);
    void PromptBackupGroupAction(const std::vector<Entry>& seeds);
    void OpenGameBackupGroup(const Entry& game);
    void RestoreAllForGame(const Entry& game);
    void PromptRestoreAllDestinations(
        std::shared_ptr<std::vector<Entry>> seeds,
        size_t step,
        std::shared_ptr<std::vector<AccountProfileBase>> accounts,
        std::shared_ptr<std::vector<Entry>> resolved_targets,
        std::shared_ptr<std::set<std::string>> seen_target_keys,
        const dump::DumpLocation& location,
        const fs::FsPath& backup_root,
        const std::string& game_name);
    void CreateBackupIfNewer(const std::vector<Entry>& seeds);
    void VerifyIntegrity(const std::vector<Entry>& seeds);
    void DeleteOlderBackups(const std::vector<Entry>& seeds);
    void RestoreSingleBackupGroup(Entry group, const AccountUid* explicit_dest_uid = nullptr, bool force_user_picker = false);
    void RestoreSingleBackupGroup(Entry group, const AccountUid* explicit_dest_uid, bool force_user_picker, const dump::DumpLocation& location, const fs::FsPath& backup_root, bool return_to_actions = false);
    void RestoreBackupGroups(std::vector<Entry> groups, bool force_user_picker = false, bool return_to_actions = false);
    void RestoreBackupGroups(std::vector<Entry> groups, bool force_user_picker, const dump::DumpLocation& location, const fs::FsPath& backup_root, bool return_to_actions = false);
    void PromptBatchRestoreTargets(std::shared_ptr<std::vector<Entry>> seeds, size_t step, std::shared_ptr<std::vector<AccountProfileBase>> accounts, std::shared_ptr<std::vector<Entry>> resolved_targets, std::shared_ptr<std::set<std::string>> seen_target_keys);
    void PromptBatchRestoreTargets(std::vector<Entry> seeds, size_t step, std::vector<AccountProfileBase> accounts, std::shared_ptr<std::vector<Entry>> resolved_targets, std::shared_ptr<std::set<std::string>> seen_target_keys);
    void PromptBatchRestoreTargets(std::shared_ptr<std::vector<Entry>> seeds, size_t step, std::shared_ptr<std::vector<AccountProfileBase>> accounts, std::shared_ptr<std::vector<Entry>> resolved_targets, std::shared_ptr<std::set<std::string>> seen_target_keys, const dump::DumpLocation& location, const fs::FsPath& backup_root, bool return_to_actions = false);
    void PromptBatchRestoreTargets(std::vector<Entry> seeds, size_t step, std::vector<AccountProfileBase> accounts, std::shared_ptr<std::vector<Entry>> resolved_targets, std::shared_ptr<std::set<std::string>> seen_target_keys, const dump::DumpLocation& location, const fs::FsPath& backup_root);
    void DeleteBackupGroups(const std::vector<Entry>& groups);
    void PromptSaveTypeOptions(SaveOp op);
    void SyncSavesRemote();
    void SyncSavesRemoteWithLocation(const location::Entry& loc);

    auto BuildSavePath(const Entry& e, bool is_auto, const fs::FsPath& backup_root) const -> fs::FsPath;
    Result RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path = nullptr, bool* out_mutation_started = nullptr) const;
    Result RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started, bool* out_created_slot_retained) const;
    Result BackupSaveInternal(ProgressBox* pbox, const dump::DumpLocation& location, const Entry& e, bool compressed, bool is_auto = false, const fs::FsPath& backup_root = "/dumps", fs::FsPath* out_path = nullptr) const;
    // every restorable archive for e across all backup formats/locations,
    // newest first.
    auto CollectBackups(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root) const -> std::vector<BackupCandidate>;
    auto CollectGroupArchives(fs::Fs* fs, const Entry& group, const fs::FsPath& backup_root = "/dumps") const -> std::vector<BackupCandidate>;
    static auto FindLiveRestoreCandidates(const Entry& backup, const AccountUid* explicit_uid) -> std::vector<Entry>;
    static void ResolveRestoreTarget(const Entry& backup, const AccountUid* explicit_uid, std::function<void(std::optional<Entry>)> cb);
    static void ResolveRestoreTarget(const Entry& backup, std::function<void(std::optional<Entry>)> cb) {
        ResolveRestoreTarget(backup, nullptr, std::move(cb));
    }
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
    ScrollingText m_hb_title_scroll{};
    bool m_is_reversed{};
    bool m_dirty{};

    std::vector<AccountProfileBase> m_accounts{};
    s64 m_account_index{};
    bool m_all_accounts{true};
    std::vector<u8> m_account_enabled{};
    std::array<u8, SAVE_TYPES.size()> m_save_type_enabled{};

    option::OptionLong m_sort{INI_SECTION, "sort", SortType::SortType_Updated};
    option::OptionLong m_order{INI_SECTION, "order", OrderType::OrderType_Descending};
    mutable option::OptionLong m_layout{INI_SECTION, "layout", LayoutType::LayoutType_Grid};
    Category m_category{Category::All};
};

} // namespace sphaira::ui::menu::save
