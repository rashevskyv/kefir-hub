# Test suites 7 and 8 for save backup library contract.
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

def test_suite_7_source_anchors_and_model_lifetimes() -> None:
    print("[8] Running Suite 7: Source Anchors & Model-Level Lifetimes...")
    # Model-Level Container and Lifetime Invariants (clearly labeled as pure-Python model behavior,
    # not claiming C++ compiler/vector move proof):
    c1 = BackupCandidateModel(ts=100, path="/dumps/1.zip", source=0)
    c2 = BackupCandidateModel(ts=200, path="/dumps/2.zip", source=1)

    # 1. Container insertion
    entries: List[EntryModel] = []
    e = EntryModel(application_id=0x1234, backup_members=[c1, c2])
    entries.append(e)

    # 2. Model copy semantics
    copied = list(entries)
    check(len(copied[0].backup_members) == 2, "Copied Entry must preserve backup_members")

    # 3. Model move / source container destruction
    moved = EntryModel(application_id=entries[0].application_id, backup_members=list(entries[0].backup_members))
    entries.clear()
    check(len(moved.backup_members) == 2, "Moved Entry must survive destruction of source container")

    # 4. Lambda capture of EntryModel
    captured = moved
    cb = lambda: [c.path for c in captured.backup_members]
    check(cb() == ["/dumps/1.zip", "/dumps/2.zip"], "Lambda capture of Entry must retain backup_members independently")

    print("  -> Suite 7 (Source Anchors & Model-Level Lifetimes) PASSED.")


def test_suite_8_connected_create_backup_if_newer_conservative_freshness() -> None:
    print("[9] Running Suite 8: Connected CreateBackupIfNewer Conservative Freshness Contract...")
    with tempfile.TemporaryDirectory() as tmpdir:
        dumps_dir = os.path.join(tmpdir, "dumps")
        os.makedirs(dumps_dir, exist_ok=True)

        app_base = 0x01000000000A0000
        uid = (0x1234567812345678, 0x8765432187654321)

        def make_seed(name: str, app_id: int) -> EntryModel:
            return EntryModel(
                application_id=app_id,
                save_data_type=SAVE_TYPE_ACCOUNT,
                uid_low=uid[0],
                uid_high=uid[1],
                name=name,
                is_backup=False,
                save_data_space_id=1,
                save_data_id=0x1000 + (app_id & 0xFFFF),
            )

        # 1. No archives -> backup
        seed_no_arch = make_seed("NoArchGame", app_base + 1)
        tb, up, ok = create_backup_if_newer_model([seed_no_arch], tmpdir,
            live_extra_provider=lambda e: FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42))
        check(ok and len(tb) == 1 and tb[0].application_id == seed_no_arch.application_id and up == 0,
              "Case 1: No archives must result in backup creation")

        # 2. Newest inspection failure -> backup
        seed_insp_fail = make_seed("InspFailGame", app_base + 2)
        seed_insp_fail.backup_path = "/dumps/InspFailGame/corrupt.zip"
        dir_insp = os.path.join(dumps_dir, "InspFailGame")
        os.makedirs(dir_insp, exist_ok=True)
        with open(os.path.join(dir_insp, "corrupt.zip"), "wb") as f:
            f.write(b"CORRUPT_ARCHIVE_DATA_NOT_A_ZIP")
        tb, up, ok = create_backup_if_newer_model([seed_insp_fail], tmpdir,
            live_extra_provider=lambda e: FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42))
        check(ok and len(tb) == 1 and up == 0,
              "Case 2: Corrupt/unreadable newest archive must result in backup creation")

        # 3. Live extra-data read failure -> backup
        seed_live_fail = make_seed("LiveFailGame", app_base + 3)
        dir_live_fail = os.path.join(dumps_dir, "LiveFailGame")
        os.makedirs(dir_live_fail, exist_ok=True)
        make_zip_file(os.path.join(dir_live_fail, "20260918_120000.zip"), {
            NX_SAVE_META_NAME: pack_jksv85(app_id=seed_live_fail.application_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000, commit_id=42),
            "data.bin": b"payload"
        })
        tb, up, ok = create_backup_if_newer_model([seed_live_fail], tmpdir,
            live_extra_provider=lambda e: None)  # R_FAILED simulation
        check(ok and len(tb) == 1 and up == 0,
              "Case 3: Live extra-data read failure must result in backup creation")

        # 4. Both timestamp and commit ID nonzero and equal -> skip (up to date)
        seed_up_to_date = make_seed("UpToDateGame", app_base + 4)
        dir_up_to_date = os.path.join(dumps_dir, "UpToDateGame")
        os.makedirs(dir_up_to_date, exist_ok=True)
        make_zip_file(os.path.join(dir_up_to_date, "20260918_120000.zip"), {
            NX_SAVE_META_NAME: pack_jksv85(app_id=seed_up_to_date.application_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000, commit_id=42),
            "data.bin": b"payload"
        })
        tb, up, ok = create_backup_if_newer_model([seed_up_to_date], tmpdir,
            live_extra_provider=lambda e: FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42))
        check(ok and len(tb) == 0 and up == 1,
              "Case 4: Exact nonzero timestamp and commit ID match must count as up to date (skip backup)")

        # Cases 5-10: Proven regressions - old predicate permitted skipping, new predicate creates backup!
        proven_matrix = [
            (5, "Equal timestamp + live commit ID zero", 1773835000, 42, 1773835000, 0),
            (6, "Equal timestamp + archive commit ID zero", 1773835000, 0, 1773835000, 42),
            (7, "Equal timestamp + both commit IDs zero", 1773835000, 0, 1773835000, 0),
            (8, "Equal commit ID + live timestamp zero", 1773835000, 42, 0, 42),
            (9, "Equal commit ID + archive timestamp zero", 0, 42, 1773835000, 42),
            (10, "Equal commit ID + both timestamps zero", 0, 42, 0, 42),
        ]

        for case_num, desc, a_ts, a_commit, l_ts, l_commit in proven_matrix:
            game_name = f"RegrCase{case_num}"
            app_id = app_base + 10 + case_num
            seed_case = make_seed(game_name, app_id)
            gdir = os.path.join(dumps_dir, game_name)
            os.makedirs(gdir, exist_ok=True)
            make_zip_file(os.path.join(gdir, "20260918_120000.zip"), {
                NX_SAVE_META_NAME: pack_jksv85(app_id=app_id, uid_low=uid[0], uid_high=uid[1], ts=a_ts, commit_id=a_commit),
                "data.bin": b"payload"
            })
            provider = lambda e, l_ts=l_ts, l_commit=l_commit: FsSaveDataExtraDataModel(timestamp=l_ts, commit_id=l_commit)

            # Old predicate defect: skips backup (up_to_date_count == 1)
            tb_old, up_old, _ = create_backup_if_newer_model([seed_case], tmpdir, live_extra_provider=provider, predicate_mode="old")
            check(len(tb_old) == 0 and up_old == 1,
                  f"Case {case_num} proven defect: old predicate incorrectly classified '{desc}' as up to date")

            # New predicate fix: conservative backup (to_backup == [seed_case], up_to_date_count == 0)
            tb_new, up_new, _ = create_backup_if_newer_model([seed_case], tmpdir, live_extra_provider=provider, predicate_mode="new")
            check(len(tb_new) == 1 and up_new == 0,
                  f"Case {case_num} fix: new predicate must create backup on '{desc}'")

        # 11. Timestamp mismatch with equal commit ID -> backup
        seed_ts_mismatch = make_seed("TsMismatchGame", app_base + 21)
        dir_ts_mismatch = os.path.join(dumps_dir, "TsMismatchGame")
        os.makedirs(dir_ts_mismatch, exist_ok=True)
        make_zip_file(os.path.join(dir_ts_mismatch, "20260918_120000.zip"), {
            NX_SAVE_META_NAME: pack_jksv85(app_id=seed_ts_mismatch.application_id, uid_low=uid[0], uid_high=uid[1], ts=1773831000, commit_id=42),
            "data.bin": b"payload"
        })
        tb, up, ok = create_backup_if_newer_model([seed_ts_mismatch], tmpdir,
            live_extra_provider=lambda e: FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42))
        check(ok and len(tb) == 1 and up == 0,
              "Case 11: Timestamp mismatch must result in backup creation")

        # 12. Commit mismatch with equal timestamp -> backup
        seed_commit_mismatch = make_seed("CommitMismatchGame", app_base + 22)
        dir_commit_mismatch = os.path.join(dumps_dir, "CommitMismatchGame")
        os.makedirs(dir_commit_mismatch, exist_ok=True)
        make_zip_file(os.path.join(dir_commit_mismatch, "20260918_120000.zip"), {
            NX_SAVE_META_NAME: pack_jksv85(app_id=seed_commit_mismatch.application_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000, commit_id=10),
            "data.bin": b"payload"
        })
        tb, up, ok = create_backup_if_newer_model([seed_commit_mismatch], tmpdir,
            live_extra_provider=lambda e: FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42))
        check(ok and len(tb) == 1 and up == 0,
              "Case 12: Commit mismatch must result in backup creation")

        # 13. Metadata-free archive with zero provenance -> backup
        seed_no_meta = make_seed("NoMetaGame", app_base + 23)
        seed_no_meta.backup_path = f"/dumps/NoMetaGame/{app_base+23:016X}_A_20260918120000_0.zip"
        dir_no_meta = os.path.join(dumps_dir, "NoMetaGame")
        os.makedirs(dir_no_meta, exist_ok=True)
        make_zip_file(os.path.join(dir_no_meta, f"{app_base+23:016X}_A_20260918120000_0.zip"), {
            "save_file.bin": b"unattributed_save_payload"
        })
        tb, up, ok = create_backup_if_newer_model([seed_no_meta], tmpdir,
            live_extra_provider=lambda e: FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42))
        check(ok and len(tb) == 1 and up == 0,
              "Case 13: Metadata-free archive must result in backup creation")

        # 14. Older archive matches but newest differs -> backup
        seed_order_diff = make_seed("OrderDiffGame", app_base + 24)
        dir_order_diff = os.path.join(dumps_dir, "OrderDiffGame")
        os.makedirs(dir_order_diff, exist_ok=True)
        make_zip_file(os.path.join(dir_order_diff, "20260918_100000.zip"), {
            NX_SAVE_META_NAME: pack_jksv85(app_id=seed_order_diff.application_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000, commit_id=42),
            "data.bin": b"older_matching_payload"
        })
        make_zip_file(os.path.join(dir_order_diff, "20260918_150000.zip"), {
            NX_SAVE_META_NAME: pack_jksv85(app_id=seed_order_diff.application_id, uid_low=uid[0], uid_high=uid[1], ts=1773836000, commit_id=99),
            "data.bin": b"newest_differing_payload"
        })
        tb, up, ok = create_backup_if_newer_model([seed_order_diff], tmpdir,
            live_extra_provider=lambda e: FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42))
        check(ok and len(tb) == 1 and up == 0,
              "Case 14: When newest archive differs, older matching archive must not suppress backup creation")

        # 15. Newest matches while older differs -> skip (up to date)
        seed_order_match = make_seed("OrderMatchGame", app_base + 25)
        dir_order_match = os.path.join(dumps_dir, "OrderMatchGame")
        os.makedirs(dir_order_match, exist_ok=True)
        make_zip_file(os.path.join(dir_order_match, "20260918_100000.zip"), {
            NX_SAVE_META_NAME: pack_jksv85(app_id=seed_order_match.application_id, uid_low=uid[0], uid_high=uid[1], ts=1773831000, commit_id=10),
            "data.bin": b"older_differing_payload"
        })
        make_zip_file(os.path.join(dir_order_match, "20260918_150000.zip"), {
            NX_SAVE_META_NAME: pack_jksv85(app_id=seed_order_match.application_id, uid_low=uid[0], uid_high=uid[1], ts=1773835000, commit_id=42),
            "data.bin": b"newest_matching_payload"
        })
        tb, up, ok = create_backup_if_newer_model([seed_order_match], tmpdir,
            live_extra_provider=lambda e: FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42))
        check(ok and len(tb) == 0 and up == 1,
              "Case 15: When newest archive matches, backup must be skipped even if older archive differed")

        # 16. Multiple selected entries preserve independent counts and decisions
        seeds_multi = [seed_up_to_date, seed_commit_mismatch, seed_no_arch]
        multi_provider = lambda e: (
            FsSaveDataExtraDataModel(timestamp=1773835000, commit_id=42)
            if e.application_id in (seed_up_to_date.application_id, seed_commit_mismatch.application_id)
            else FsSaveDataExtraDataModel(timestamp=1000, commit_id=1)
        )
        tb_multi, up_multi, ok_multi = create_backup_if_newer_model(seeds_multi, tmpdir, live_extra_provider=multi_provider)
        check(ok_multi, "Case 16: Multi-entry run must succeed")
        check(len(tb_multi) == 2, f"Case 16: Expected 2 entries to backup, got {len(tb_multi)}")
        check(up_multi == 1, f"Case 16: Expected 1 up-to-date entry, got {up_multi}")
        check(tb_multi[0].application_id == seed_commit_mismatch.application_id, "Case 16: tb[0] must be CommitMismatchGame")
        check(tb_multi[1].application_id == seed_no_arch.application_id, "Case 16: tb[1] must be NoArchGame")

        # 17. Cancellation gate remains before each seed
        tb_cancel_0, up_cancel_0, ok_cancel_0 = create_backup_if_newer_model(
            seeds_multi, tmpdir, live_extra_provider=multi_provider, should_exit_at_index=0)
        check(not ok_cancel_0, "Case 17: Cancellation before seed 0 must halt execution immediately")
        check(len(tb_cancel_0) == 0 and up_cancel_0 == 0, "Case 17: Cancelled before seed 0 must leave counts at zero")

        tb_cancel_1, up_cancel_1, ok_cancel_1 = create_backup_if_newer_model(
            seeds_multi, tmpdir, live_extra_provider=multi_provider, should_exit_at_index=1)
        check(not ok_cancel_1, "Case 17: Cancellation before seed 1 must halt execution early")
        check(len(tb_cancel_1) == 0 and up_cancel_1 == 1,
              "Case 17: Cancellation before seed 1 must not process remaining seeds (seed 1 and seed 2 never processed)")

        # 18. to_backup_count and up_to_date_count remain consistent with actual decisions
        total_decisions = len(tb_multi) + up_multi
        check(total_decisions == len(seeds_multi),
              f"Case 18: Sum of to_backup ({len(tb_multi)}) and up_to_date ({up_multi}) must equal total seeds ({len(seeds_multi)})")

    print("  -> Suite 8 (Connected CreateBackupIfNewer Conservative Freshness Contract) PASSED.")
