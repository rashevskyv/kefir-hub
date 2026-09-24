# Test suites 1 to 4 for save backup library contract.
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
    pack_jksv85, pack_jksv_tail86, pack_sphaira128, pack_dbi_raw512, make_zip
)
from contract_fixtures.save_backup_library_scanner import (
    read_backup_entries_model, collect_group_archives_model,
    collect_group_archives_legacy_model, FsSaveDataExtraDataModel,
    create_backup_if_newer_model, disambiguate_picker_labels
)

def test_suite_1_connected_scanner_and_proven_regression() -> None:
    print("[2] Running Suite 1: Connected Scanner & Proven Regression (Arbitrary ZIPs in /dumps)...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        os.makedirs(dumps_dir, exist_ok=True)

        app_id = 0x0100000000010000
        uid = (0x1111222233334444, 0x5555666677778888)

        # Create two valid metadata ZIPs directly in /dumps with arbitrary filenames
        zip1_path = os.path.join(dumps_dir, "my_custom_backup_alpha.zip")
        zip2_path = os.path.join(dumps_dir, "my_custom_backup_beta.zip")

        # Use actual POSIX seconds: 1773835200 (alpha) and 1773838800 (beta)
        meta1 = pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773835200)
        meta2 = pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773838800)

        make_zip_file(zip1_path, {NX_SAVE_META_NAME: meta1, "game.sav": b"alpha_payload"})
        make_zip_file(zip2_path, {NX_SAVE_META_NAME: meta2, "game.sav": b"beta_payload"})

        # Actual filesystem scanner builds the EntryModel (NOT manually supplied!)
        groups = read_backup_entries_model(tmpdir)
        check(len(groups) == 1, f"Scanner must find exactly 1 group, got {len(groups)}")
        group = groups[0]

        # Verify discovery results:
        check(group.backup_count == 2, f"Group backup_count must be 2, got {group.backup_count}")
        check(len(group.backup_members) == 2, f"Group backup_members length must be 2, got {len(group.backup_members)}")
        check(group.backup_members[0].path == "/dumps/my_custom_backup_beta.zip", "Newer beta must be 1st in backup_members")
        check(group.backup_members[1].path == "/dumps/my_custom_backup_alpha.zip", "Older alpha must be 2nd in backup_members")

        # Representative attributes match backup_members[0]:
        check(group.backup_path == group.backup_members[0].path,
              "Representative backup_path must match backup_members[0].path")
        check(group.backup_timestamp == group.backup_members[0].ts,
              "Representative backup_timestamp must match backup_members[0].ts")
        check(group.backup_timestamp == posix_to_timestamp(1773838800),
              "Timestamp must match posix_to_timestamp of 1773838800")

        # Demonstrate the proven legacy defect:
        # Pre-861 narrow discovery looking in /dumps/<game_name>/ finds 0 items, then falls back to ONLY representative path!
        legacy_candidates = collect_group_archives_legacy_model(group, tmpdir)
        check(len(legacy_candidates) == 1,
              f"Proven Defect: legacy narrow discovery finds 0 and falls back to only 1 candidate, got {len(legacy_candidates)}")
        check(legacy_candidates[0].path == "/dumps/my_custom_backup_beta.zip",
              "Legacy fallback returned only beta, losing alpha!")

        # Contrast with new .861 library membership:
        # Uses retained members, re-inspects, and returns BOTH candidates!
        new_candidates = collect_group_archives_model(group, tmpdir)
        check(len(new_candidates) == 2,
              f"New library CollectGroupArchives must return both candidates! Got {len(new_candidates)}")
        check(new_candidates[0].path == "/dumps/my_custom_backup_beta.zip", "Beta must be first")
        check(new_candidates[1].path == "/dumps/my_custom_backup_alpha.zip", "Alpha must be second")
        check(len(new_candidates) == group.backup_count, "Candidate count must equal library backup_count")

    print("  -> Suite 1 (Connected Scanner & Proven Regression) PASSED.")


def test_suite_2_location_matrix_priority_and_tiebreaking() -> None:
    print("[3] Running Suite 2: Location Matrix, Priorities, Tie-breaking, & Overlapping Roots...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dbi_dir = os.path.join(tmpdir, "switch", "DBI", "saves", "GameAlpha")
        dbi_date_dir = os.path.join(dbi_dir, "2026-09-18")
        dumps_root = os.path.join(tmpdir, "dumps")
        dumps_l2 = os.path.join(dumps_root, "GameAlpha")
        dumps_l3 = os.path.join(dumps_l2, "sub")
        custom_root = os.path.join(tmpdir, "custom1")
        custom_l2 = os.path.join(custom_root, "dir1")
        custom_l3 = os.path.join(custom_l2, "dir2")

        for d in (dbi_date_dir, dumps_l3, custom_l3):
            os.makedirs(d, exist_ok=True)

        app_id = 0x0100000000020000
        uid = (0xAAAA111122223333, 0xBBBB444455556666)

        # 1. DBI direct file (source priority 0)
        p_dbi = os.path.join(dbi_dir, "0100000000020000_A_20260918100000.zip")
        make_zip_file(p_dbi, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773830000)})

        # 2. DBI date folder file (source priority 0)
        p_dbi_date = os.path.join(dbi_date_dir, "0100000000020000_A_20260918110000.zip")
        make_zip_file(p_dbi_date, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773831000)})

        # 3. Sphaira dumps level 1 (source priority 1)
        p_dumps_l1 = os.path.join(dumps_root, "0100000000020000_A_20260918120000.zip")
        make_zip_file(p_dumps_l1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773832000)})

        # 4. Sphaira dumps level 2 (source priority 2)
        p_dumps_l2 = os.path.join(dumps_l2, "20260918123000.zip")
        make_zip_file(p_dumps_l2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773832500)})

        # 5. Sphaira dumps level 3 (source priority 3)
        p_dumps_l3 = os.path.join(dumps_l3, "20260918124500.zip")
        make_zip_file(p_dumps_l3, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773832700)})

        # 6. Custom search path level 1 (source priority 10)
        p_custom_l1 = os.path.join(custom_root, "save_1.zip")
        make_zip_file(p_custom_l1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773833000)})

        # 7. Custom search path level 2 (source priority 11)
        p_custom_l2 = os.path.join(custom_l2, "save_2.zip")
        make_zip_file(p_custom_l2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773833500)})

        # 8. Custom search path level 3 (source priority 12)
        p_custom_l3 = os.path.join(custom_l3, "save_3.zip")
        make_zip_file(p_custom_l3, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773834000)})

        # 9. Identical basenames in different folders with equal timestamp:
        p_twin_dumps = os.path.join(dumps_l2, "twin_save.zip")  # prio 2
        p_twin_custom = os.path.join(custom_l2, "twin_save.zip")  # prio 11
        make_zip_file(p_twin_dumps, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000)})
        make_zip_file(p_twin_custom, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000)})

        # Scan with overlapping configured search paths (/dumps configured as custom search path too!)
        groups = read_backup_entries_model(tmpdir, custom_search_paths=["/custom1", "/dumps"])
        check(len(groups) == 1, f"Expected 1 group, got {len(groups)}")
        group = groups[0]

        # Total unique files created: 10
        check(group.backup_count == 10, f"Expected 10 distinct files, got {group.backup_count}")
        check(len(group.backup_members) == 10, f"Expected 10 backup_members, got {len(group.backup_members)}")

        # Check deduplication: no path appears more than once
        paths = [m.path for m in group.backup_members]
        check(len(paths) == len(set(paths)), "Overlapping configured roots must not duplicate any file path")

        # Tie-break verification 1: Equal timestamp (twin_save.zip), lower source priority (2 vs 11) comes first:
        twins = [m for m in group.backup_members if os.path.basename(m.path) == "twin_save.zip"]
        check(len(twins) == 2, "Both twin saves must be present")
        check(twins[0].source == 2 and twins[0].path == "/dumps/GameAlpha/twin_save.zip",
              "Source prio 2 must precede source prio 11 on equal timestamp")
        check(twins[1].source == 11 and twins[1].path == "/custom1/dir1/twin_save.zip",
              "Source prio 11 must follow source prio 2")

        # Tie-break verification 2: Equal timestamp and equal source priority: lexicographically smaller path wins:
        p_lex_a = os.path.join(dumps_l2, "a_tie.zip")
        p_lex_b = os.path.join(dumps_l2, "b_tie.zip")
        make_zip_file(p_lex_a, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773836000)})
        make_zip_file(p_lex_b, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=1773836000)})

        groups_tie = read_backup_entries_model(tmpdir, custom_search_paths=["/custom1"])
        gt = groups_tie[0]
        ties = [m for m in gt.backup_members if os.path.basename(m.path) in ("a_tie.zip", "b_tie.zip")]
        check(len(ties) == 2, "Both tie-break candidates must be present")
        check(ties[0].path < ties[1].path, "Lexicographically smaller path must come first on tie")

        # Representative attributes match backup_members[0]:
        check(gt.backup_path == gt.backup_members[0].path, "Representative path must match backup_members[0].path")
        check(gt.backup_timestamp == gt.backup_members[0].ts, "Representative timestamp must match backup_members[0].ts")

    print("  -> Suite 2 (Location Matrix, Priorities, & Tie-breaking) PASSED.")


def test_suite_3_wire_and_rejection_fixtures() -> None:
    print("[4] Running Suite 3: Wire Fields, POSIX Conversion, & Rejection Fixtures...")
    with tempfile.TemporaryDirectory() as tmpdir:
        # 1. Genuine POSIX seconds converted via posix_to_timestamp
        posix_sec = 1773835200
        converted_cal = posix_to_timestamp(posix_sec)
        check(converted_cal > 20000000000000, f"Converted calendar timestamp must be valid 14-digit int, got {converted_cal}")

        # 2. Filename timestamp overrides metadata timestamp
        p_named = os.path.join(tmpdir, "20260918_180000.zip")
        make_zip_file(p_named, {NX_SAVE_META_NAME: pack_jksv85(ts=posix_sec)})
        info_named = inspect_backup_archive_model(p_named, "20260918_180000.zip")
        check(info_named is not None, "Named archive must decode")
        check(info_named.timestamp == 20260918180000,
              f"Valid filename timestamp must override metadata fallback! Got {info_named.timestamp}")

        # 3. Hand-renamed / zero-timestamp filename falls back to metadata posix_to_timestamp
        p_hand = os.path.join(tmpdir, "hand_renamed_no_date.zip")
        make_zip_file(p_hand, {NX_SAVE_META_NAME: pack_jksv85(ts=posix_sec)})
        info_hand = inspect_backup_archive_model(p_hand, "hand_renamed_no_date.zip")
        check(info_hand is not None, "Hand-renamed archive must decode")
        check(info_hand.timestamp == converted_cal,
              f"Hand-renamed archive must fall back to posix_to_timestamp! Got {info_hand.timestamp}")

        # 4. Real ZIP Rejection Fixtures (Must return None and NEVER enter scan groups):
        # 4.1 Duplicate .nx_save_meta.bin entries in the same ZIP
        buf_dup = io.BytesIO()
        with zipfile.ZipFile(buf_dup, "w") as zf:
            zf.writestr(".nx_save_meta.bin", pack_jksv85(ts=posix_sec))
            zf.writestr(".nx_save_meta.bin", pack_jksv85(ts=posix_sec))
        p_dup = os.path.join(tmpdir, "dumps", "reject_dup.zip")
        os.makedirs(os.path.dirname(p_dup), exist_ok=True)
        with open(p_dup, "wb") as f:
            f.write(buf_dup.getvalue())
        check(inspect_backup_archive_model(p_dup, "reject_dup.zip") is None,
              "Duplicate metadata in ZIP must fail closed (None)")

        # 4.2 Parent directory conflict / nested reserved root (.nx_save_meta.bin/sub)
        buf_nested = io.BytesIO()
        with zipfile.ZipFile(buf_nested, "w") as zf:
            zf.writestr(".nx_save_meta.bin/subfile", b"garbage")
        p_nested = os.path.join(tmpdir, "dumps", "reject_nested.zip")
        with open(p_nested, "wb") as f:
            f.write(buf_nested.getvalue())
        check(inspect_backup_archive_model(p_nested, "reject_nested.zip") is None,
              "Parent directory conflict with reserved root must fail closed (None)")

        # 4.3 Conflicting NX metadata and DBI extra metadata (different app IDs)
        meta_nx = pack_jksv85(app_id=0x0100000000010000, ts=posix_sec)
        meta_dbi = pack_dbi_raw512(app_id=0x0100000000020000, ts=posix_sec)
        p_conflict = os.path.join(tmpdir, "dumps", "reject_conflict.zip")
        make_zip_file(p_conflict, {NX_SAVE_META_NAME: meta_nx, DBI_SAVE_EXTRA_NAME: meta_dbi})
        check(inspect_backup_archive_model(p_conflict, "reject_conflict.zip") is None,
              "Conflicting NX and DBI metadata must fail closed (None)")

        # 4.4 Ambiguous 86-byte layout (divergent valid interpretations)
        raw_divergent = pack_jksv_tail86(owner_id=0x0100000000010001, source_space=0, ts=posix_sec)
        p_ambig = os.path.join(tmpdir, "dumps", "reject_ambig86.zip")
        make_zip_file(p_ambig, {NX_SAVE_META_NAME: raw_divergent})
        check(inspect_backup_archive_model(p_ambig, "reject_ambig86.zip") is None,
              "Ambiguous 86-byte layout must fail closed (None)")

        # 4.5 Malformed/invalid source fields:
        # Invalid save_data_type > 6
        p_bad_type = os.path.join(tmpdir, "dumps", "reject_bad_type.zip")
        make_zip_file(p_bad_type, {NX_SAVE_META_NAME: pack_jksv85(save_type=7, ts=posix_sec)})
        check(inspect_backup_archive_model(p_bad_type, "reject_bad_type.zip") is None,
              "Save data type 7 must fail closed (None)")

        # Negative data_size
        p_neg_size = os.path.join(tmpdir, "dumps", "reject_neg_size.zip")
        make_zip_file(p_neg_size, {NX_SAVE_META_NAME: pack_jksv85(data_size=-1, ts=posix_sec)})
        check(inspect_backup_archive_model(p_neg_size, "reject_neg_size.zip") is None,
              "Negative data size must fail closed (None)")

        # Account save with zero UID
        p_zero_uid = os.path.join(tmpdir, "dumps", "reject_zero_uid.zip")
        make_zip_file(p_zero_uid, {NX_SAVE_META_NAME: pack_jksv85(save_type=1, uid_low=0, uid_high=0, ts=posix_sec)})
        check(inspect_backup_archive_model(p_zero_uid, "reject_zero_uid.zip") is None,
              "Account save with zero UID must fail closed (None)")

        # System save with zero system_save_data_id
        p_zero_sys = os.path.join(tmpdir, "dumps", "reject_zero_sys.zip")
        make_zip_file(p_zero_sys, {NX_SAVE_META_NAME: pack_jksv85(save_type=0, sys_id=0, ts=posix_sec)})
        check(inspect_backup_archive_model(p_zero_sys, "reject_zero_sys.zip") is None,
              "System save with zero system ID must fail closed (None)")

        # Save data rank > 1 (invalid rank)
        p_bad_rank = os.path.join(tmpdir, "dumps", "reject_bad_rank.zip")
        make_zip_file(p_bad_rank, {NX_SAVE_META_NAME: pack_jksv85(rank=2, ts=posix_sec)})
        check(inspect_backup_archive_model(p_bad_rank, "reject_bad_rank.zip") is None,
              "Save data rank 2 must fail closed (None)")

        # Invalid source_space 255
        p_bad_space = os.path.join(tmpdir, "dumps", "reject_bad_space.zip")
        make_zip_file(p_bad_space, {NX_SAVE_META_NAME: pack_jksv_tail86(owner_id=0x01000000000100FF, source_space=255, ts=posix_sec)})
        check(inspect_backup_archive_model(p_bad_space, "reject_bad_space.zip") is None,
              "Invalid source space 255 must fail closed (None)")

        # Truncated metadata (< 85 bytes)
        p_trunc = os.path.join(tmpdir, "dumps", "reject_trunc.zip")
        make_zip_file(p_trunc, {NX_SAVE_META_NAME: b"\x00" * 50})
        check(inspect_backup_archive_model(p_trunc, "reject_trunc.zip") is None,
              "Truncated metadata must fail closed (None)")

        # Verify that running directory scanner over /dumps rejects all invalid archives:
        groups = read_backup_entries_model(tmpdir)
        check(len(groups) == 0, f"No invalid archives must be admitted by the scanner! Got {len(groups)}")

    print("  -> Suite 3 (Wire Fields, POSIX Conversion, & Rejection Fixtures) PASSED.")


def test_suite_4_real_archive_identity_and_grouping_matrix() -> None:
    print("[5] Running Suite 4: Real Archive Identity & Grouping Matrix...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        os.makedirs(dumps_dir, exist_ok=True)

        app1 = 0x0100000000030000
        uid1 = (0x1111111111111111, 0x2222222222222222)
        posix_ts = 1773835200

        # 1. Rank 0 vs Rank 1: admitted into SEPARATE groups (v0.13.862 provenance)
        z_rank0 = os.path.join(dumps_dir, "rank0.zip")
        z_rank1 = os.path.join(dumps_dir, "rank1.zip")
        make_zip_file(z_rank0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app1, uid_low=uid1[0], uid_high=uid1[1], rank=0, ts=posix_ts)})
        make_zip_file(z_rank1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app1, uid_low=uid1[0], uid_high=uid1[1], rank=1, ts=posix_ts + 10)})

        # 2. JKSV86 rank 0 and rank 1 distinct groups with source spaces User/SdUser
        app_j86 = 0x0100000000031000
        z_j86_r0 = os.path.join(dumps_dir, "j86_rank0.zip")
        z_j86_r1 = os.path.join(dumps_dir, "j86_rank1.zip")
        make_zip_file(z_j86_r0, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_j86, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, rank=0, source_space=1, ts=posix_ts)})
        make_zip_file(z_j86_r1, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_j86, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, rank=1, source_space=2, ts=posix_ts + 10)})

        # 3. Sphaira legacy128 rank 0 and rank 1 distinct groups
        app_sph128 = 0x0100000000032000
        z_sph_r0 = os.path.join(dumps_dir, "sph_rank0.zip")
        z_sph_r1 = os.path.join(dumps_dir, "sph_rank1.zip")
        make_zip_file(z_sph_r0, {NX_SAVE_META_NAME: pack_sphaira128(app_id=app_sph128, uid_low=uid1[0], uid_high=uid1[1], rank=0, ts=posix_ts)})
        make_zip_file(z_sph_r1, {NX_SAVE_META_NAME: pack_sphaira128(app_id=app_sph128, uid_low=uid1[0], uid_high=uid1[1], rank=1, ts=posix_ts + 10)})

        # 4. DBI extra 512 rank 0 and rank 1 distinct groups
        app_dbi512 = 0x0100000000033000
        z_dbi_r0 = os.path.join(dumps_dir, "dbi512_rank0.zip")
        z_dbi_r1 = os.path.join(dumps_dir, "dbi512_rank1.zip")
        make_zip_file(z_dbi_r0, {DBI_SAVE_EXTRA_NAME: pack_dbi_raw512(app_id=app_dbi512, uid_low=uid1[0], uid_high=uid1[1], rank=0, ts=posix_ts)})
        make_zip_file(z_dbi_r1, {DBI_SAVE_EXTRA_NAME: pack_dbi_raw512(app_id=app_dbi512, uid_low=uid1[0], uid_high=uid1[1], rank=1, ts=posix_ts + 10)})

        # 5. 3-way test on same app/UID/index: known Primary (rk:0), known Secondary (rk:1), and unknown metadata-free (rk:?)
        app_3way = 0x0100000000034000
        z_3w_r0 = os.path.join(dumps_dir, "0100000000034000_D_20260918120000.zip")
        z_3w_r1 = os.path.join(dumps_dir, "0100000000034000_D_20260918120001.zip")
        z_3w_un = os.path.join(dumps_dir, "0100000000034000_D_20260918120002.zip")
        make_zip_file(z_3w_r0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_3way, save_type=SAVE_TYPE_DEVICE, uid_low=0, uid_high=0, rank=0, ts=posix_ts)})
        make_zip_file(z_3w_r1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_3way, save_type=SAVE_TYPE_DEVICE, uid_low=0, uid_high=0, rank=1, ts=posix_ts + 1)})
        make_zip_file(z_3w_un, {"save.dat": b"3way_un_bytes"})

        # 6. Index 0 vs Index 1: admitted into SEPARATE groups
        app2 = 0x0100000000040000
        z_idx0 = os.path.join(dumps_dir, "idx0.zip")
        z_idx1 = os.path.join(dumps_dir, "idx1.zip")
        make_zip_file(z_idx0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app2, uid_low=uid1[0], uid_high=uid1[1], index=0, ts=posix_ts)})
        make_zip_file(z_idx1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app2, uid_low=uid1[0], uid_high=uid1[1], index=1, ts=posix_ts)})

        # 7. Cache type (type 5) with Index 0 vs Index 1: admitted into SEPARATE groups
        app_cache = 0x0100000000050000
        z_c0 = os.path.join(dumps_dir, "cache0.zip")
        z_c1 = os.path.join(dumps_dir, "cache1.zip")
        make_zip_file(z_c0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_cache, uid_low=uid1[0], uid_high=uid1[1], save_type=SAVE_TYPE_CACHE, index=0, ts=posix_ts)})
        make_zip_file(z_c1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_cache, uid_low=uid1[0], uid_high=uid1[1], save_type=SAVE_TYPE_CACHE, index=1, ts=posix_ts)})

        # 8. Cache in User vs SdUser (source_space 1 vs 2) merging into 1 group when rank matches
        app_cache_sp = 0x0100000000051000
        z_csp1 = os.path.join(dumps_dir, "cache_sp1.zip")
        z_csp2 = os.path.join(dumps_dir, "cache_sp2.zip")
        make_zip_file(z_csp1, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_cache_sp, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, save_type=SAVE_TYPE_CACHE, source_space=1, rank=0, ts=posix_ts)})
        make_zip_file(z_csp2, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_cache_sp, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, save_type=SAVE_TYPE_CACHE, source_space=2, rank=0, ts=posix_ts + 1)})

        # 9. Save data type: Account (1) vs Device (3): admitted into SEPARATE groups
        app_dev = 0x0100000000060000
        z_acc = os.path.join(dumps_dir, "acc.zip")
        z_dev = os.path.join(dumps_dir, "dev.zip")
        make_zip_file(z_acc, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_dev, uid_low=uid1[0], uid_high=uid1[1], save_type=SAVE_TYPE_ACCOUNT, ts=posix_ts)})
        make_zip_file(z_dev, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_dev, uid_low=0, uid_high=0, save_type=SAVE_TYPE_DEVICE, ts=posix_ts)})

        # 10. System saves differing in system_save_data_id: admitted into SEPARATE groups
        z_sys1 = os.path.join(dumps_dir, "sys1.zip")
        z_sys2 = os.path.join(dumps_dir, "sys2.zip")
        make_zip_file(z_sys1, {NX_SAVE_META_NAME: pack_jksv85(app_id=0, sys_id=0x8000000000000010, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, ts=posix_ts)})
        make_zip_file(z_sys2, {NX_SAVE_META_NAME: pack_jksv85(app_id=0, sys_id=0x8000000000000020, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, ts=posix_ts)})

        # 11. System saves across alternate valid system spaces -> merge into 1 group (count 2)
        sys_id_sp = 0x8000000000000030
        z_sys_sp1 = os.path.join(dumps_dir, "sys_sp1.zip")
        z_sys_sp2 = os.path.join(dumps_dir, "sys_sp2.zip")
        make_zip_file(z_sys_sp1, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=0, sys_id=sys_id_sp, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, owner_id=0x0100000000010055, source_space=0, rank=0, ts=posix_ts)})
        make_zip_file(z_sys_sp2, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=0, sys_id=sys_id_sp, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, owner_id=0x0100000000010055, source_space=100, rank=0, ts=posix_ts + 1)})

        # 12. UID-high-only difference: admitted into SEPARATE groups
        app_uid = 0x0100000000070000
        z_uh1 = os.path.join(dumps_dir, "uid_high1.zip")
        z_uh2 = os.path.join(dumps_dir, "uid_high2.zip")
        make_zip_file(z_uh1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_uid, uid_low=0x5555, uid_high=0x6666, ts=posix_ts)})
        make_zip_file(z_uh2, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_uid, uid_low=0x5555, uid_high=0x7777, ts=posix_ts)})

        # 13. Concrete source-space facts (source_space 1 vs 2): admitted into SAME group
        app_sp = 0x0100000000080000
        z_sp1 = os.path.join(dumps_dir, "sp1.zip")
        z_sp2 = os.path.join(dumps_dir, "sp2.zip")
        make_zip_file(z_sp1, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_sp, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, source_space=1, ts=posix_ts)})
        make_zip_file(z_sp2, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_sp, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, source_space=2, ts=posix_ts + 5)})

        # 14. System (0) vs SystemBcat (6): admitted into SEPARATE groups
        z_s0 = os.path.join(dumps_dir, "system_0.zip")
        z_s6 = os.path.join(dumps_dir, "system_bcat.zip")
        make_zip_file(z_s0, {NX_SAVE_META_NAME: pack_jksv85(app_id=0, sys_id=0x8000000000000099, save_type=SAVE_TYPE_SYSTEM, uid_low=0, uid_high=0, ts=posix_ts)})
        make_zip_file(z_s6, {NX_SAVE_META_NAME: pack_jksv85(app_id=0, sys_id=0x8000000000000099, save_type=SAVE_TYPE_SYSTEM_BCAT, uid_low=0, uid_high=0, ts=posix_ts)})

        # 15. Mixed wire versions (85, 86, 128, 512) for same save: admitted into SAME group
        app_mix = 0x01000000000A0000
        z_v85 = os.path.join(dumps_dir, "v85.zip")
        z_v86 = os.path.join(dumps_dir, "v86.zip")
        z_v128 = os.path.join(dumps_dir, "v128.zip")
        z_v512 = os.path.join(dumps_dir, "v512.zip")
        make_zip_file(z_v85, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_mix, uid_low=uid1[0], uid_high=uid1[1], ts=posix_ts)})
        make_zip_file(z_v86, {NX_SAVE_META_NAME: pack_jksv_tail86(app_id=app_mix, uid_low=uid1[0], uid_high=uid1[1], owner_id=0x0100000000010055, ts=posix_ts + 1)})
        make_zip_file(z_v128, {NX_SAVE_META_NAME: pack_sphaira128(app_id=app_mix, uid_low=uid1[0], uid_high=uid1[1], ts=posix_ts + 2)})
        make_zip_file(z_v512, {DBI_SAVE_EXTRA_NAME: pack_dbi_raw512(app_id=app_mix, uid_low=uid1[0], uid_high=uid1[1], ts=posix_ts + 3)})

        # 16. Two compatible metadata-free DBI legacy archives: merge into 1 group with unknown rank
        app_dbi = 0x01000000000B0000
        z_dbi_legacy1 = os.path.join(dumps_dir, "01000000000B0000_A_20260918140000.zip")
        z_dbi_legacy2 = os.path.join(dumps_dir, "01000000000B0000_A_20260918140001.zip")
        make_zip_file(z_dbi_legacy1, {"save.dat": b"dbi_legacy_bytes1"})
        make_zip_file(z_dbi_legacy2, {"save.dat": b"dbi_legacy_bytes2"})

        # Run actual filesystem scanner
        groups = read_backup_entries_model(tmpdir)

        # Map groups by group key
        by_key = {
            backup_group_key(g.application_id, g.system_save_data_id, g.save_data_type, g.uid_low, g.uid_high, g.save_data_index, g.backup_rank_known, g.save_data_rank): g
            for g in groups
        }

        # Assertions on grouping:
        # Rank 0 & 1 -> 2 SEPARATE groups (v0.13.862 provenance)
        k_rank0 = backup_group_key(app1, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_rank1 = backup_group_key(app1, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=1)
        check(k_rank0 in by_key and by_key[k_rank0].backup_count == 1, "Rank 0 must form distinct group (count 1)")
        check(k_rank1 in by_key and by_key[k_rank1].backup_count == 1, "Rank 1 must form distinct group (count 1)")
        check(k_rank0 != k_rank1, "Rank 0 and Rank 1 group keys must differ")

        # JKSV86 rank 0 and 1 -> 2 distinct groups
        k_j86_r0 = backup_group_key(app_j86, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_j86_r1 = backup_group_key(app_j86, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=1)
        check(k_j86_r0 in by_key and k_j86_r1 in by_key and k_j86_r0 != k_j86_r1, "JKSV86 rank 0 and 1 must form distinct groups")

        # Sphaira legacy128 rank 0 and 1 -> 2 distinct groups
        k_sph_r0 = backup_group_key(app_sph128, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_sph_r1 = backup_group_key(app_sph128, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=1)
        check(k_sph_r0 in by_key and k_sph_r1 in by_key and k_sph_r0 != k_sph_r1, "Sphaira legacy128 rank 0 and 1 must form distinct groups")

        # DBI extra 512 rank 0 and 1 -> 2 distinct groups
        k_dbi_r0 = backup_group_key(app_dbi512, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_dbi_r1 = backup_group_key(app_dbi512, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=1)
        check(k_dbi_r0 in by_key and k_dbi_r1 in by_key and k_dbi_r0 != k_dbi_r1, "DBI 512 rank 0 and 1 must form distinct groups")

        # 3-way test on same app/UID/index: Primary, Secondary, Unknown -> 3 distinct groups
        k_3w_r0 = backup_group_key(app_3way, 0, SAVE_TYPE_DEVICE, 0, 0, 0, rank_known=True, rank=0)
        k_3w_r1 = backup_group_key(app_3way, 0, SAVE_TYPE_DEVICE, 0, 0, 0, rank_known=True, rank=1)
        k_3w_un = backup_group_key(app_3way, 0, SAVE_TYPE_DEVICE, 0, 0, 0, rank_known=False, rank=0)
        check(k_3w_r0 in by_key and k_3w_r1 in by_key and k_3w_un in by_key, "3-way archives must all be admitted")
        check(len({k_3w_r0, k_3w_r1, k_3w_un}) == 3, "3-way archives must form 3 completely distinct groups")

        # Index 0 & 1 -> 2 distinct groups
        k_idx0 = backup_group_key(app2, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_idx1 = backup_group_key(app2, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 1, rank_known=True, rank=0)
        check(k_idx0 in by_key and k_idx1 in by_key and k_idx0 != k_idx1, "Index 0 and Index 1 must form distinct groups")

        # Cache Index 0 & 1 -> 2 distinct groups
        k_c0 = backup_group_key(app_cache, 0, SAVE_TYPE_CACHE, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_c1 = backup_group_key(app_cache, 0, SAVE_TYPE_CACHE, uid1[0], uid1[1], 1, rank_known=True, rank=0)
        check(k_c0 in by_key and k_c1 in by_key and k_c0 != k_c1, "Cache index 0 and 1 must form distinct groups")

        # Cache in User vs SdUser (source space 1 vs 2) -> merge into 1 group (count 2)
        k_csp = backup_group_key(app_cache_sp, 0, SAVE_TYPE_CACHE, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        check(k_csp in by_key and by_key[k_csp].backup_count == 2, "Cache User/SdUser archives must merge into 1 group")

        # Account vs Device -> 2 distinct groups
        k_acc = backup_group_key(app_dev, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        k_dev = backup_group_key(app_dev, 0, SAVE_TYPE_DEVICE, 0, 0, 0, rank_known=True, rank=0)
        check(k_acc in by_key and k_dev in by_key and k_acc != k_dev, "Account and Device must form distinct groups")

        # System saves with different system IDs -> 2 distinct groups
        k_sys1 = backup_group_key(0, 0x8000000000000010, SAVE_TYPE_SYSTEM, 0, 0, 0, rank_known=True, rank=0)
        k_sys2 = backup_group_key(0, 0x8000000000000020, SAVE_TYPE_SYSTEM, 0, 0, 0, rank_known=True, rank=0)
        check(k_sys1 in by_key and k_sys2 in by_key and k_sys1 != k_sys2, "Different system IDs must form distinct groups")

        # System saves across alternate valid system spaces -> merge into 1 group (count 2)
        k_sys_sp = backup_group_key(0, sys_id_sp, SAVE_TYPE_SYSTEM, 0, 0, 0, rank_known=True, rank=0)
        check(k_sys_sp in by_key and by_key[k_sys_sp].backup_count == 2, "System saves across spaces must merge into 1 group")

        # UID-high distinction -> 2 distinct groups
        k_uh1 = backup_group_key(app_uid, 0, SAVE_TYPE_ACCOUNT, 0x5555, 0x6666, 0, rank_known=True, rank=0)
        k_uh2 = backup_group_key(app_uid, 0, SAVE_TYPE_ACCOUNT, 0x5555, 0x7777, 0, rank_known=True, rank=0)
        check(k_uh1 in by_key and k_uh2 in by_key and k_uh1 != k_uh2, "Differing UID-high must form distinct groups")

        # Source-space facts -> single group with backup_count == 2
        k_sp = backup_group_key(app_sp, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        check(k_sp in by_key and by_key[k_sp].backup_count == 2, "Different source spaces must merge into 1 group")

        # System (0) vs SystemBcat (6) -> 2 distinct groups
        k_s0 = backup_group_key(0, 0x8000000000000099, SAVE_TYPE_SYSTEM, 0, 0, 0, rank_known=True, rank=0)
        k_s6 = backup_group_key(0, 0x8000000000000099, SAVE_TYPE_SYSTEM_BCAT, 0, 0, 0, rank_known=True, rank=0)
        check(k_s0 in by_key and k_s6 in by_key and k_s0 != k_s6, "System and SystemBcat must form distinct groups")

        # Mixed wire versions -> single group with backup_count == 4
        k_mix = backup_group_key(app_mix, 0, SAVE_TYPE_ACCOUNT, uid1[0], uid1[1], 0, rank_known=True, rank=0)
        check(k_mix in by_key and by_key[k_mix].backup_count == 4, "All 4 wire formats must merge into 1 group (count 4)")

        # 2 compatible metadata-free DBI legacy archives -> merge into 1 group with unknown rank
        k_dbi = backup_group_key(app_dbi, 0, SAVE_TYPE_ACCOUNT, 0, 0, 0, rank_known=False, rank=0)
        check(k_dbi in by_key and by_key[k_dbi].backup_count == 2, "Compatible metadata-free DBI archives must merge into 1 group (count 2)")

        # Independent exact artifact membership mapping for all scan-produced groups:
        expected_memberships: Dict[str, Set[str]] = {
            k_rank0: {"/dumps/rank0.zip"},
            k_rank1: {"/dumps/rank1.zip"},
            k_j86_r0: {"/dumps/j86_rank0.zip"},
            k_j86_r1: {"/dumps/j86_rank1.zip"},
            k_sph_r0: {"/dumps/sph_rank0.zip"},
            k_sph_r1: {"/dumps/sph_rank1.zip"},
            k_dbi_r0: {"/dumps/dbi512_rank0.zip"},
            k_dbi_r1: {"/dumps/dbi512_rank1.zip"},
            k_3w_r0: {"/dumps/0100000000034000_D_20260918120000.zip"},
            k_3w_r1: {"/dumps/0100000000034000_D_20260918120001.zip"},
            k_3w_un: {"/dumps/0100000000034000_D_20260918120002.zip"},
            k_idx0: {"/dumps/idx0.zip"},
            k_idx1: {"/dumps/idx1.zip"},
            k_c0: {"/dumps/cache0.zip"},
            k_c1: {"/dumps/cache1.zip"},
            k_csp: {"/dumps/cache_sp1.zip", "/dumps/cache_sp2.zip"},
            k_acc: {"/dumps/acc.zip"},
            k_dev: {"/dumps/dev.zip"},
            k_sys1: {"/dumps/sys1.zip"},
            k_sys2: {"/dumps/sys2.zip"},
            k_sys_sp: {"/dumps/sys_sp1.zip", "/dumps/sys_sp2.zip"},
            k_uh1: {"/dumps/uid_high1.zip"},
            k_uh2: {"/dumps/uid_high2.zip"},
            k_sp: {"/dumps/sp1.zip", "/dumps/sp2.zip"},
            k_s0: {"/dumps/system_0.zip"},
            k_s6: {"/dumps/system_bcat.zip"},
            k_mix: {"/dumps/v85.zip", "/dumps/v86.zip", "/dumps/v128.zip", "/dumps/v512.zip"},
            k_dbi: {"/dumps/01000000000B0000_A_20260918140000.zip", "/dumps/01000000000B0000_A_20260918140001.zip"},
        }

        # Check that scanner produced exactly these groups with no extras or omissions:
        check(len(groups) == len(expected_memberships),
              f"Expected exactly {len(expected_memberships)} groups from scanner, got {len(groups)}")
        check(set(by_key.keys()) == set(expected_memberships.keys()),
              "Scanner group keys must exactly match expected group keys")

        # For every scan-produced group, call collect_group_archives_model and assert exact path membership:
        for key, expected_paths in expected_memberships.items():
            g = by_key[key]
            collected = collect_group_archives_model(g, tmpdir)
            collected_paths = {c.path for c in collected}
            check(collected_paths == expected_paths,
                  f"Group '{key}' collected paths {collected_paths} did not match expected {expected_paths}")
            check(len(collected) == g.backup_count,
                  f"Group '{key}' candidate count {len(collected)} != group.backup_count {g.backup_count}")
            for c in collected:
                check(c.ts > 0, f"Candidate {c.path} in group {key} must have valid positive timestamp, got {c.ts}")

        # Live entry matching: live FsSaveDataInfo entry matches only matching rank archive group
        live_dir = os.path.join(dumps_dir, "LiveApp")
        os.makedirs(live_dir, exist_ok=True)
        app_live = 0x01000000000EE000
        z_live_r0 = os.path.join(live_dir, "live_rank0.zip")
        z_live_r1 = os.path.join(live_dir, "live_rank1.zip")
        make_zip_file(z_live_r0, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_live, uid_low=uid1[0], uid_high=uid1[1], rank=0, ts=posix_ts)})
        make_zip_file(z_live_r1, {NX_SAVE_META_NAME: pack_jksv85(app_id=app_live, uid_low=uid1[0], uid_high=uid1[1], rank=1, ts=posix_ts + 1)})

        live_e_r0 = EntryModel(application_id=app_live, uid_low=uid1[0], uid_high=uid1[1], save_data_type=SAVE_TYPE_ACCOUNT, save_data_rank=0, is_backup=False, name="LiveApp")
        live_e_r1 = EntryModel(application_id=app_live, uid_low=uid1[0], uid_high=uid1[1], save_data_type=SAVE_TYPE_ACCOUNT, save_data_rank=1, is_backup=False, name="LiveApp")

        cands_live_r0 = collect_group_archives_model(live_e_r0, tmpdir)
        check(len(cands_live_r0) == 1 and cands_live_r0[0].path == "/dumps/LiveApp/live_rank0.zip",
              f"Live entry rank 0 must collect only live_rank0.zip, got {[c.path for c in cands_live_r0]}")

        cands_live_r1 = collect_group_archives_model(live_e_r1, tmpdir)
        check(len(cands_live_r1) == 1 and cands_live_r1[0].path == "/dumps/LiveApp/live_rank1.zip",
              f"Live entry rank 1 must collect only live_rank1.zip, got {[c.path for c in cands_live_r1]}")

    print("  -> Suite 4 (Real Archive Identity & Grouping Matrix) PASSED.")
