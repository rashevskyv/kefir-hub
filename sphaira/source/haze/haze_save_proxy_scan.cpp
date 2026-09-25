#include "haze/haze_save_proxy_internal.hpp"
#include "app.hpp"
#include "title_info.hpp"
#include "title_export_name.hpp"
#include "log.hpp"
#include "ui/menus/save/save_paths.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::haze {

namespace {

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

static void DisambiguateFinalName(const SaveTypeMap& game_map,
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
} // namespace

void ScanMtpSaves(SaveTreeMap& out_tree) {
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
        auto& game_map = out_tree[game_name];

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

        auto& temp_map = out_tree["Temporary"];
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

        auto& system_map = out_tree["System"];
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

        auto& system_bcat_map = out_tree["System BCAT"];
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

    log_write("[MTP-SAVES] scanned %zu entries\n", out_tree.size());
}

} // namespace sphaira::haze
