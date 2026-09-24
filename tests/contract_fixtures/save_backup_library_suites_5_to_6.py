# Test suites 5 and 6 for save backup library contract.
import os
import io
import zipfile
import tempfile
import shutil
from typing import Optional, List, Dict, Any, Tuple, Set
from contract_fixtures.save_backup_library_models import (
    RES_OK, FS_ERROR_PATH_NOT_FOUND, FS_ERROR_TARGET_LOCKED,
    FS_SAVE_DATA_SPACE_ID_SYSTEM, FS_SAVE_DATA_SPACE_ID_USER,
    FS_SAVE_DATA_SPACE_ID_SD_USER, FS_SAVE_DATA_TYPE_SYSTEM,
    FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_TYPE_DEVICE,
    FS_SAVE_DATA_TYPE_SYSTEM_BCAT, SAVE_TYPE_SYSTEM, SAVE_TYPE_ACCOUNT,
    SAVE_TYPE_BCAT, SAVE_TYPE_DEVICE, SAVE_TYPE_TEMPORARY,
    SAVE_TYPE_CACHE, SAVE_TYPE_SYSTEM_BCAT, NX_SAVE_META_MAGIC,
    NX_SAVE_META_NAME, DBI_SAVE_INFO_NAME, DBI_SAVE_EXTRA_NAME,
    is_system_like, make_zip_file, read_zip_entry, BackupCandidateModel,
    BackupArchiveInfoModel, EntryModel, backup_group_key, posix_to_timestamp,
    parse_backup_name_timestamp, parse_dbi_type_letter,
    parse_dbi_backup_app_id, parse_dbi_backup_index,
    inspect_backup_archive_model, check,
    pack_jksv85, make_zip
)
from contract_fixtures.save_backup_library_scanner import (
    read_backup_entries_model, collect_group_archives_model,
    collect_group_archives_legacy_model, FsSaveDataExtraDataModel,
    create_backup_if_newer_model, disambiguate_picker_labels
)

def test_suite_5_connected_picker_and_destination_boundary() -> None:
    print("[6] Running Suite 5: Connected Picker Callback & Destination Object Boundary...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        os.makedirs(dumps_dir, exist_ok=True)

        app_id = 0x0100000000090000
        source_uid = (0x1111222233334444, 0x5555666677778888)

        # 3 candidates with distinct payloads to verify exact chosen artifact readback:
        p1 = os.path.join(dumps_dir, "alpha_1.zip")
        p2 = os.path.join(dumps_dir, "alpha_2.zip")
        p3 = os.path.join(dumps_dir, "beta_3.zip")

        make_zip_file(p1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=source_uid[0], uid_high=source_uid[1], ts=1773835200), "game.sav": b"PAYLOAD_ONE"})
        make_zip_file(p2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=source_uid[0], uid_high=source_uid[1], ts=1773835200), "game.sav": b"PAYLOAD_TWO"})
        make_zip_file(p3, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=source_uid[0], uid_high=source_uid[1], ts=1773838800), "game.sav": b"PAYLOAD_THREE"})

        # Collect via scanner:
        groups = read_backup_entries_model(tmpdir)
        check(len(groups) == 1, "Expected 1 group from scanner")
        group = groups[0]

        candidates = collect_group_archives_model(group, tmpdir)
        check(len(candidates) == 3, f"Expected 3 candidates, got {len(candidates)}")

        # 1. Test Picker Label Disambiguation:
        labels = disambiguate_picker_labels(candidates)
        # candidates[0] is p3 (ts 1773838800, unique date) -> clean label
        # candidates[1] and [2] are p1 and p2 (ts 1773835200, identical date) -> appended with path
        check("(" not in labels[0], f"Unique date must NOT have path appended, got: {labels[0]}")
        check(f"({candidates[1].path})" in labels[1], f"Duplicate date 1 must have path appended, got: {labels[1]}")
        check(f"({candidates[2].path})" in labels[2], f"Duplicate date 2 must have path appended, got: {labels[2]}")

        # 2. Test Live Destination Target Object Invariance:
        # Explicit live destination target (e.g. on-console target for Account user remap):
        live_target = EntryModel(
            application_id=app_id,
            system_save_data_id=0,
            save_data_type=SAVE_TYPE_ACCOUNT,
            save_data_space_id=1,  # User space
            save_data_id=0xCAFE000000001234,
            uid_low=0xAAAABBBBCCCCDDDD,  # Destination UID differs from source archive UID!
            uid_high=0xEEEEFFFF00001111,
            save_data_index=0,
            save_data_rank=0,
            is_backup=False
        )

        # Snapshot all 9 destination target fields:
        orig_app = live_target.application_id
        orig_sys = live_target.system_save_data_id
        orig_type = live_target.save_data_type
        orig_space = live_target.save_data_space_id
        orig_save_id = live_target.save_data_id
        orig_ul = live_target.uid_low
        orig_uh = live_target.uid_high
        orig_idx = live_target.save_data_index
        orig_rank = live_target.save_data_rank

        # Model picker callback selecting index 1 (which corresponds to payload PAYLOAD_ONE or PAYLOAD_TWO):
        selected_index = 1
        chosen_candidate = candidates[selected_index]

        # Modeled Restore boundary:
        restore_recorded_calls = []

        def modeled_restore_boundary(target: EntryModel, chosen_path: str):
            # Read payload directly from chosen archive
            real_p = os.path.join(tmpdir, chosen_path.lstrip("/"))
            read_payload = read_zip_entry(real_p, "game.sav")
            restore_recorded_calls.append({
                "target_app": target.application_id,
                "target_uid": (target.uid_low, target.uid_high),
                "target_save_id": target.save_data_id,
                "chosen_path": chosen_path,
                "payload": read_payload
            })

        # Execute picker callback:
        modeled_restore_boundary(live_target, chosen_candidate.path)

        # Verify exact payload read back from chosen artifact:
        check(len(restore_recorded_calls) == 1, "Exactly 1 restore call must be recorded")
        rec = restore_recorded_calls[0]
        check(rec["chosen_path"] == chosen_candidate.path, "Chosen path must match candidate path")
        expected_payload = b"PAYLOAD_ONE" if "alpha_1" in chosen_candidate.path else b"PAYLOAD_TWO"
        check(rec["payload"] == expected_payload, f"Expected {expected_payload}, got {rec['payload']}")

        # Explicit recording boundary assertions:
        check(rec["target_app"] == live_target.application_id, "Recorded target_app must equal live target application_id")
        check(rec["target_uid"] == (live_target.uid_low, live_target.uid_high), "Recorded target_uid must equal live target UID")
        check(rec["target_save_id"] == live_target.save_data_id, "Recorded target_save_id must equal live target save_data_id")
        check(rec["target_uid"] != source_uid, "Recorded target UID must differ from source candidate UID (Account user remap)")

        # Verify ALL 9 live target fields are completely UNCHANGED after restore:
        check(live_target.application_id == orig_app, "Target application_id must be unchanged")
        check(live_target.system_save_data_id == orig_sys, "Target system_save_data_id must be unchanged")
        check(live_target.save_data_type == orig_type, "Target save_data_type must be unchanged")
        check(live_target.save_data_space_id == orig_space, "Target save_data_space_id must be unchanged")
        check(live_target.save_data_id == orig_save_id, "Target save_data_id must be unchanged")
        check(live_target.uid_low == orig_ul, "Target uid_low must be unchanged")
        check(live_target.uid_high == orig_uh, "Target uid_high must be unchanged")
        check(live_target.save_data_index == orig_idx, "Target save_data_index must be unchanged")
        check(live_target.save_data_rank == orig_rank, "Target save_data_rank must be unchanged")

    print("  -> Suite 5 (Connected Picker & Destination Boundary) PASSED.")


def test_suite_6_actions_sentinels_and_readmission() -> None:
    print("[7] Running Suite 6: Actions, Sentinels, & Readmission (Verify, Prune, Delete)...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        target_dir = os.path.join(dumps_dir, "TargetGame")
        other_dir = os.path.join(dumps_dir, "OtherGame")
        os.makedirs(target_dir, exist_ok=True)
        os.makedirs(other_dir, exist_ok=True)

        app_id = 0x01000000000C0000
        uid = (0x3333, 0x4444)

        # 3 initial valid group archives
        p_newest = os.path.join(target_dir, "20260918150000.zip")
        p_middle = os.path.join(target_dir, "20260918140000.zip")
        p_oldest = os.path.join(target_dir, "20260918130000.zip")

        make_zip_file(p_newest, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773838800), "data.bin": b"newest"})
        make_zip_file(p_middle, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773835200), "data.bin": b"middle"})
        make_zip_file(p_oldest, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773831600), "data.bin": b"oldest"})

        # Foreign sentinels:
        p_other = os.path.join(other_dir, "other_game.zip")
        make_zip_file(p_other, {NX_SAVE_META_NAME: pack_jksv85(app_id=0x01000000000D0000, uid_low=uid[0], uid_high=uid[1], ts=1773835200), "data.bin": b"other"})
        other_bytes_initial = open(p_other, "rb").read()

        p_sentinel = os.path.join(dumps_dir, "root_sentinel.bin")
        with open(p_sentinel, "wb") as f:
            f.write(b"TOP_LEVEL_SENTINEL_PAYLOAD")
        sentinel_bytes_initial = open(p_sentinel, "rb").read()

        # Run scanner to construct group:
        groups = read_backup_entries_model(tmpdir)
        group = next(g for g in groups if g.application_id == app_id)
        check(group.backup_count == 3, "Group must have backup_count 3 from scanner")

        # 1. Invariant: Post-scan added file is NOT touched or rediscovered:
        p_post_scan = os.path.join(target_dir, "post_scan_valid.zip")
        make_zip_file(p_post_scan, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773839999), "data.bin": b"post_scan"})
        post_scan_bytes_initial = open(p_post_scan, "rb").read()

        # CollectGroupArchives on existing group: iterates ONLY group.backup_members!
        cands_verify = collect_group_archives_model(group, tmpdir)
        check(len(cands_verify) == 3, "CollectGroupArchives must not discover post-scan added file!")
        check(all(c.path != "/dumps/TargetGame/post_scan_valid.zip" for c in cands_verify),
              "Post-scan added file must NOT be in collected candidates")
        check(open(p_post_scan, "rb").read() == post_scan_bytes_initial, "Post-scan file bytes must be untouched")

        # 2. Invariant: Foreign injected path into backup_members is dropped:
        group.backup_members.append(BackupCandidateModel(ts=1773835200, path="/dumps/OtherGame/other_game.zip", source=1))
        cands_foreign = collect_group_archives_model(group, tmpdir)
        check(len(cands_foreign) == 3, "Foreign injected path must be dropped by BackupGroupKey validation!")
        check(open(p_other, "rb").read() == other_bytes_initial, "Foreign file bytes must be untouched")
        group.backup_members.pop()  # Clean up injected test member

        # 3. VERIFY operation (all 3 archives readable):
        for c in cands_verify:
            real_p = os.path.join(tmpdir, c.path.lstrip("/"))
            with zipfile.ZipFile(real_p, "r") as zf:
                check(zf.testzip() is None, f"Archive {c.path} must pass ZIP integrity check")

        # 4. PRUNE operation (DeleteOlderBackups):
        # Capture newest archive full bytes before prune to verify byte preservation:
        newest_bytes_before_prune = open(p_newest, "rb").read()

        # Keeps newest, deletes middle and oldest exact paths:
        newest_cand = cands_verify[0]
        older_cands = cands_verify[1:]
        check(len(older_cands) == 2, "Prune must find 2 older candidates")

        for c in older_cands:
            os.remove(os.path.join(tmpdir, c.path.lstrip("/")))

        check(os.path.exists(p_newest), "Newest archive must remain on disk after prune")
        check(open(p_newest, "rb").read() == newest_bytes_before_prune,
              "Newest archive bytes must be perfectly preserved byte-for-byte after prune")
        check(not os.path.exists(p_middle), "Middle archive must be deleted by prune")
        check(not os.path.exists(p_oldest), "Oldest archive must be deleted by prune")
        check(open(p_other, "rb").read() == other_bytes_initial, "Foreign other_game.zip must be untouched by prune")
        check(open(p_sentinel, "rb").read() == sentinel_bytes_initial, "Root sentinel must be untouched by prune")
        check(open(p_post_scan, "rb").read() == post_scan_bytes_initial, "Post-scan file must be untouched by prune")

        # 5. Readmission with deleted member:
        # After prune, CollectGroupArchives returns only remaining 1 archive:
        cands_after_prune = collect_group_archives_model(group, tmpdir)
        check(len(cands_after_prune) == 1, f"Expected 1 candidate after prune, got {len(cands_after_prune)}")
        check(cands_after_prune[0].path == "/dumps/TargetGame/20260918150000.zip", "Remaining archive must be newest")

        # 6. DELETE operation (DeleteBackupGroups):
        for c in cands_after_prune:
            os.remove(os.path.join(tmpdir, c.path.lstrip("/")))
        check(not os.path.exists(p_newest), "Newest archive must be deleted by delete operation")

        # Nonrecursive parent directory cleanup:
        # TargetGame still contains post_scan_valid.zip, so it must NOT be removed!
        if os.path.isdir(target_dir) and not os.listdir(target_dir):
            os.rmdir(target_dir)
        check(os.path.isdir(target_dir), "Parent dir with remaining post_scan file must NOT be deleted")

        # Clean up post_scan file, now TargetGame becomes empty and is removed:
        os.remove(p_post_scan)
        if os.path.isdir(target_dir) and not os.listdir(target_dir):
            os.rmdir(target_dir)
        check(not os.path.exists(target_dir), "Empty parent directory must be removed by nonrecursive cleanup")

        # Dumps root and sentinels remain intact:
        check(os.path.isdir(dumps_dir), "Dumps root directory must NOT be deleted")
        check(os.path.exists(p_sentinel), "Root sentinel must remain intact")
        check(os.path.exists(p_other), "Other game folder and archive must remain intact")

        # 7. Empty result reporting without stale fallback:
        cands_empty = collect_group_archives_model(group, tmpdir)
        check(len(cands_empty) == 0, "CollectGroupArchives must return empty list when all members deleted")

        def simulate_restore_caller(archives: List[BackupCandidateModel], e: EntryModel) -> str:
            if len(archives) == 1:
                return f"RESTORE_PICKED:{archives[0].path}"
            elif len(archives) > 1:
                return f"POPUP_PICKER:{len(archives)}"
            elif e.is_backup:
                return "NO_BACKUPS_FOUND"
            elif e.backup_path:
                return f"STALE_FALLBACK:{e.backup_path}"
            return "FIND_LATEST_FALLBACK"

        res = simulate_restore_caller(cands_empty, group)
        check(res == "NO_BACKUPS_FOUND", f"Empty result for is_backup must report NO_BACKUPS_FOUND without fallback, got {res}")

        # Live entry (is_backup == False) preserves existing fallback:
        live_entry = EntryModel(application_id=app_id, is_backup=False, backup_path="/dumps/live_fallback.zip")
        res_live = simulate_restore_caller([], live_entry)
        check(res_live == "STALE_FALLBACK:/dumps/live_fallback.zip", "Live entry must preserve existing fallback")

        # 8. Fresh timestamp reinspection & re-sorting on arbitrary-named ZIPs:
        resort_dir = os.path.join(dumps_dir, "ResortTest")
        os.makedirs(resort_dir, exist_ok=True)
        app_resort = 0x01000000000E0000
        uid_resort = (0x6666, 0x7777)

        # Arbitrary-named ZIPs without filename timestamps (purely metadata POSIX timestamps):
        p_resort_1 = os.path.join(resort_dir, "custom_backup_one.zip")
        p_resort_2 = os.path.join(resort_dir, "custom_backup_two.zip")

        # Initially: custom_backup_one has older ts (1773831000), custom_backup_two has newer ts (1773832000)
        make_zip_file(p_resort_1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_resort, uid_low=uid_resort[0], uid_high=uid_resort[1], ts=1773831000), "data.bin": b"one"})
        make_zip_file(p_resort_2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_resort, uid_low=uid_resort[0], uid_high=uid_resort[1], ts=1773832000), "data.bin": b"two"})

        # Run scanner to produce group
        resort_groups = read_backup_entries_model(tmpdir)
        group_resort = next(g for g in resort_groups if g.application_id == app_resort)
        check(group_resort.backup_count == 2, "Resort group must have backup_count 2")

        # Verify initial order in backup_members: custom_backup_two is first, custom_backup_one is second
        initial_members_snapshot = [(m.path, m.ts) for m in group_resort.backup_members]
        check(initial_members_snapshot[0][0] == "/dumps/ResortTest/custom_backup_two.zip", "Initial order: two.zip must be first")
        check(initial_members_snapshot[1][0] == "/dumps/ResortTest/custom_backup_one.zip", "Initial order: one.zip must be second")

        # Initial collection reflects initial scan order
        cands_initial = collect_group_archives_model(group_resort, tmpdir)
        check(cands_initial[0].path == "/dumps/ResortTest/custom_backup_two.zip", "Initial collect: two.zip first")
        check(cands_initial[1].path == "/dumps/ResortTest/custom_backup_one.zip", "Initial collect: one.zip second")

        # Overwrite older path (custom_backup_one) with newer POSIX timestamp metadata (1773839999 > 1773832000):
        make_zip_file(p_resort_1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_resort, uid_low=uid_resort[0], uid_high=uid_resort[1], ts=1773839999), "data.bin": b"one_updated"})

        # Collect again via collect_group_archives_model:
        cands_resorted = collect_group_archives_model(group_resort, tmpdir)
        check(len(cands_resorted) == 2, "CollectGroupArchives must return both members after timestamp update")

        # Collection must reflect NEW order: custom_backup_one is now first!
        check(cands_resorted[0].path == "/dumps/ResortTest/custom_backup_one.zip",
              "Fresh reinspection must re-sort custom_backup_one to 1st place due to newer timestamp")
        check(cands_resorted[1].path == "/dumps/ResortTest/custom_backup_two.zip",
              "custom_backup_two must now be 2nd place")
        check(cands_resorted[0].ts == posix_to_timestamp(1773839999),
              "Re-inspected timestamp must reflect updated POSIX timestamp")

        # Assert that group.backup_members scan inventory remains completely UNCHANGED:
        current_members = [(m.path, m.ts) for m in group_resort.backup_members]
        check(current_members == initial_members_snapshot,
              "group.backup_members scan inventory must remain completely unchanged across reinspection and resort")

        # 9. Post-scan admission changes on actual scan-produced groups:
        adm_dir = os.path.join(dumps_dir, "AdmissionTest")
        os.makedirs(adm_dir, exist_ok=True)
        app_adm = 0x01000000000F0000
        uid_adm = (0x8888, 0x9999)

        p_adm_corrupt = os.path.join(adm_dir, "member_corrupt.zip")
        p_adm_diff = os.path.join(adm_dir, "member_diff.zip")
        p_adm_rank = os.path.join(adm_dir, "member_rank.zip")
        p_adm_valid = os.path.join(adm_dir, "member_valid.zip")

        # Create all 4 with valid metadata for app_adm (all rank 0) initially
        make_zip_file(p_adm_corrupt, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773831000), "data.bin": b"to_corrupt"})
        make_zip_file(p_adm_diff, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773832000), "data.bin": b"to_change_group"})
        make_zip_file(p_adm_rank, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773832500), "data.bin": b"to_mutate_rank"})
        make_zip_file(p_adm_valid, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773833000), "data.bin": b"stay_valid"})

        # Run actual scanner to construct group
        adm_groups = read_backup_entries_model(tmpdir)
        group_adm = next(g for g in adm_groups if g.application_id == app_adm)
        check(group_adm.backup_count == 4, f"Group must have 4 members from scanner, got {group_adm.backup_count}")
        check(len(group_adm.backup_members) == 4, "backup_members must have 4 items")

        # Now apply post-scan mutations on disk:
        # 1. Overwrite one retained member with corrupt bytes (invalid ZIP data)
        with open(p_adm_corrupt, "wb") as f:
            f.write(b"CORRUPT_NOT_A_VALID_ZIP_HEADER_DATA_1234567890")

        # 2. Overwrite another with valid metadata for a DIFFERENT group (different app_id)
        app_other = 0x01000000000F0001
        meta_diff = pack_jksv85(app_id=app_other, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=0, ts=1773835000)
        make_zip_file(p_adm_diff, {NX_SAVE_META_NAME: meta_diff, "data.bin": b"different_group_payload"})
        diff_bytes_after = open(p_adm_diff, "rb").read()

        # 3. Overwrite another with mutated rank (rank=1 instead of rank=0)
        meta_rank_mut = pack_jksv85(app_id=app_adm, uid_low=uid_adm[0], uid_high=uid_adm[1], rank=1, ts=1773836000)
        make_zip_file(p_adm_rank, {NX_SAVE_META_NAME: meta_rank_mut, "data.bin": b"mutated_rank_payload"})
        rank_bytes_after = open(p_adm_rank, "rb").read()

        # 4. Keep one valid member (p_adm_valid) untouched

        # Verify collection returns ONLY the valid member:
        cands_adm = collect_group_archives_model(group_adm, tmpdir)
        check(len(cands_adm) == 1, f"Collection must return only 1 valid member, got {len(cands_adm)}")
        check(cands_adm[0].path == "/dumps/AdmissionTest/member_valid.zip",
              f"Collection must return member_valid.zip, got {cands_adm[0].path}")

        # Verify changed-group and mutated-rank bytes are unaltered on disk:
        check(open(p_adm_diff, "rb").read() == diff_bytes_after,
              "Changed-group archive bytes must remain unaltered on disk during inspection")
        check(open(p_adm_rank, "rb").read() == rank_bytes_after,
              "Mutated-rank archive bytes must remain unaltered on disk during inspection")

        # Verify empty on removal with no stale fallback:
        os.remove(p_adm_valid)
        cands_adm_empty = collect_group_archives_model(group_adm, tmpdir)
        check(len(cands_adm_empty) == 0, f"Collection must return empty after valid member removed, got {len(cands_adm_empty)}")
        sim_res = simulate_restore_caller(cands_adm_empty, group_adm)
        check(sim_res == "NO_BACKUPS_FOUND",
              f"Caller must report NO_BACKUPS_FOUND with no stale fallback on removal, got {sim_res}")

    print("  -> Suite 6 (Actions, Sentinels, & Readmission) PASSED.")
