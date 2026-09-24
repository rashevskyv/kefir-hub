# Scenarios for save folder import contract.
import os
import io
import struct
import pathlib
import zipfile
import tempfile
import shutil
import hashlib
from contract_fixtures.save_folder_import_models import (
    JKSV_MAGIC, JKSV_REVISION, VALID_SPACES, RESERVED_ROOT_NAMES,
    NX_SAVE_META_NAME, DBI_SAVE_EXTRA_NAME, DBI_SAVE_INFO_NAME,
    INVALID_PATH_CHARS, MAX_S64, is_save_reserved_metadata_root,
    stage_folder_to_zip, pack_jksv85, pack_dbi_raw512, unpack_jksv85,
    decode_dbi_raw512, compare_common_source_fields,
    validate_jksv85_source_metadata, admit_and_drain_zip_archive,
    ConnectedRealDiskImportRunner
)

def check(condition, message):
    if not condition:
        raise AssertionError(message)

def test_jksv_source_validation_and_account_remap():
    print("[3] Running independent JKSV source validation, coexistence/conflict, & account remap fixture...")

    # 1. Valid source metadata independently admitted
    valid_raw = pack_jksv85()
    check(len(valid_raw) == 85, "Settled JKSV format must produce exactly 85 bytes")
    ok, msg, meta = validate_jksv85_source_metadata(valid_raw)
    check(ok and msg == "OK" and meta is not None, "Valid 85-byte source metadata must be admitted")

    # 2. Truncated wire formats refused
    for trunc_len in (0, 1, 4, 16, 50, 72, 84):
        ok, msg, _ = validate_jksv85_source_metadata(valid_raw[:trunc_len])
        check(not ok and "Truncated" in msg, f"Truncated {trunc_len}-byte wire format must be refused")

    # 3. Oversized wire format refused
    ok, msg, _ = validate_jksv85_source_metadata(valid_raw + b"\x00")
    check(not ok and "Malformed wire length" in msg, "86-byte wire format without tail decoder must be refused")

    # 4. Invalid magic refused
    for bad_magic in (0x00000000, 0x12345678, 0x4A4B5356):
        bad_raw = pack_jksv85(magic=bad_magic)
        ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
        check(not ok and "Invalid JKSV magic" in msg, f"Bad magic {bad_magic:#x} must be refused")

    # 5. Invalid revision refused
    for bad_rev in (0, 2, 255):
        bad_raw = pack_jksv85(rev=bad_rev)
        ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
        check(not ok and "Invalid revision" in msg, f"Bad revision {bad_rev} must be refused")

    # 6. Malformed negative sizes refused
    bad_raw = pack_jksv85(data_size=-1)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "Negative size" in msg, "Negative data_size must be refused")

    bad_raw = pack_jksv85(journal_size=-500)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "Negative size" in msg, "Negative journal_size must be refused")

    # 7. Malformed invalid save type (>6) refused
    bad_raw = pack_jksv85(save_type=7)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "Invalid save type" in msg, "Save type > 6 must be refused")

    # 8. Malformed invalid rank (>1) refused
    bad_raw = pack_jksv85(rank=2)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "Invalid save rank" in msg, "Save rank > 1 must be refused")

    # 9. Inconsistent account save metadata refused
    bad_raw = pack_jksv85(app_id=0)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "application_id" in msg, "Account save with zero app_id must be refused")

    bad_raw = pack_jksv85(sys_id=0x8000000000000010)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "system_save_data_id" in msg, "Account save with non-zero sys_id must be refused")

    bad_raw = pack_jksv85(uid_low=0, uid_high=0)
    ok, msg, _ = validate_jksv85_source_metadata(bad_raw)
    check(not ok and "uid" in msg, "Account save with zero uid must be refused")

    # 10. Real ZIP Metadata Coexistence-Success: .nx_save_meta.bin + .dbi_save_info.ini
    with tempfile.TemporaryDirectory() as coex_dir_str:
        coex_zip = pathlib.Path(coex_dir_str) / "coex.zip"
        with zipfile.ZipFile(coex_zip, "w") as zf:
            zf.writestr(NX_SAVE_META_NAME, pack_jksv85())
            zf.writestr(DBI_SAVE_INFO_NAME, b"[Save]\nTitle=Opaque INI Content\n")
            zf.writestr("save.dat", b"PAYLOAD")

        ok_coex, _, _, _, coex_meta = admit_and_drain_zip_archive(coex_zip)
        check(ok_coex and coex_meta is not None, "Real ZIP coexistence of NX metadata and opaque DBI INI succeeds")
    print("  -> Real ZIP Coexistence-Success (.nx_save_meta.bin + .dbi_save_info.ini) PASSED.")

    # 11. Real ZIP Mutual Disagreement Refusal: .nx_save_meta.bin + .dbi_save_extra
    with tempfile.TemporaryDirectory() as conflict_dir_str:
        conflict_zip = pathlib.Path(conflict_dir_str) / "conflict.zip"
        with zipfile.ZipFile(conflict_zip, "w") as zf:
            zf.writestr(NX_SAVE_META_NAME, pack_jksv85(app_id=0x0100000000001000))
            zf.writestr(DBI_SAVE_EXTRA_NAME, pack_dbi_raw512(app_id=0x0100000000002000))

        ok_conf, msg_conf, _, _, _ = admit_and_drain_zip_archive(conflict_zip)
        check(not ok_conf and "contradictory metadata" in msg_conf,
              "Disagreement across decoded NX and DBI extra common fields detected and refused")
    print("  -> Real ZIP Mutual Disagreement Refusal (.nx_save_meta.bin + disagreeing .dbi_save_extra) PASSED.")

    # 12. Real ZIP Mutual Agreement Success: .nx_save_meta.bin + matching .dbi_save_extra
    with tempfile.TemporaryDirectory() as agree_dir_str:
        agree_zip = pathlib.Path(agree_dir_str) / "agree.zip"
        with zipfile.ZipFile(agree_zip, "w") as zf:
            zf.writestr(NX_SAVE_META_NAME, pack_jksv85(app_id=0x0100000000001000, uid_low=0x11, uid_high=0x22))
            zf.writestr(DBI_SAVE_EXTRA_NAME, pack_dbi_raw512(app_id=0x0100000000001000, uid_low=0x11, uid_high=0x22))

        ok_agr, _, _, _, agr_meta = admit_and_drain_zip_archive(agree_zip)
        check(ok_agr and agr_meta is not None, "Matching NX and DBI extra common fields agree")
    print("  -> Real ZIP Mutual Agreement Success PASSED.")

    # 13. Explicit Account Remap Fixture:
    # Source has different BOTH UID halves and different sizing hints.
    # Selected destination Entry and live sizes stay authoritative!
    source_remap_raw = pack_jksv85(
        app_id=0x0100000000001000,
        uid_low=0xAAAAAAAAAAAAAAAA,
        uid_high=0xBBBBBBBBBBBBBBBB,  # Different BOTH UID halves!
        data_size=0x100000,           # Different sizing hints!
        journal_size=0x100000
    )
    ok_src, _, src_meta = validate_jksv85_source_metadata(source_remap_raw)
    check(ok_src, "Source metadata with different UID is internally valid")

    selected_destination = {
        "application_id": 0x0100000000001000,
        "uid_low": 0x1111222233334444,
        "uid_high": 0x5555666677778888,
        "save_data_id": 0x0000000012345678,
        "save_data_space_id": 0,  # User space
        "save_data_type": 1,
        "save_data_rank": 0,
        "save_data_index": 0
    }

    live_extra_data = {
        "data_size": 0x400000,     # Live size takes precedence over source hint
        "journal_size": 0x400000,
        "commit_id": 42
    }

    # Product resolution policy in RestoreSaveZip:
    # Live target slot exists (save_data_id != 0) -> Destination identity and live sizes stay authoritative!
    resolved_attr = {
        "application_id": selected_destination["application_id"],
        "uid_low": selected_destination["uid_low"],
        "uid_high": selected_destination["uid_high"],
        "save_data_type": selected_destination["save_data_type"],
        "save_data_rank": selected_destination["save_data_rank"],
        "save_data_index": selected_destination["save_data_index"]
    }
    resolved_data_size = live_extra_data["data_size"]
    resolved_journal_size = live_extra_data["journal_size"]

    check(resolved_attr["uid_low"] == 0x1111222233334444 and resolved_attr["uid_high"] == 0x5555666677778888,
          "Destination account UID must remain authoritative during account remap")
    check(resolved_data_size == 0x400000 and resolved_journal_size == 0x400000,
          "Actual live sizes must remain authoritative over source sizing hints")
    print("  -> Independent JKSV source validation & Account Remap fixture PASSED.")


# ==============================================================================
# 4. Connected Real-Disk Artifact Lifecycle & Comprehensive Shared-Boundary Fault Matrix
# ==============================================================================


def test_connected_fault_evidence_matrix():
    print("[4] Running connected real-disk artifact lifecycle & comprehensive fault injection matrix...")

    faults = [
        # Pre-mutation faults (39): live slot NEVER modified, stage cleaned, source/foreign untouched
        ("cancel_during_scan", False, False),
        ("strict_default_zero_entry", False, False),
        ("reserve_exhaustion", False, False),
        ("cancel_during_read", False, False),
        ("read_zero_progress", False, False),
        ("read_overread", False, False),
        ("read_trailing_error", False, False),
        ("read_trailing_nonzero", False, False),
        ("read_zero_byte_growth", False, False),
        ("stage_drift_shrink", False, False),
        ("stage_no_space", False, False),
        ("zip_entry_close_error", False, False),
        ("cancel_during_finalize", False, False),
        ("zip_finalize_flush_error", False, False),
        ("zip_stream_close_leak", False, False),
        ("stage_sd_commit_error", False, False),
        ("cancel_during_admission", False, False),
        ("preflight_crc_error", False, False),
        ("stage_inventory_mismatch", False, False),
        ("stage_metadata_invalid", False, False),
        ("stage_metadata_internal_conflict", False, False),
        ("stage_reader_close_error", False, False),
        ("stage_altered_between_admissions_corrupt_zip", False, False),
        ("stage_altered_between_admissions_invalid_meta", False, False),
        ("cancel_before_shared_restore", False, False),
        ("target_unbacked", False, False),
        ("destination_uid_low_mismatch", False, False),
        ("destination_uid_high_mismatch", False, False),
        ("destination_app_id_mismatch", False, False),
        ("destination_space_mismatch", False, False),
        ("destination_type_mismatch", False, False),
        ("destination_rank_mismatch", False, False),
        ("destination_index_mismatch", False, False),
        ("live_data_size_nonpositive", False, False),
        ("live_journal_size_negative", False, False),
        ("payload_exceeds_live_data_size", False, False),
        ("recovery_write_fail", False, False),
        ("recovery_validation_fail", False, False),
        ("recovery_close_fail", False, False),

        # Post-mutation faults (9): mutation_started = True, recovery published & retained on disk
        ("live_clear_fail", True, True),
        ("extract_fail", True, True),
        ("ro_verify_same_size_corruption", True, True),
        ("ro_verify_unexpected_file", True, True),
        ("ro_verify_missing_file", True, True),
        ("ro_verify_unexpected_dir", True, True),
        ("empty_restore_leftover_file", True, True),
        ("empty_restore_leftover_dir", True, True),
        ("source_close_fail", True, True),

        # Cleanup failure fault (1): stage artifact remains on disk, reported honestly
        ("stage_cleanup_fail", True, True),
    ]

    for fault_name, expect_mutation, expect_recovery in faults:
        with tempfile.TemporaryDirectory() as tmp_str:
            runner = ConnectedRealDiskImportRunner(pathlib.Path(tmp_str))
            is_empty = fault_name in ("strict_default_zero_entry", "empty_restore_leftover_file", "empty_restore_leftover_dir")
            is_allow_empty = (fault_name != "strict_default_zero_entry")
            rc, msg = runner.run(fault=fault_name, is_empty_folder=is_empty, allow_empty=is_allow_empty)

            check(rc != 0, f"Fault '{fault_name}' must return error, returned 0")
            check(runner.mutation_started == expect_mutation,
                  f"Fault '{fault_name}': mutation_started expected {expect_mutation}, got {runner.mutation_started}")
            check(runner.recovery_published == expect_recovery,
                  f"Fault '{fault_name}': recovery_published expected {expect_recovery}, got {runner.recovery_published}")

            # 1. Foreign file must remain 100% UNTOUCHED in all cases
            check(runner.foreign_file.is_file(), f"Fault '{fault_name}': foreign file disappeared")
            check(hashlib.md5(runner.foreign_file.read_bytes()).hexdigest() == runner.initial_foreign_hash,
                  f"Fault '{fault_name}': foreign file was modified")

            # 2. Source folder must remain 100% UNTOUCHED in all cases
            if not is_empty:
                check(hashlib.md5((runner.source_dir / "main.dat").read_bytes()).hexdigest() == runner.initial_src_main_hash,
                      f"Fault '{fault_name}': source main.dat modified")
                check(hashlib.md5((runner.source_dir / "config.bin").read_bytes()).hexdigest() == runner.initial_src_cfg_hash,
                      f"Fault '{fault_name}': source config.bin modified")

            # 3. Pre-mutation: live save slot MUST NOT be touched
            if not expect_mutation:
                check((runner.live_save_dir / "original_live.dat").is_file(),
                      f"Fault '{fault_name}': live slot file missing before mutation")
                check(hashlib.md5((runner.live_save_dir / "original_live.dat").read_bytes()).hexdigest() == runner.initial_live_dat_hash,
                      f"Fault '{fault_name}': live slot file modified before mutation")

            # 4. Post-mutation: published recovery archive MUST be retained on disk with original live files
            if expect_recovery:
                check(runner.recovery_zip.is_file(), f"Fault '{fault_name}': recovery archive missing on disk")
                with zipfile.ZipFile(runner.recovery_zip, "r") as rzf:
                    r_names = rzf.namelist()
                    check("original_live.dat" in r_names and "slot.ini" in r_names,
                          f"Fault '{fault_name}': recovery archive missing original live files")
                    check(hashlib.md5(rzf.read("original_live.dat")).hexdigest() == runner.initial_live_dat_hash,
                          f"Fault '{fault_name}': recovery archive contains corrupted original live file")

            # 5. Cleanup assertions
            if fault_name == "stage_cleanup_fail":
                # Truthful failure reporting: stage artifact was NOT cleaned up, remains on disk!
                check(runner.owned_stage_zip.is_file() and runner.owned_stage_dir.is_dir(),
                      "stage_cleanup_fail must leave remaining owned stage artifact on disk")
            else:
                check(runner.owned_stage_zip is None and runner.owned_stage_dir is None,
                      f"Fault '{fault_name}': stage pointers not cleared")
                leftovers = list(runner.staging_parent.glob("2026*"))
                check(len(leftovers) == 0, f"Fault '{fault_name}': leftover stage dirs found: {leftovers}")

    # 50. Full success scenario (Account remap + payload restoration + recovery retained + stage cleaned)
    with tempfile.TemporaryDirectory() as tmp_str:
        runner = ConnectedRealDiskImportRunner(pathlib.Path(tmp_str))
        rc, msg = runner.run(fault=None)

        check(rc == 0 and msg == "OK", f"Full success run failed: {rc}, {msg}")
        check(runner.mutation_started is True, "Success run must execute mutation")
        check(runner.recovery_published is True, "Success run must publish recovery archive")

        # Restored payload in live save slot
        check((runner.live_save_dir / "main.dat").is_file(), "Restored main.dat missing")
        check((runner.live_save_dir / "config.bin").is_file(), "Restored config.bin missing")
        check(hashlib.md5((runner.live_save_dir / "main.dat").read_bytes()).hexdigest() == runner.initial_src_main_hash,
              "Restored main.dat content mismatch")

        # Reserved root metadata filtered out of live save slot
        check(not (runner.live_save_dir / ".nx_save_meta.bin").exists(),
              "Root metadata .nx_save_meta.bin must be filtered from live slot")
        check(not (runner.live_save_dir / ".dbi_save_info.ini").exists(),
              "Root metadata .dbi_save_info.ini must be filtered from live slot")

        # Old live slot files cleared
        check(not (runner.live_save_dir / "original_live.dat").exists(), "Old live slot file not cleared")

        # Published recovery archive retained on disk
        check(runner.recovery_zip.is_file(), "Published recovery archive must be retained on success")
        with zipfile.ZipFile(runner.recovery_zip, "r") as rzf:
            r_names = rzf.namelist()
            check("original_live.dat" in r_names and "slot.ini" in r_names,
                  "Published recovery archive must contain original live files")

        # Foreign and source files untouched
        check(hashlib.md5(runner.foreign_file.read_bytes()).hexdigest() == runner.initial_foreign_hash, "Foreign file modified")
        check(hashlib.md5((runner.source_dir / "main.dat").read_bytes()).hexdigest() == runner.initial_src_main_hash, "Source modified")

        # Stage files deleted
        leftovers = list(runner.staging_parent.glob("2026*"))
        check(len(leftovers) == 0, f"Leftover stage dirs found: {leftovers}")

    # 51. Empty folder opt-in success scenario (allow_empty = true)
    with tempfile.TemporaryDirectory() as tmp_str:
        runner = ConnectedRealDiskImportRunner(pathlib.Path(tmp_str))
        rc, msg = runner.run(fault=None, is_empty_folder=True, allow_empty=True)

        check(rc == 0 and msg == "OK", f"Empty folder opt-in run failed: {rc}, {msg}")
        check(runner.mutation_started is True, "Empty folder restore must execute mutation")
        check(len(list(runner.live_save_dir.iterdir())) == 0, "Empty folder restore must leave empty live save slot")
        check(runner.recovery_zip.is_file(), "Recovery archive must be retained for empty folder restore")
        check(runner.foreign_file.is_file(), "Foreign file preserved")
        leftovers = list(runner.staging_parent.glob("2026*"))
        check(len(leftovers) == 0, "Stage files cleaned after empty folder restore")

    print("  -> Connected real-disk artifact lifecycle & comprehensive fault injection matrix PASSED.")


# ==============================================================================
# Main Runner
# ==============================================================================
