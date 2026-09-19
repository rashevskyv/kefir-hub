#include "haze/haze_internal.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "title_info.hpp"
#include "title_export_name.hpp"
#include "title_nsp.hpp"
#include "mtp_games_path.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/progress_box.hpp"
#include <usbhsfs.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <functional>
#include <haze.h>

namespace sphaira::haze {

struct FsSaveProxy final : FsProxyBase {
    FsSaveProxy(const char* name, const char* display_name) : FsProxyBase{name, display_name} {
        ScanSaves();
    }

    Result GetTotalSpace(const char *path, s64 *out) override {
        const auto pp = Parse(path);
        if (pp.depth >= 2) {
            std::shared_ptr<fs::FsNative> fs;
            R_TRY(MountSave(pp, fs));
            return fs->GetTotalSpace("/", out);
        }
        *out = 1024ULL * 1024ULL * 1024ULL * 32ULL;
        R_SUCCEED();
    }
    Result GetFreeSpace(const char *path, s64 *out) override {
        const auto pp = Parse(path);
        if (pp.depth >= 2) {
            std::shared_ptr<fs::FsNative> fs;
            R_TRY(MountSave(pp, fs));
            return fs->GetFreeSpace("/", out);
        }
        *out = 1024ULL * 1024ULL * 1024ULL * 32ULL;
        R_SUCCEED();
    }

    Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) override {
        const auto pp = Parse(path);

        // levels 0-2 are fully virtual directories.
        if (pp.depth < 3) {
            if (pp.depth >= 1) {
                const auto game = FindGame(pp.game);
                R_UNLESS(game, FsError_PathNotFound);
                R_UNLESS(pp.depth == 1 || game->contains(pp.type), FsError_PathNotFound);
            }
            *out_entry_type = FsDirEntryType_Dir;
            R_SUCCEED();
        }

        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));
        return fs->GetEntryType(pp.rest, out_entry_type);
    }

    // the drive is a view of decrypted saves: everything that would modify
    // it is rejected fail-closed, matching the settings contract.
    Result CreateFile(const char* path, s64 size, u32 option) override {
        log_write("[MTP-SAVES] rejecting CreateFile(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteFile(const char* path) override {
        log_write("[MTP-SAVES] rejecting DeleteFile(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result RenameFile(const char *old_path, const char *new_path) override {
        log_write("[MTP-SAVES] rejecting RenameFile(%s)\n", old_path);
        R_THROW(FsError_NotImplemented);
    }
    Result CreateDirectory(const char* path) override {
        log_write("[MTP-SAVES] rejecting CreateDirectory(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result DeleteDirectoryRecursively(const char* path) override {
        log_write("[MTP-SAVES] rejecting DeleteDirectoryRecursively(%s)\n", path);
        R_THROW(FsError_NotImplemented);
    }
    Result RenameDirectory(const char *old_path, const char *new_path) override {
        log_write("[MTP-SAVES] rejecting RenameDirectory(%s)\n", old_path);
        R_THROW(FsError_NotImplemented);
    }
    Result SetFileSize(FsFile *file, s64 size) override {
        log_write("[MTP-SAVES] rejecting SetFileSize()\n");
        R_THROW(FsError_NotImplemented);
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
        log_write("[MTP-SAVES] rejecting WriteFile()\n");
        R_THROW(FsError_NotImplemented);
    }

    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        log_write("[MTP-SAVES] OpenFile(%s)\n", path);
        R_UNLESS(!(mode & (FsOpenMode_Write | FsOpenMode_Append)), FsError_NotImplemented);

        const auto pp = Parse(path);
        R_UNLESS(pp.depth >= 3, FsError_PathNotFound);

        std::shared_ptr<fs::FsNative> fs;
        R_TRY(MountSave(pp, fs));

        auto handle = std::make_unique<FileHandle>();
        handle->fs = fs;
        R_TRY(fs->OpenFile(pp.rest, mode, &handle->file));

        auto raw = handle.release();
        std::memcpy(&out_file->s, &raw, sizeof(raw));
        R_SUCCEED();
    }
    Result GetFileSize(FsFile *file, s64 *out_size) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        return h->file.GetSize(out_size);
    }
    Result ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        return h->file.Read(off, buf, read_size, option, out_bytes_read);
    }
    void CloseFile(FsFile *file) override {
        FileHandle* h;
        std::memcpy(&h, &file->s, sizeof(h));
        delete h;
        std::memset(file, 0, sizeof(*file));
    }

    Result OpenDirectory(const char *path, u32 mode, FsDir *out_dir) override {
        const auto pp = Parse(path);
        auto handle = std::make_unique<DirHandle>();

        if (pp.depth < 2) {
            const TypeMap* game{};
            if (pp.depth == 1) {
                game = FindGame(pp.game);
                R_UNLESS(game, FsError_PathNotFound);
            }

            handle->is_virtual = true;
            // levels 1-2 only contain directories.
            if (mode & FsDirOpenMode_ReadDirs) {
                if (game) {
                    for (const auto& [name, info] : *game) {
                        handle->virt.emplace_back(MakeVirtualDirEntry(name));
                    }
                } else {
                    for (const auto& [name, types] : m_tree) {
                        handle->virt.emplace_back(MakeVirtualDirEntry(name));
                    }
                }
            }
        } else {
            std::shared_ptr<fs::FsNative> fs;
            R_TRY(MountSave(pp, fs));
            handle->fs = fs;
            R_TRY(fs->OpenDirectory(pp.rest, mode, &handle->dir));
        }

        auto raw = handle.release();
        std::memcpy(&out_dir->s, &raw, sizeof(raw));
        log_write("[MTP-SAVES] OpenDirectory(%s) depth=%d\n", path, pp.depth);
        R_SUCCEED();
    }
    Result ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));

        if (h->is_virtual) {
            // same generation pattern as FsProxyVfs::ReadDirectory.
            max_entries = std::min<s64>(h->virt.size() - h->index, max_entries);
            std::memcpy(buf, h->virt.data() + h->index, max_entries * sizeof(*buf));
            h->index += max_entries;
            *out_total_entries = max_entries;
            R_SUCCEED();
        }

        return h->dir.Read(out_total_entries, max_entries, buf);
    }
    Result GetDirectoryEntryCount(FsDir *d, s64 *out_count) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));

        if (h->is_virtual) {
            *out_count = h->virt.size();
            R_SUCCEED();
        }

        return h->dir.GetEntryCount(out_count);
    }
    void CloseDirectory(FsDir *d) override {
        DirHandle* h;
        std::memcpy(&h, &d->s, sizeof(h));
        delete h;
        std::memset(d, 0, sizeof(*d));
    }

    // saves are small, keep transfers single-threaded.
    bool MultiThreadTransfer(s64 size, bool read) override {
        return false;
    }

private:
    static constexpr size_t MOUNT_CACHE_MAX = 4;

    using TypeMap = std::map<std::string, FsSaveDataInfo, CaseInsensitiveLess>;

    struct CachedMount {
        std::shared_ptr<fs::FsNative> fs;
        u64 tick;
    };

    // handles keep a shared_ptr to their mount, so a mount evicted from the
    // LRU cache stays alive until every handle into it is closed.
    // note: member order matters - file/dir must be destroyed before fs.
    struct FileHandle {
        std::shared_ptr<fs::FsNative> fs{};
        fs::File file{};
    };

    struct DirHandle {
        std::shared_ptr<fs::FsNative> fs{};
        fs::Dir dir{};
        std::vector<FsDirectoryEntry> virt{};
        s64 index{};
        bool is_virtual{};
    };

    struct ParsedPath {
        std::string game; // level 1 component.
        std::string type; // level 2 component.
        fs::FsPath rest{"/"}; // path inside the mounted save.
        int depth{}; // 0 = root, 1 = game, 2 = game/type, 3 = inside the save.
    };

    struct AccountInfo {
        AccountUid uid;
        std::string raw_nickname;
    };

    static bool EqualsCaseInsensitive(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

    static bool IsWindowsReservedDeviceName(std::string_view name) {
        const auto dot_pos = name.find('.');
        const auto stem = (dot_pos != std::string_view::npos) ? name.substr(0, dot_pos) : name;
        if (stem.size() == 3) {
            if (EqualsCaseInsensitive(stem, "CON") ||
                EqualsCaseInsensitive(stem, "PRN") ||
                EqualsCaseInsensitive(stem, "AUX") ||
                EqualsCaseInsensitive(stem, "NUL")) {
                return true;
            }
        } else if (stem.size() == 4) {
            const char c3 = stem[3];
            if (c3 >= '1' && c3 <= '9') {
                const auto prefix = stem.substr(0, 3);
                if (EqualsCaseInsensitive(prefix, "COM") ||
                    EqualsCaseInsensitive(prefix, "LPT")) {
                    return true;
                }
            }
        }
        return false;
    }

    static auto TrimMtpName(std::string s) -> std::string {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
            s.erase(s.begin());
        }
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '.')) {
            s.pop_back();
        }
        return s;
    }

    static auto SanitizeMtpComponent(std::string_view input) -> std::string {
        std::string s = title::SanitizeUtf8TitleName(input);
        s = TrimMtpName(s);
        if (s.empty() || s == "." || s == "..") {
            return {};
        }
        if (IsWindowsReservedDeviceName(s)) {
            s = "_" + s;
        }
        return s;
    }

    static auto TruncateMtpComponent(std::string_view input, size_t max_len) -> std::string {
        std::string s = title::TruncateUtf8(input, max_len);
        return TrimMtpName(s);
    }

    static auto FormatSaveIdSuffix(u64 save_data_id) -> std::string {
        return " [" + title::FormatTitleId(save_data_id) + "]";
    }

    static bool IsReservedBucketName(std::string_view name) {
        if (EqualsCaseInsensitive(name, "BCAT") ||
            EqualsCaseInsensitive(name, "Device") ||
            EqualsCaseInsensitive(name, "Cache")) {
            return true;
        }
        if (name.size() > 6 && !strncasecmp(name.data(), "Cache ", 6)) {
            return true;
        }
        return false;
    }

    // Deterministic total order for save records using retained actual fields.
    // Full UID is used internally as a tie-breaker without inspecting padding bytes.
    static bool CompareSaveDataInfo(const FsSaveDataInfo& a, const FsSaveDataInfo& b) {
        if (a.save_data_type != b.save_data_type) {
            return a.save_data_type < b.save_data_type;
        }
        if (a.save_data_index != b.save_data_index) {
            return a.save_data_index < b.save_data_index;
        }
        if (a.save_data_rank != b.save_data_rank) {
            return a.save_data_rank < b.save_data_rank;
        }
        if (a.save_data_id != b.save_data_id) {
            return a.save_data_id < b.save_data_id;
        }
        if (a.save_data_space_id != b.save_data_space_id) {
            return a.save_data_space_id < b.save_data_space_id;
        }
        if (a.system_save_data_id != b.system_save_data_id) {
            return a.system_save_data_id < b.system_save_data_id;
        }
        if (a.application_id != b.application_id) {
            return a.application_id < b.application_id;
        }
        if (a.uid.uid[0] != b.uid.uid[0]) {
            return a.uid.uid[0] < b.uid.uid[0];
        }
        if (a.uid.uid[1] != b.uid.uid[1]) {
            return a.uid.uid[1] < b.uid.uid[1];
        }
        return false;
    }

    static bool IsSameSaveRecord(const FsSaveDataInfo& a, const FsSaveDataInfo& b) {
        return a.save_data_id == b.save_data_id &&
               a.save_data_space_id == b.save_data_space_id &&
               a.save_data_type == b.save_data_type &&
               a.application_id == b.application_id &&
               a.system_save_data_id == b.system_save_data_id &&
               a.save_data_rank == b.save_data_rank &&
               a.save_data_index == b.save_data_index &&
               std::memcmp(&a.uid, &b.uid, sizeof(AccountUid)) == 0;
    }

    // Formats a bounded game directory name ensuring the complete stable [Title ID]
    // suffix is preserved intact and fits sizeof(FsDirectoryEntry::name)-1 even if
    // a '_' prefix is added for a Windows reserved device name.
    static auto FormatSaveGameDirName(const char* localized_name, u64 application_id) -> std::string {
        const std::string suffix = " [" + title::FormatTitleId(application_id) + "]";
        std::string base = title::ResolveMtpDisplayTitleName(localized_name, nullptr, nullptr, application_id);
        if (base == title::FormatTitleId(application_id) || base.empty()) {
            return "[" + title::FormatTitleId(application_id) + "]";
        }

        const bool is_reserved = IsWindowsReservedDeviceName(base);
        constexpr size_t MAX_NAME_LEN = sizeof(FsDirectoryEntry::name) - 1;
        const size_t reserved_space = suffix.size() + (is_reserved ? 1 : 0);
        const size_t max_title_len = (MAX_NAME_LEN > reserved_space) ? (MAX_NAME_LEN - reserved_space) : 0;

        base = title::TruncateUtf8(base, max_title_len);
        while (!base.empty() && (base.back() == ' ' || base.back() == '\t')) {
            base.pop_back();
        }

        if (base.empty()) {
            return "[" + title::FormatTitleId(application_id) + "]";
        }

        return (is_reserved ? "_" : "") + base + suffix;
    }

    static auto BuildSaveGameDirName(u64 application_id) -> std::string {
        const char* localized_name = nullptr;
        if (const auto data = title::Get(application_id); data && data->status == title::NacpLoadStatus::Loaded) {
            localized_name = data->lang.name;
        }
        return FormatSaveGameDirName(localized_name, application_id);
    }

    static void DisambiguateFinalName(const TypeMap& game_map,
                                      const std::string& base,
                                      const FsSaveDataInfo& info,
                                      std::string& out_name) {
        constexpr size_t MAX_NAME_LEN = sizeof(FsDirectoryEntry::name) - 1;
        char disambig[80];
        std::snprintf(disambig, sizeof(disambig), " [%016llX-s%u-t%u-r%u-i%u]",
            static_cast<unsigned long long>(info.save_data_id),
            static_cast<unsigned>(info.save_data_space_id),
            static_cast<unsigned>(info.save_data_type),
            static_cast<unsigned>(info.save_data_rank),
            static_cast<unsigned>(info.save_data_index));
        const size_t extra_len = std::strlen(disambig);
        const size_t max_base = (MAX_NAME_LEN > extra_len) ? (MAX_NAME_LEN - extra_len) : 0;
        std::string dbase = TruncateMtpComponent(base.empty() ? "Account" : base, max_base);
        out_name = dbase + disambig;

        int counter = 2;
        while (game_map.find(out_name) != game_map.end()) {
            std::string cnt = " (" + std::to_string(counter++) + ")";
            const size_t total_suffix_len = extra_len + cnt.size();
            const size_t mb = (MAX_NAME_LEN > total_suffix_len) ? (MAX_NAME_LEN - total_suffix_len) : 0;
            std::string cb = TruncateMtpComponent(base.empty() ? "Account" : base, mb);
            out_name = cb + cnt + disambig;
        }
    }

    // scans once at registration (never per ReadDirectory), building the
    // virtual tree: game dir -> save dir -> save info.
    void ScanSaves() {
        const auto raw_accounts = App::GetAccountList();
        std::vector<AccountInfo> accounts;
        accounts.reserve(raw_accounts.size());
        for (const auto& acc : raw_accounts) {
            accounts.push_back({acc.uid, std::string(acc.nickname)});
        }

        // ref-counted background loader used by title::Get() for game names.
        const bool has_title = R_SUCCEEDED(title::Init());
        ON_SCOPE_EXIT(if (has_title) { title::Exit(); });

        namespace save = ui::menu::save;
        const auto discovered_records = save::DiscoverSaveDataInfo(nullptr, std::nullopt);

        std::map<u64, std::vector<FsSaveDataInfo>> game_save_records;
        std::vector<FsSaveDataInfo> temporary_records;
        std::vector<FsSaveDataInfo> system_records;
        std::vector<FsSaveDataInfo> system_bcat_records;

        for (const auto& info : discovered_records) {
            switch (info.save_data_type) {
                case FsSaveDataType_Account:
                case FsSaveDataType_Bcat:
                case FsSaveDataType_Device:
                case FsSaveDataType_Cache:
                    game_save_records[info.application_id].push_back(info);
                    break;
                case FsSaveDataType_Temporary:
                    temporary_records.push_back(info);
                    break;
                case FsSaveDataType_System:
                    system_records.push_back(info);
                    break;
                case FsSaveDataType_SystemBcat:
                    system_bcat_records.push_back(info);
                    break;
                default:
                    log_write("[MTP-SAVES] ignoring unknown save type %u (save_id=0x%016llX)\n",
                        static_cast<unsigned>(info.save_data_type),
                        static_cast<unsigned long long>(info.save_data_id));
                    break;
            }
        }

        constexpr size_t MAX_NAME_LEN = sizeof(FsDirectoryEntry::name) - 1;

        for (auto& [app_id, records] : game_save_records) {
            // Establish a deterministic total order and deduplicate identical scan records.
            std::sort(records.begin(), records.end(), CompareSaveDataInfo);
            auto it = std::unique(records.begin(), records.end(), IsSameSaveRecord);
            records.erase(it, records.end());

            const auto game_name = BuildSaveGameDirName(app_id);
            auto& game_map = m_tree[game_name];

            // 1. Process non-account buckets first in deterministic order to establish stable bucket names.
            for (const auto& info : records) {
                if (info.save_data_type == FsSaveDataType_Account) {
                    continue;
                }
                std::string base;
                if (info.save_data_type == FsSaveDataType_Bcat) {
                    base = "BCAT";
                } else if (info.save_data_type == FsSaveDataType_Device) {
                    base = "Device";
                } else if (info.save_data_type == FsSaveDataType_Cache) {
                    base = info.save_data_index ? ("Cache " + std::to_string(info.save_data_index)) : "Cache";
                } else {
                    base = ui::menu::save::GetSaveTypeSubdir(info.save_data_type).toString();
                }

                std::string name = base;
                if (game_map.find(name) != game_map.end()) {
                    name = base + FormatSaveIdSuffix(info.save_data_id);
                }
                if (game_map.find(name) != game_map.end()) {
                    DisambiguateFinalName(game_map, base, info, name);
                }
                game_map.emplace(name, info);
            }

            // 2. Process account saves in deterministic order.
            struct AccountSaveEntry {
                FsSaveDataInfo info;
                std::string base;
                std::string candidate_name;
                bool has_usable_nickname{false};
                bool needs_suffix{false};
            };
            std::vector<AccountSaveEntry> acc_entries;

            for (const auto& info : records) {
                if (info.save_data_type != FsSaveDataType_Account) {
                    continue;
                }
                const AccountInfo* found_acc = nullptr;
                for (const auto& acc : accounts) {
                    if (!std::memcmp(&info.uid, &acc.uid, sizeof(AccountUid))) {
                        found_acc = &acc;
                        break;
                    }
                }

                AccountSaveEntry entry{};
                entry.info = info;
                if (found_acc) {
                    entry.base = SanitizeMtpComponent(found_acc->raw_nickname);
                }

                if (entry.base.empty()) {
                    entry.has_usable_nickname = false;
                    entry.base = "Account";
                    entry.needs_suffix = true;
                } else {
                    entry.has_usable_nickname = true;
                    if (IsReservedBucketName(entry.base) || game_map.find(entry.base) != game_map.end()) {
                        entry.needs_suffix = true;
                    }
                }
                acc_entries.push_back(std::move(entry));
            }

            // Ponytail: O(n^2) duplicate scans are bounded by console account limit (<= 8); if general scaling is needed, upgrade to an unordered_map frequency index.
            for (size_t i = 0; i < acc_entries.size(); i++) {
                for (size_t j = i + 1; j < acc_entries.size(); j++) {
                    if (EqualsCaseInsensitive(acc_entries[i].base, acc_entries[j].base)) {
                        acc_entries[i].needs_suffix = true;
                        acc_entries[j].needs_suffix = true;
                    }
                }
            }

            for (size_t i = 0; i < acc_entries.size(); i++) {
                for (size_t j = i + 1; j < acc_entries.size(); j++) {
                    if (acc_entries[i].has_usable_nickname && acc_entries[j].has_usable_nickname) {
                        const size_t suffix_len = 19;
                        const size_t max_base = (MAX_NAME_LEN > suffix_len) ? (MAX_NAME_LEN - suffix_len) : 0;
                        const auto ti = TruncateMtpComponent(acc_entries[i].base, max_base);
                        const auto tj = TruncateMtpComponent(acc_entries[j].base, max_base);
                        if (EqualsCaseInsensitive(ti, tj)) {
                            acc_entries[i].needs_suffix = true;
                            acc_entries[j].needs_suffix = true;
                        }
                    }
                }
            }

            auto format_acc_name = [&](const AccountSaveEntry& entry) -> std::string {
                std::string name;
                if (entry.needs_suffix) {
                    const std::string suffix = FormatSaveIdSuffix(entry.info.save_data_id);
                    const size_t suffix_len = suffix.size();
                    const size_t max_base = (MAX_NAME_LEN > suffix_len) ? (MAX_NAME_LEN - suffix_len) : 0;
                    std::string truncated_base = TruncateMtpComponent(entry.base, max_base);
                    if (truncated_base.empty()) {
                        truncated_base = "Account";
                    }
                    name = truncated_base + suffix;
                } else {
                    name = TruncateMtpComponent(entry.base, MAX_NAME_LEN);
                }

                if (IsWindowsReservedDeviceName(name)) {
                    name = "_" + name;
                }
                if (name.size() > MAX_NAME_LEN) {
                    name = title::TruncateUtf8(name, MAX_NAME_LEN);
                    name = TrimMtpName(name);
                }
                return name;
            };

            for (auto& entry : acc_entries) {
                entry.candidate_name = format_acc_name(entry);
            }

            // If a candidate name collides with another account's candidate name or an existing bucket, ensure suffixing.
            for (size_t i = 0; i < acc_entries.size(); i++) {
                if (!acc_entries[i].needs_suffix && game_map.find(acc_entries[i].candidate_name) != game_map.end()) {
                    acc_entries[i].needs_suffix = true;
                    acc_entries[i].candidate_name = format_acc_name(acc_entries[i]);
                }
                for (size_t j = i + 1; j < acc_entries.size(); j++) {
                    if (EqualsCaseInsensitive(acc_entries[i].candidate_name, acc_entries[j].candidate_name)) {
                        if (!acc_entries[i].needs_suffix) {
                            acc_entries[i].needs_suffix = true;
                            acc_entries[i].candidate_name = format_acc_name(acc_entries[i]);
                        }
                        if (!acc_entries[j].needs_suffix) {
                            acc_entries[j].needs_suffix = true;
                            acc_entries[j].candidate_name = format_acc_name(acc_entries[j]);
                        }
                    }
                }
            }

            for (const auto& entry : acc_entries) {
                std::string name = entry.candidate_name;
                if (game_map.find(name) != game_map.end()) {
                    DisambiguateFinalName(game_map, entry.base, entry.info, name);
                }
                game_map.emplace(name, entry.info);
            }
        }

        if (!temporary_records.empty()) {
            std::sort(temporary_records.begin(), temporary_records.end(), CompareSaveDataInfo);
            auto it = std::unique(temporary_records.begin(), temporary_records.end(), IsSameSaveRecord);
            temporary_records.erase(it, temporary_records.end());

            auto& temp_map = m_tree["Temporary"];
            for (const auto& info : temporary_records) {
                const u64 id = info.application_id ? info.application_id : info.save_data_id;
                const std::string base = title::FormatTitleId(id);

                std::string name = base;
                if (temp_map.find(name) != temp_map.end()) {
                    name = base + FormatSaveIdSuffix(info.save_data_id);
                }
                if (temp_map.find(name) != temp_map.end()) {
                    DisambiguateFinalName(temp_map, base, info, name);
                }
                temp_map.emplace(name, info);
            }
        }

        if (!system_records.empty()) {
            std::sort(system_records.begin(), system_records.end(), CompareSaveDataInfo);
            auto it = std::unique(system_records.begin(), system_records.end(), IsSameSaveRecord);
            system_records.erase(it, system_records.end());

            auto& system_map = m_tree["System"];
            for (const auto& info : system_records) {
                const std::string base = "System [" + title::FormatTitleId(info.system_save_data_id) + "]";

                std::string name = base;
                if (system_map.find(name) != system_map.end()) {
                    name = base + FormatSaveIdSuffix(info.save_data_id);
                }
                if (system_map.find(name) != system_map.end()) {
                    DisambiguateFinalName(system_map, base, info, name);
                }
                system_map.emplace(name, info);
            }
        }

        if (!system_bcat_records.empty()) {
            std::sort(system_bcat_records.begin(), system_bcat_records.end(), CompareSaveDataInfo);
            auto it = std::unique(system_bcat_records.begin(), system_bcat_records.end(), IsSameSaveRecord);
            system_bcat_records.erase(it, system_bcat_records.end());

            auto& system_bcat_map = m_tree["System BCAT"];
            for (const auto& info : system_bcat_records) {
                const std::string base = "System BCAT [" + title::FormatTitleId(info.system_save_data_id) + "]";

                std::string name = base;
                if (system_bcat_map.find(name) != system_bcat_map.end()) {
                    name = base + FormatSaveIdSuffix(info.save_data_id);
                }
                if (system_bcat_map.find(name) != system_bcat_map.end()) {
                    DisambiguateFinalName(system_bcat_map, base, info, name);
                }
                system_bcat_map.emplace(name, info);
            }
        }

        log_write("[MTP-SAVES] scanned %zu entries\n", m_tree.size());
    }

    auto Parse(const char* path) const -> ParsedPath {
        ParsedPath out{};
        const auto fixed = FixPath(path);

        // libhaze may produce double slashes ("//game"), skip empty components.
        const char* p = fixed.s;
        for (int level = 0; level < 2; level++) {
            while (*p == '/') {
                p++;
            }
            if (!*p) {
                return out;
            }

            const char* start = p;
            while (*p && *p != '/') {
                p++;
            }

            auto& dst = level == 0 ? out.game : out.type;
            dst.assign(start, p - start);
            out.depth = level + 1;
        }

        while (*p == '/') {
            p++;
        }
        if (*p) {
            out.depth = 3;
            std::snprintf(out.rest.s, sizeof(out.rest.s), "/%s", p);
        }
        return out;
    }

    auto FindGame(const std::string& game) const -> const TypeMap* {
        const auto it = m_tree.find(game);
        return it == m_tree.end() ? nullptr : &it->second;
    }

    Result MountSave(const ParsedPath& pp, std::shared_ptr<fs::FsNative>& out) {
        const auto game_it = m_tree.find(pp.game);
        R_UNLESS(game_it != m_tree.end(), FsError_PathNotFound);
        const auto type_it = game_it->second.find(pp.type);
        R_UNLESS(type_it != game_it->second.end(), FsError_PathNotFound);

        // canonical key, so that differently-cased requests share one mount.
        const auto key = game_it->first + "/" + type_it->first;

        // ops run on the haze thread(s) and may interleave, guard the cache.
        SCOPED_MUTEX(&m_mount_mutex);

        if (const auto it = m_mounts.find(key); it != m_mounts.end()) {
            it->second.tick = ++m_mount_tick;
            out = it->second.fs;
            R_SUCCEED();
        }

        const auto& info = type_it->second;
        FsSaveDataAttribute attr{};
        attr.application_id = info.application_id;
        attr.uid = info.uid;
        attr.system_save_data_id = info.system_save_data_id;
        attr.save_data_type = info.save_data_type;
        attr.save_data_rank = info.save_data_rank;
        attr.save_data_index = info.save_data_index;

        auto fs = std::make_shared<fs::FsNativeSave>((FsSaveDataType)info.save_data_type, (FsSaveDataSpaceId)info.save_data_space_id, &attr, true);
        if (const auto rc = fs->GetFsOpenResult(); R_FAILED(rc)) {
            log_write("[MTP-SAVES] failed to mount save %s 0x%X\n", key.c_str(), rc);
            return rc;
        }

        if (m_mounts.size() >= MOUNT_CACHE_MAX) {
            // evict the least recently used mount. open handles keep their
            // own shared_ptr, so an evicted mount survives until they close.
            const auto lru = std::ranges::min_element(m_mounts, {}, [](const auto& e) { return e.second.tick; });
            m_mounts.erase(lru);
        }

        m_mounts.emplace(key, CachedMount{fs, ++m_mount_tick});
        out = fs;
        R_SUCCEED();
    }

    // level 1 (game) -> level 2 (type) -> save info. built once in the
    // constructor, immutable afterwards (safe for concurrent reads).
    std::map<std::string, TypeMap, CaseInsensitiveLess> m_tree{};

    // lazily mounted saves. cleared by the (default) destructor, which closes
    // every cached save fs when haze::Exit() drops g_fs_entries.
    std::map<std::string, CachedMount> m_mounts{};
    u64 m_mount_tick{};
    Mutex m_mount_mutex{};
};

std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsSaveProxy(const char* name, const char* display_name) {
    return std::make_shared<FsSaveProxy>(name, display_name);
}

} // namespace sphaira::haze
