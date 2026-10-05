#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <cstdio>
#include <utility>
#include <optional>

namespace sphaira::ui::menu::save::bundle {

// Matches sphaira::ui::menu::save::Category
enum class Category {
    All = 0,
    Installed = 1,
    Deleted = 2,
    Backups = 3,
};

// Priority rank for picking the representative tile slot:
// 1. Account matching active_uid (100)
// 2. Any Account (90)
// 3. Device (80)
// 4. BCAT (70)
// 5. Cache (60)
// 6. Temporary (50)
inline int SlotPriority(uint8_t data_type, const uint64_t uid[2], const uint64_t* active_uid = nullptr) {
    if (data_type == 1 /* Account */) {
        if (active_uid && uid[0] == active_uid[0] && uid[1] == active_uid[1] && (active_uid[0] != 0 || active_uid[1] != 0)) {
            return 100;
        }
        return 90;
    }
    if (data_type == 3 /* Device */) return 80;
    if (data_type == 2 /* BCAT */) return 70;
    if (data_type == 5 /* Cache */) return 60;
    if (data_type == 4 /* Temporary */) return 50;
    return 10;
}

// 1. Real visibility decision:
// Category::Installed (1): shows installed apps.
// Category::Deleted (2): shows uninstalled apps with live saves.
// Category::All (0): shows installed if show_installed, uninstalled with live saves if show_deleted.
// Category::Backups (3): live game tiles are not shown.
inline bool ShouldShowGameTile(
    uint64_t app_id,
    bool is_installed,
    bool has_any_save,
    Category category,
    bool show_installed,
    bool show_deleted,
    uint64_t filter_app_id = 0)
{
    if (filter_app_id != 0 && app_id != filter_app_id) {
        return false;
    }
    switch (category) {
        case Category::Installed:
            return is_installed;
        case Category::Deleted:
            return !is_installed && has_any_save;
        case Category::Backups:
            return false;
        case Category::All:
            if (is_installed) {
                return show_installed;
            }
            return show_deleted && has_any_save;
    }
    return false;
}

// 2. Exact slot identity preserving save space and valid UID data:
struct SaveSlotIdentity {
    uint64_t application_id{0};
    uint64_t system_save_data_id{0};
    uint8_t save_data_type{0};
    uint8_t save_data_space_id{0};
    uint8_t save_data_rank{0};
    uint16_t save_data_index{0};
    uint64_t uid[2]{0, 0};
};

inline std::string FormatSlotIdentityKey(const SaveSlotIdentity& id) {
    char buf[128];
    if (id.save_data_type == 0 || id.save_data_type == 6 /* System or SystemBcat */) {
        std::snprintf(buf, sizeof(buf), "sys:%u:%u:%016llX:%u:%u",
            (unsigned)id.save_data_type, (unsigned)id.save_data_space_id,
            (unsigned long long)id.system_save_data_id,
            (unsigned)id.save_data_index, (unsigned)id.save_data_rank);
    } else if (id.save_data_type == 1 /* Account */) {
        std::snprintf(buf, sizeof(buf), "app:%016llX:1:%u:%016llX%016llX:%u:%u",
            (unsigned long long)id.application_id,
            (unsigned)id.save_data_space_id,
            (unsigned long long)id.uid[0], (unsigned long long)id.uid[1],
            (unsigned)id.save_data_index, (unsigned)id.save_data_rank);
    } else {
        // Shared slot (Device, BCAT, Cache, Temporary) - shared across users for this game
        std::snprintf(buf, sizeof(buf), "app:%016llX:%u:%u:%u:%u",
            (unsigned long long)id.application_id,
            (unsigned)id.save_data_type,
            (unsigned)id.save_data_space_id,
            (unsigned)id.save_data_index, (unsigned)id.save_data_rank);
    }
    return std::string(buf);
}

inline bool IsSlotAccountEligible(
    uint8_t save_data_type,
    const uint64_t uid[2],
    bool all_accounts,
    const std::vector<std::pair<uint64_t, uint64_t>>& selected_account_uids)
{
    if (save_data_type != 1 /* Account */) {
        return true;
    }
    if (all_accounts) {
        return true;
    }
    for (const auto& u : selected_account_uids) {
        if (uid[0] == u.first && uid[1] == u.second) {
            return true;
        }
    }
    return false;
}

// 3. Bundle member selection and exact shared-slot deduplication:
template <typename T, typename GetIdentityFn>
inline std::vector<T> DeduplicateBundleMembers(
    const std::vector<T>& candidates,
    bool all_accounts,
    const std::vector<std::pair<uint64_t, uint64_t>>& selected_account_uids,
    GetIdentityFn get_identity)
{
    std::vector<T> out;
    std::unordered_set<std::string> seen;
    for (const auto& item : candidates) {
        SaveSlotIdentity id = get_identity(item);
        if (!IsSlotAccountEligible(id.save_data_type, id.uid, all_accounts, selected_account_uids)) {
            continue;
        }
        std::string key = FormatSlotIdentityKey(id);
        if (seen.insert(key).second) {
            out.push_back(item);
        }
    }
    return out;
}

// 4. Missing live destinations & restore admission outcome:
enum class RestoreDestinationOutcome {
    ExistingLiveTarget,
    PlanAccountCreation,
    UnsupportedMissingDestination,
};

inline bool CanCreateLiveSlot(uint8_t save_data_type, uint8_t rank, uint16_t index, uint64_t application_id) {
    if (save_data_type == 1 /* Account */) {
        return rank == 0 /* Primary */ && index == 0 && application_id != 0;
    }
    return false;
}

inline RestoreDestinationOutcome EvaluateRestoreDestination(
    bool has_live_target,
    uint8_t save_data_type,
    uint8_t rank,
    uint16_t index,
    uint64_t application_id)
{
    if (has_live_target) {
        return RestoreDestinationOutcome::ExistingLiveTarget;
    }
    if (CanCreateLiveSlot(save_data_type, rank, index, application_id)) {
        return RestoreDestinationOutcome::PlanAccountCreation;
    }
    return RestoreDestinationOutcome::UnsupportedMissingDestination;
}

// 5. Empty restore selection:
template <typename T>
inline bool IsRestoreSelectionEmpty(const std::vector<T>& selected_members) {
    return selected_members.empty();
}

// 6. Backup source identity & newest candidate selection:
// Matches sphaira::ui::menu::save::BackupSource
enum class BackupSourceId : uint8_t {
    KefirHub = 0,
    Dbi = 1,
    Jksv = 2,
    Checkpoint = 3,
    Other = 4,
};

struct BackupMemberCandidate {
    uint64_t timestamp{0};
    std::string path;
    BackupSourceId source{BackupSourceId::Other};
    int priority{0};
    bool is_directory{false};
};

inline std::string FormatSourceGroupKey(BackupSourceId source, const std::string& slot_identity_key) {
    return std::to_string(static_cast<int>(source)) + ":" + slot_identity_key;
}

inline bool IsCandidateBetter(
    const BackupMemberCandidate& candidate,
    const BackupMemberCandidate& current_best)
{
    if (candidate.timestamp != current_best.timestamp) {
        return candidate.timestamp > current_best.timestamp;
    }
    if (candidate.priority != current_best.priority) {
        return candidate.priority < current_best.priority;
    }
    return candidate.path < current_best.path;
}

inline std::string FormatBackupSpaceKeyPart(bool space_known, uint8_t space_id) {
    if (!space_known) {
        return "sp:?";
    }
    return "sp:" + std::to_string(static_cast<unsigned>(space_id));
}

inline std::string FormatBackupRankKeyPart(bool rank_known, uint8_t rank) {
    if (!rank_known) {
        return "rk:?";
    }
    return (rank == 1 /* FsSaveDataRank_Secondary */) ? "rk:1" : "rk:0";
}

inline std::string FormatBackupGroupIdentityKey(
    bool is_system,
    uint8_t save_data_type,
    uint64_t app_or_sys_id,
    bool space_known,
    uint8_t space_id,
    const uint64_t uid[2],
    uint16_t save_data_index,
    bool rank_known,
    uint8_t rank)
{
    char key[0x80];
    const std::string sp = FormatBackupSpaceKeyPart(space_known, space_id);
    const std::string rk = FormatBackupRankKeyPart(rank_known, rank);
    if (is_system) {
        std::snprintf(key, sizeof(key), "backup:system:%u:%s:%016llX:%u:%s",
            (unsigned)save_data_type, sp.c_str(), (unsigned long long)app_or_sys_id,
            (unsigned)save_data_index, rk.c_str());
    } else {
        std::snprintf(key, sizeof(key), "backup:app:%016llX:%u:%s:%016llX%016llX:%u:%s",
            (unsigned long long)app_or_sys_id, (unsigned)save_data_type, sp.c_str(),
            (unsigned long long)uid[0], (unsigned long long)uid[1],
            (unsigned)save_data_index, rk.c_str());
    }
    return std::string(key);
}

// 7. Freshness check decisions:
inline bool IsCandidateEligibleForHubFreshness(bool is_in_target_root, BackupSourceId source) {
    return is_in_target_root && (source == BackupSourceId::KefirHub);
}

inline bool IsBackupFreshnessMatching(
    uint64_t live_timestamp,
    uint64_t live_commit_id,
    uint64_t backup_source_timestamp,
    uint64_t backup_commit_id)
{
    return live_timestamp != 0 && live_commit_id != 0 &&
           live_timestamp == backup_source_timestamp &&
           live_commit_id == backup_commit_id;
}

// 8. Restore source resolution:
// - Preserve explicitly selected source if still available.
// - Default to KefirHub if present in available sources.
// - Otherwise return std::nullopt (explicit selection required).
inline std::optional<BackupSourceId> ResolveRestoreSource(
    const std::vector<BackupSourceId>& available_sources,
    std::optional<BackupSourceId> explicit_source)
{
    if (explicit_source.has_value() &&
        std::ranges::find(available_sources, *explicit_source) != available_sources.end()) {
        return explicit_source;
    }
    if (std::ranges::find(available_sources, BackupSourceId::KefirHub) != available_sources.end()) {
        return BackupSourceId::KefirHub;
    }
    return std::nullopt;
}

// 9. Restore account eligibility:
// Filters account archives without silently discarding backups owned by users absent from console.
inline bool IsAccountBackupEligibleForRestore(
    bool all_accounts,
    const uint64_t backup_uid[2],
    const std::vector<std::pair<uint64_t, uint64_t>>& console_account_uids,
    const std::vector<bool>& console_account_enabled)
{
    if (all_accounts) {
        return true;
    }
    bool matches_console_user = false;
    for (size_t i = 0; i < console_account_uids.size(); i++) {
        if (backup_uid[0] == console_account_uids[i].first &&
            backup_uid[1] == console_account_uids[i].second) {
            matches_console_user = true;
            if (i < console_account_enabled.size() && console_account_enabled[i]) {
                return true;
            }
            break;
        }
    }
    // Users absent from the console are NOT silently discarded:
    return !matches_console_user;
}

// 10. Slot identity compatibility:
// Checks slot match preserving save space while supporting legacy unknown space (sp:?).
inline bool AreBackupSlotIdentitiesCompatible(
    bool is_system_a, uint8_t type_a, uint64_t id_a, bool space_known_a, uint8_t space_a, const uint64_t uid_a[2], uint16_t index_a, bool rank_known_a, uint8_t rank_a,
    bool is_system_b, uint8_t type_b, uint64_t id_b, bool space_known_b, uint8_t space_b, const uint64_t uid_b[2], uint16_t index_b, bool rank_known_b, uint8_t rank_b)
{
    if (is_system_a != is_system_b) return false;
    if (type_a != type_b) return false;
    if (id_a != id_b) return false;
    if (index_a != index_b) return false;
    if (type_a == 1 /* Account */) {
        if (uid_a[0] != uid_b[0] || uid_a[1] != uid_b[1]) return false;
    }
    if (rank_known_a && rank_known_b && rank_a != rank_b) {
        return false;
    }
    if (space_known_a && space_known_b && space_a != space_b) {
        return false;
    }
    return true;
}

// 11. Pre-mutation archive revalidation:
// Verifies exact archive identity and creator provenance before performing restore mutations.
inline bool RevalidateSelectedArchiveIdentityAndProvenance(
    const std::string& inspected_group_key,
    BackupSourceId inspected_source,
    const std::string& expected_group_key,
    BackupSourceId expected_source)
{
    return (inspected_group_key == expected_group_key) && (inspected_source == expected_source);
}

// 12. Destination slot matching:
// Evaluates whether an archive matches the destination save slot.
// Unknown legacy space is admitted; known space mismatch is rejected.
inline bool MatchesRestoreDestinationSlot(
    uint8_t archive_type,
    uint64_t /* archive_app_id */,
    bool archive_space_known,
    uint8_t archive_space_id,
    const uint64_t archive_uid[2],
    uint16_t archive_index,
    bool archive_rank_known,
    uint8_t archive_rank,
    uint8_t dest_space_id,
    const uint64_t dest_uid[2],
    uint16_t dest_index,
    uint8_t dest_rank)
{
    const bool rank_matches = (!archive_rank_known || archive_rank == dest_rank);
    if (!rank_matches || archive_index != dest_index) {
        return false;
    }
    if (archive_space_known && archive_space_id != dest_space_id) {
        return false;
    }
    if (archive_type == 1 /* Account */) {
        return (dest_uid[0] != 0 || dest_uid[1] != 0);
    }
    return (archive_uid[0] == dest_uid[0] && archive_uid[1] == dest_uid[1]);
}

} // namespace sphaira::ui::menu::save::bundle
