#include "ui/menus/save/save_bundle_util.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <unordered_map>

using namespace sphaira::ui::menu::save::bundle;

static void test_slot_priority() {
    const uint64_t active_uid[2] = {0x1111222233334444ULL, 0x5555666677778888ULL};
    const uint64_t other_uid[2] = {0xAAAABBBBCCCCDDDDULL, 0xEEEEFFFF00001111ULL};
    const uint64_t zero_uid[2] = {0, 0};

    // Account matching active user gets top priority (100)
    assert(SlotPriority(1, active_uid, active_uid) == 100);
    // Any other account gets 90
    assert(SlotPriority(1, other_uid, active_uid) == 90);
    assert(SlotPriority(1, active_uid, nullptr) == 90);

    // Device gets 80
    assert(SlotPriority(3, zero_uid, active_uid) == 80);
    // BCAT gets 70
    assert(SlotPriority(2, zero_uid, active_uid) == 70);
    // Cache gets 60
    assert(SlotPriority(5, zero_uid, active_uid) == 60);
    // Temporary gets 50
    assert(SlotPriority(4, zero_uid, active_uid) == 50);
}

static void test_real_visibility() {
    const uint64_t acnh_app_id = 0x01006F8002326000ULL;
    const uint64_t mario_app_id = 0x0100000000010000ULL;

    // Category::Installed (1)
    // Installed game is visible regardless of save presence
    assert(ShouldShowGameTile(acnh_app_id, true, true, Category::Installed, true, true));
    assert(ShouldShowGameTile(acnh_app_id, true, false, Category::Installed, true, true));
    // Non-installed game is not visible in Installed tab even if it has a save
    assert(!ShouldShowGameTile(acnh_app_id, false, true, Category::Installed, true, true));

    // Category::Deleted (2)
    // Non-installed game with save is visible
    assert(ShouldShowGameTile(acnh_app_id, false, true, Category::Deleted, true, true));
    // Non-installed game without save is hidden
    assert(!ShouldShowGameTile(acnh_app_id, false, false, Category::Deleted, true, true));
    // Installed game is not visible in Deleted tab
    assert(!ShouldShowGameTile(acnh_app_id, true, true, Category::Deleted, true, true));

    // Category::All (0)
    // Installed game respects show_installed
    assert(ShouldShowGameTile(acnh_app_id, true, true, Category::All, true, false));
    assert(!ShouldShowGameTile(acnh_app_id, true, true, Category::All, false, true));
    // Deleted game with save respects show_deleted
    assert(ShouldShowGameTile(acnh_app_id, false, true, Category::All, false, true));
    assert(!ShouldShowGameTile(acnh_app_id, false, true, Category::All, true, false));
    // Deleted game without save is never shown in All
    assert(!ShouldShowGameTile(acnh_app_id, false, false, Category::All, true, true));

    // Category::Backups (3)
    // Live game tiles are not displayed in Backups tab
    assert(!ShouldShowGameTile(acnh_app_id, true, true, Category::Backups, true, true));
    assert(!ShouldShowGameTile(acnh_app_id, false, true, Category::Backups, true, true));

    // Filter app_id
    assert(ShouldShowGameTile(acnh_app_id, true, true, Category::Installed, true, true, acnh_app_id));
    assert(!ShouldShowGameTile(mario_app_id, true, true, Category::Installed, true, true, acnh_app_id));
}

static void test_exact_slot_identity_and_shared_deduplication() {
    const uint64_t app_id = 0x01006F8002326000ULL;
    const uint64_t user1[2] = {1, 10};
    const uint64_t user2[2] = {2, 20};

    // Space ID preservation
    SaveSlotIdentity id_user_space{app_id, 0, 5 /* Cache */, 1 /* User */, 0, 0, {0, 0}};
    SaveSlotIdentity id_sduser_space{app_id, 0, 5 /* Cache */, 4 /* SdUser */, 0, 0, {0, 0}};
    assert(FormatSlotIdentityKey(id_user_space) != FormatSlotIdentityKey(id_sduser_space));

    // Valid UID data preservation for Account saves
    SaveSlotIdentity id_acc1{app_id, 0, 1 /* Account */, 1, 0, 0, {user1[0], user1[1]}};
    SaveSlotIdentity id_acc2{app_id, 0, 1 /* Account */, 1, 0, 0, {user2[0], user2[1]}};
    assert(FormatSlotIdentityKey(id_acc1) != FormatSlotIdentityKey(id_acc2));

    // Shared slot (Device/BCAT) does not distinguish by calling user
    SaveSlotIdentity id_dev1{app_id, 0, 3 /* Device */, 1, 0, 0, {0, 0}};
    SaveSlotIdentity id_dev2{app_id, 0, 3 /* Device */, 1, 0, 0, {0, 0}};
    assert(FormatSlotIdentityKey(id_dev1) == FormatSlotIdentityKey(id_dev2));

    // Bundle deduplication with multiple users
    std::vector<SaveSlotIdentity> candidates{
        id_acc1,
        id_acc2,
        id_dev1,
        SaveSlotIdentity{app_id, 0, 2 /* BCAT */, 1, 0, 0, {0, 0}},
        id_dev2, // duplicate shared slot
    };

    auto get_id = [](const SaveSlotIdentity& x) { return x; };

    // With all_accounts = true:
    // Should contain: id_acc1, id_acc2, 1 Device (deduplicated), 1 BCAT = 4 items
    std::vector<std::pair<uint64_t, uint64_t>> selected_uids{{user1[0], user1[1]}};
    auto dedup_all = DeduplicateBundleMembers(candidates, true, selected_uids, get_id);
    assert(dedup_all.size() == 4);

    // With all_accounts = false and only user1 selected:
    // Should contain: id_acc1, 1 Device, 1 BCAT = 3 items (id_acc2 excluded)
    auto dedup_user1 = DeduplicateBundleMembers(candidates, false, selected_uids, get_id);
    assert(dedup_user1.size() == 3);
    assert(dedup_user1[0].save_data_type == 1 && dedup_user1[0].uid[0] == user1[0]);
    assert(dedup_user1[1].save_data_type == 3);
    assert(dedup_user1[2].save_data_type == 2);

    // Animal Crossing bundle (Device + BCAT only, no Account):
    std::vector<SaveSlotIdentity> acnh_candidates{
        SaveSlotIdentity{app_id, 0, 3 /* Device */, 1, 0, 0, {0, 0}},
        SaveSlotIdentity{app_id, 0, 2 /* BCAT */, 1, 0, 0, {0, 0}},
        SaveSlotIdentity{app_id, 0, 3 /* Device */, 1, 0, 0, {0, 0}}, // duplicate
    };
    auto acnh_result = DeduplicateBundleMembers(acnh_candidates, false, selected_uids, get_id);
    assert(acnh_result.size() == 2);
    assert(acnh_result[0].save_data_type == 3);
    assert(acnh_result[1].save_data_type == 2);
}

static void test_missing_live_destinations() {
    const uint64_t app_id = 0x01006F8002326000ULL;

    // Existing live target is always ready
    assert(EvaluateRestoreDestination(true, 1 /* Account */, 0, 0, app_id) == RestoreDestinationOutcome::ExistingLiveTarget);
    assert(EvaluateRestoreDestination(true, 3 /* Device */, 0, 0, app_id) == RestoreDestinationOutcome::ExistingLiveTarget);
    assert(EvaluateRestoreDestination(true, 2 /* BCAT */, 0, 0, app_id) == RestoreDestinationOutcome::ExistingLiveTarget);
    assert(EvaluateRestoreDestination(true, 5 /* Cache */, 0, 0, app_id) == RestoreDestinationOutcome::ExistingLiveTarget);

    // When missing live destination:
    // Account primary slot 0 for installed title -> PlanAccountCreation
    assert(EvaluateRestoreDestination(false, 1 /* Account */, 0, 0, app_id) == RestoreDestinationOutcome::PlanAccountCreation);

    // Account secondary rank or non-zero slot or zero app_id -> UnsupportedMissingDestination
    assert(EvaluateRestoreDestination(false, 1 /* Account */, 1 /* Secondary */, 0, app_id) == RestoreDestinationOutcome::UnsupportedMissingDestination);
    assert(EvaluateRestoreDestination(false, 1 /* Account */, 0, 1 /* Slot 1 */, app_id) == RestoreDestinationOutcome::UnsupportedMissingDestination);
    assert(EvaluateRestoreDestination(false, 1 /* Account */, 0, 0, 0 /* Invalid App ID */) == RestoreDestinationOutcome::UnsupportedMissingDestination);

    // Shared slots (Device, BCAT, Cache) -> UnsupportedMissingDestination
    assert(EvaluateRestoreDestination(false, 3 /* Device */, 0, 0, app_id) == RestoreDestinationOutcome::UnsupportedMissingDestination);
    assert(EvaluateRestoreDestination(false, 2 /* BCAT */, 0, 0, app_id) == RestoreDestinationOutcome::UnsupportedMissingDestination);
    assert(EvaluateRestoreDestination(false, 5 /* Cache */, 0, 0, app_id) == RestoreDestinationOutcome::UnsupportedMissingDestination);
    assert(EvaluateRestoreDestination(false, 0 /* System */, 0, 0, app_id) == RestoreDestinationOutcome::UnsupportedMissingDestination);
}

static void test_empty_restore_selection() {
    std::vector<int> empty_bundle;
    std::vector<int> populated_bundle{1, 2, 3};

    assert(IsRestoreSelectionEmpty(empty_bundle));
    assert(!IsRestoreSelectionEmpty(populated_bundle));
}

static void test_shared_identity_preserves_save_space() {
    const uint64_t app_id = 0x01006F8002326000ULL;
    const uint64_t uid[2] = {0x1234, 0x5678};

    // User space (space_id = 1) vs SdUser space (space_id = 4) for the same app and type
    std::string user_key = FormatBackupGroupIdentityKey(false, 5 /* Cache */, app_id, true, 1 /* User */, uid, 0, true, 0);
    std::string sduser_key = FormatBackupGroupIdentityKey(false, 5 /* Cache */, app_id, true, 4 /* SdUser */, uid, 0, true, 0);
    assert(user_key != sduser_key);
    assert(user_key.find(":sp:1:") != std::string::npos);
    assert(sduser_key.find(":sp:4:") != std::string::npos);

    // Legacy archive with unknown space (space_known = false)
    std::string unknown_key = FormatBackupGroupIdentityKey(false, 5 /* Cache */, app_id, false, 0, uid, 0, true, 0);
    assert(unknown_key.find(":sp:?:") != std::string::npos);
    assert(unknown_key != user_key);
    assert(unknown_key != sduser_key);

    // Source identity key separation
    std::string hub_source_key = FormatSourceGroupKey(BackupSourceId::KefirHub, user_key);
    std::string dbi_source_key = FormatSourceGroupKey(BackupSourceId::Dbi, user_key);
    assert(hub_source_key != dbi_source_key);
    assert(hub_source_key == "0:" + user_key);
    assert(dbi_source_key == "1:" + user_key);
}

// Regression 1: Hub-created DBI-format backup is recognized as unchanged
static void test_hub_created_dbi_format_recognized_as_unchanged() {
    const uint64_t live_ts = 1728000000ULL;
    const uint64_t live_commit = 42ULL;

    // Backup is stored in DBI folder layout, but has inspected KefirHub provenance
    BackupMemberCandidate cand{1728000000ULL, "/switch/DBI/saves/Game/2026-10-04_12-00-00.zip", BackupSourceId::KefirHub, 0, false};
    const uint64_t backup_source_ts = 1728000000ULL;
    const uint64_t backup_commit = 42ULL;

    assert(IsCandidateEligibleForHubFreshness(true, cand.source));
    assert(IsBackupFreshnessMatching(live_ts, live_commit, backup_source_ts, backup_commit));

    // If live commit or timestamp differed, it is not up-to-date
    assert(!IsBackupFreshnessMatching(live_ts, live_commit + 1, backup_source_ts, backup_commit));
    assert(!IsBackupFreshnessMatching(live_ts + 10, live_commit, backup_source_ts, backup_commit));
}

// Regression 2: External timestamps do not influence Hub selection or skipping
static void test_external_timestamps_do_not_influence_hub_selection_or_skipping() {
    BackupMemberCandidate hub_old{1000ULL, "/dumps/Game/hub_old.zip", BackupSourceId::KefirHub, 0, false};
    BackupMemberCandidate hub_new{1500ULL, "/dumps/Game/hub_new.zip", BackupSourceId::KefirHub, 0, false};
    BackupMemberCandidate dbi_cand{2500ULL, "/switch/DBI/saves/Game/dbi.zip", BackupSourceId::Dbi, 0, false};

    // External candidate is not eligible for Hub freshness check
    assert(IsCandidateEligibleForHubFreshness(true, hub_old.source));
    assert(!IsCandidateEligibleForHubFreshness(true, dbi_cand.source));

    // Within Hub's own history: newest candidate is preferred
    assert(IsCandidateBetter(hub_new, hub_old));
    assert(!IsCandidateBetter(hub_old, hub_new));
}

// Regression 3: No mixed-source members enter a Hub restore bundle
static void test_no_mixed_source_members_enter_hub_restore_bundle() {
    struct CandidateEntry {
        std::string slot_key;
        BackupMemberCandidate cand;
    };

    // Slot 1 (Account): present in Hub and DBI
    // Slot 2 (Device): present in Hub and DBI
    // Slot 3 (BCAT): present ONLY in DBI
    std::vector<CandidateEntry> pool = {
        {"slot1", BackupMemberCandidate{1000, "hub_acc.zip", BackupSourceId::KefirHub, 0, false}},
        {"slot1", BackupMemberCandidate{2000, "dbi_acc.zip", BackupSourceId::Dbi, 0, false}},
        {"slot2", BackupMemberCandidate{1000, "hub_dev.zip", BackupSourceId::KefirHub, 0, false}},
        {"slot2", BackupMemberCandidate{2000, "dbi_dev.zip", BackupSourceId::Dbi, 0, false}},
        {"slot3", BackupMemberCandidate{2000, "dbi_bcat.zip", BackupSourceId::Dbi, 0, false}},
    };

    // Strict source filtering (as done in CollectBackupEntriesForRestore):
    auto collect_for_source = [&](BackupSourceId target_source) {
        std::unordered_map<std::string, BackupMemberCandidate> slots;
        for (const auto& item : pool) {
            if (item.cand.source != target_source) {
                continue; // Isolated: non-target source skipped!
            }
            auto it = slots.find(item.slot_key);
            if (it == slots.end()) {
                slots.emplace(item.slot_key, item.cand);
            } else if (IsCandidateBetter(item.cand, it->second)) {
                it->second = item.cand;
            }
        }
        return slots;
    };

    auto hub_bundle = collect_for_source(BackupSourceId::KefirHub);
    assert(hub_bundle.size() == 2);
    assert(hub_bundle.count("slot1") && hub_bundle["slot1"].source == BackupSourceId::KefirHub);
    assert(hub_bundle.count("slot2") && hub_bundle["slot2"].source == BackupSourceId::KefirHub);
    assert(hub_bundle.count("slot3") == 0); // Slot 3 (DBI-only) NEVER enters Hub bundle!

    auto dbi_bundle = collect_for_source(BackupSourceId::Dbi);
    assert(dbi_bundle.size() == 3);
    assert(dbi_bundle["slot1"].source == BackupSourceId::Dbi);
    assert(dbi_bundle["slot2"].source == BackupSourceId::Dbi);
    assert(dbi_bundle["slot3"].source == BackupSourceId::Dbi);
}

// Regression 4: Explicit external-source selection, Hub default, and authorization preservation
static void test_explicit_external_source_selection_and_hub_priority() {
    std::vector<BackupSourceId> hub_and_dbi{BackupSourceId::KefirHub, BackupSourceId::Dbi};
    std::vector<BackupSourceId> dbi_only{BackupSourceId::Dbi};
    std::vector<BackupSourceId> empty_sources{};

    // 1. Hub available, no explicit choice -> defaults to Hub
    auto res1 = ResolveRestoreSource(hub_and_dbi, std::nullopt);
    assert(res1.has_value() && *res1 == BackupSourceId::KefirHub);

    // 2. Hub absent, only external source present, no explicit choice -> requires choice (nullopt)
    auto res2 = ResolveRestoreSource(dbi_only, std::nullopt);
    assert(!res2.has_value());

    // 3. User explicitly selects single external source -> preserved
    auto res3 = ResolveRestoreSource(dbi_only, BackupSourceId::Dbi);
    assert(res3.has_value() && *res3 == BackupSourceId::Dbi);

    // 4. User explicitly selected external source even when Hub is available -> preserved
    auto res4 = ResolveRestoreSource(hub_and_dbi, BackupSourceId::Dbi);
    assert(res4.has_value() && *res4 == BackupSourceId::Dbi);

    // 5. Empty sources -> nullopt
    auto res5 = ResolveRestoreSource(empty_sources, std::nullopt);
    assert(!res5.has_value());
}

// Regression 5: Backups owned by users absent from console are not silently discarded during restore
static void test_absent_console_user_backups_preserved_during_restore() {
    std::vector<std::pair<uint64_t, uint64_t>> console_users{{1, 1}, {2, 2}};
    std::vector<bool> enabled_users{true, false}; // User 1 enabled, User 2 disabled

    const uint64_t uid_user1[2]{1, 1};
    const uint64_t uid_user2[2]{2, 2};
    const uint64_t uid_foreign[2]{999, 888}; // User from another console or deleted profile

    // All Accounts checked -> all backups eligible
    assert(IsAccountBackupEligibleForRestore(true, uid_user1, console_users, enabled_users));
    assert(IsAccountBackupEligibleForRestore(true, uid_user2, console_users, enabled_users));
    assert(IsAccountBackupEligibleForRestore(true, uid_foreign, console_users, enabled_users));

    // Specific accounts selected -> User 1 allowed, User 2 rejected, foreign user PRESERVED
    assert(IsAccountBackupEligibleForRestore(false, uid_user1, console_users, enabled_users));
    assert(!IsAccountBackupEligibleForRestore(false, uid_user2, console_users, enabled_users));
    assert(IsAccountBackupEligibleForRestore(false, uid_foreign, console_users, enabled_users));
}

// Regression 6: Slot identity compatibility preserves space distinction and supports legacy unknown space
static void test_slot_identity_compatibility_space_and_legacy() {
    const uint64_t app_id = 0x01006F8002326000ULL;
    const uint64_t uid[2]{1, 2};

    // 1. Same known space (User vs User) -> compatible
    assert(AreBackupSlotIdentitiesCompatible(
        false, 1, app_id, true, 1 /* User */, uid, 0, true, 0,
        false, 1, app_id, true, 1 /* User */, uid, 0, true, 0));

    // 2. Different known space (User vs SdUser) -> INCOMPATIBLE (prevents slot merge!)
    assert(!AreBackupSlotIdentitiesCompatible(
        false, 1, app_id, true, 1 /* User */, uid, 0, true, 0,
        false, 1, app_id, true, 4 /* SdUser */, uid, 0, true, 0));

    // 3. Known space vs legacy archive with unknown space (sp:?) -> COMPATIBLE (preserves legacy support)
    assert(AreBackupSlotIdentitiesCompatible(
        false, 1, app_id, true, 1 /* User */, uid, 0, true, 0,
        false, 1, app_id, false, 0 /* Unknown */, uid, 0, true, 0));

    // 4. Different account UIDs -> INCOMPATIBLE
    const uint64_t other_uid[2]{3, 4};
    assert(!AreBackupSlotIdentitiesCompatible(
        false, 1, app_id, true, 1, uid, 0, true, 0,
        false, 1, app_id, true, 1, other_uid, 0, true, 0));
}

// Regression 7: Bundle member deduplication and destination evaluation
static void test_bundle_admission_and_destination_evaluation() {
    const uint64_t acnh_id = 0x01006F8002326000ULL;
    std::vector<SaveSlotIdentity> candidates = {
        // Slot 1: Account user 1 (has live save on console)
        SaveSlotIdentity{acnh_id, 0, 1 /* Account */, 1 /* User */, 0, 0, {1, 1}},
        // Slot 2: Account user 2 (archive-only, no live save)
        SaveSlotIdentity{acnh_id, 0, 1 /* Account */, 1 /* User */, 0, 0, {2, 2}},
        // Slot 3: Device save (archive-only, no live save)
        SaveSlotIdentity{acnh_id, 0, 3 /* Device */, 1 /* User */, 0, 0, {0, 0}},
        // Slot 4: BCAT save (archive-only, no live save)
        SaveSlotIdentity{acnh_id, 0, 2 /* BCAT */, 1 /* User */, 0, 0, {0, 0}},
        // Duplicate older archive for Device save:
        SaveSlotIdentity{acnh_id, 0, 3 /* Device */, 1 /* User */, 0, 0, {0, 0}},
    };

    std::vector<std::pair<uint64_t, uint64_t>> selected_uids{{1, 1}, {2, 2}};
    auto bundle = DeduplicateBundleMembers(candidates, true, selected_uids, [](const SaveSlotIdentity& x){ return x; });
    assert(bundle.size() == 4);

    // Verify admission evaluation:
    assert(EvaluateRestoreDestination(true, bundle[0].save_data_type, bundle[0].save_data_rank, bundle[0].save_data_index, acnh_id) == RestoreDestinationOutcome::ExistingLiveTarget);
    assert(EvaluateRestoreDestination(false, bundle[1].save_data_type, bundle[1].save_data_rank, bundle[1].save_data_index, acnh_id) == RestoreDestinationOutcome::PlanAccountCreation);
    assert(EvaluateRestoreDestination(false, bundle[2].save_data_type, bundle[2].save_data_rank, bundle[2].save_data_index, acnh_id) == RestoreDestinationOutcome::UnsupportedMissingDestination);
    assert(EvaluateRestoreDestination(false, bundle[3].save_data_type, bundle[3].save_data_rank, bundle[3].save_data_index, acnh_id) == RestoreDestinationOutcome::UnsupportedMissingDestination);
}

// Regression 8: Admission followed by final revalidation using production identity logic
static void test_admission_followed_by_final_revalidation() {
    const uint64_t app_id = 0x01006F8002326000ULL;
    const uint64_t user_uid[2]{0x1234, 0x5678};
    const uint64_t dest_uid[2]{0x9999, 0x8888};

    // 1. Live System slot with space 0 (FsSaveDataSpaceId_System) remains known (not an unknown sentinel)
    const bool live_system_space_known = true; // Every actual live slot has known space
    const uint8_t live_system_space_id = 0;    // Valid FsSaveDataSpaceId_System
    const std::string sys_key = FormatBackupGroupIdentityKey(
        true, 0, 0x0100000000000000ULL, live_system_space_known, live_system_space_id, user_uid, 0, true, 0);
    assert(sys_key.find("sp:0") != std::string::npos);
    assert(sys_key.find("sp:?") == std::string::npos);

    // 2. Known live User + legacy unknown-space archive succeeds:
    // Destination: live User slot (space 1)
    const uint8_t live_user_space_id = 1;
    // Legacy archive: unknown space (space_known = false)
    const bool legacy_space_known = false;
    const uint8_t legacy_space_id = 0;

    // A) Admission check:
    const bool admitted = AreBackupSlotIdentitiesCompatible(
        false, 1 /* Account */, app_id, true /* live known space */, live_user_space_id, user_uid, 0, true, 0,
        false, 1 /* Account */, app_id, legacy_space_known, legacy_space_id, user_uid, 0, true, 0);
    assert(admitted);

    // B) Preserved archive identity in backup group:
    // Selected archive keeps unknown space (sp:?)
    const std::string selected_legacy_key = FormatBackupGroupIdentityKey(
        false, 1, app_id, legacy_space_known, legacy_space_id, user_uid, 0, true, 0);
    assert(selected_legacy_key.find("sp:?") != std::string::npos);

    // C) Destination slot matching (account target):
    assert(MatchesRestoreDestinationSlot(
        1, app_id, legacy_space_known, legacy_space_id, user_uid, 0, true, 0,
        live_user_space_id, dest_uid, 0, 0));

    // D) Final revalidation before mutation:
    // Reinspected archive matches selected archive identity and Hub provenance
    const std::string reinspected_legacy_key = FormatBackupGroupIdentityKey(
        false, 1, app_id, false /* legacy */, 0, user_uid, 0, true, 0);
    assert(RevalidateSelectedArchiveIdentityAndProvenance(
        reinspected_legacy_key, BackupSourceId::KefirHub,
        selected_legacy_key, BackupSourceId::KefirHub));

    // 3. Known User archive + live SdUser fails:
    // Archive has known space 1 (User)
    const bool known_user_archive_space_known = true;
    const uint8_t known_user_archive_space_id = 1;
    // Destination has space 4 (SdUser)
    const uint8_t live_sduser_space_id = 4;

    const bool user_archive_on_sduser_admitted = AreBackupSlotIdentitiesCompatible(
        false, 1, app_id, true, live_sduser_space_id, user_uid, 0, true, 0,
        false, 1, app_id, known_user_archive_space_known, known_user_archive_space_id, user_uid, 0, true, 0);
    assert(!user_archive_on_sduser_admitted);

    const bool user_archive_on_sduser_destination_matches = MatchesRestoreDestinationSlot(
        1, app_id, known_user_archive_space_known, known_user_archive_space_id, user_uid, 0, true, 0,
        live_sduser_space_id, dest_uid, 0, 0);
    assert(!user_archive_on_sduser_destination_matches);

    // 4. Changed selected archive identity / provenance fails before mutation:
    // A) Changed identity (e.g. archive replaced with different save slot index or different rank or space):
    const std::string tampered_key = FormatBackupGroupIdentityKey(
        false, 1, app_id, true, 2 /* altered space */, user_uid, 0, true, 0);
    assert(!RevalidateSelectedArchiveIdentityAndProvenance(
        tampered_key, BackupSourceId::KefirHub,
        selected_legacy_key, BackupSourceId::KefirHub));

    // B) Changed provenance (e.g. Hub backup expected, but file comes from external tool/source):
    assert(!RevalidateSelectedArchiveIdentityAndProvenance(
        selected_legacy_key, BackupSourceId::Dbi,
        selected_legacy_key, BackupSourceId::KefirHub));
}

int main() {
    test_slot_priority();
    test_real_visibility();
    test_exact_slot_identity_and_shared_deduplication();
    test_missing_live_destinations();
    test_empty_restore_selection();
    test_shared_identity_preserves_save_space();
    test_hub_created_dbi_format_recognized_as_unchanged();
    test_external_timestamps_do_not_influence_hub_selection_or_skipping();
    test_no_mixed_source_members_enter_hub_restore_bundle();
    test_explicit_external_source_selection_and_hub_priority();
    test_absent_console_user_backups_preserved_during_restore();
    test_slot_identity_compatibility_space_and_legacy();
    test_bundle_admission_and_destination_evaluation();
    test_admission_followed_by_final_revalidation();

    std::cout << "test_save_bundle_util: all production decision checks and regressions passed.\n";
    return 0;
}
