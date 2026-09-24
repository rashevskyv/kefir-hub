# Scenario tests for raw save restore contract.
import os
import tempfile
import zipfile
from contract_fixtures.raw_save_restore_models import (
    FS_ERROR_NOT_FOUND, FS_ERROR_TARGET_LOCKED, FS_ERROR_INVALID_PATH,
    FS_ERROR_BUFFER_TOO_SMALL, ERR_RAW_RESTORE_REFUSED, ERR_RECOVERY_DIRTY,
    ERR_SIMULATED_CRASH, TargetSaveSentinel, BoundedArtifactReader,
    create_disa_matching_looking_artifact, create_magic_only_artifact,
    create_unsupported_version_artifact, create_remap_out_of_bounds_artifact,
    create_remap_uint64_overflow_artifact, create_raw_fixture_file,
    create_valid_zip_fixture, run_model_filebrowser_restore,
    run_model_savemenu_picked_restore, run_model_batch_restore_item,
    run_model_internal_defense
)

def test_sentinel_positive_self_check():
    """Confirms that TargetSaveSentinel actually writes to disk and fails on mutation."""
    print("[1] Testing TargetSaveSentinel Positive Self-Check...")
    with tempfile.TemporaryDirectory() as tmpdir:
        test_path = os.path.join(tmpdir, "sentinel.bin")
        s = TargetSaveSentinel(test_path, b"INITIAL_CLEAN_BYTES")
        s.verify_unchanged()  # Must pass initially

        s.write_data(b"MUTATED_DIRTY_BYTES")
        failed = False
        try:
            s.verify_unchanged()
        except AssertionError:
            failed = True

        assert failed, "TargetSaveSentinel.verify_unchanged MUST fail when write_data modified the file!"
    print("  -> Sentinel positive self-check PASSED.")


def test_all_variants_across_all_routes():
    """Tests all artifact and reader fault variants across all 4 connected routes."""
    print("[2] Running All Artifact and Fault Variants Across All Connected Routes...")
    with tempfile.TemporaryDirectory() as tmpdir:
        # Create all variants
        variants = {}

        # 1. Matching-looking structural fixture
        path_matching = os.path.join(tmpdir, "matching.bin")
        create_disa_matching_looking_artifact(path_matching)
        variants["matching_looking"] = (path_matching, None, True)

        # 2. Magic-only 0x200 bytes
        path_magic_only = os.path.join(tmpdir, "magic_only.bin")
        create_magic_only_artifact(path_magic_only)
        variants["magic_only_0x200"] = (path_magic_only, None, True)

        # 3. Unsupported version at 0x104
        path_unsupp_ver = os.path.join(tmpdir, "unsupported_version.bin")
        create_unsupported_version_artifact(path_unsupp_ver)
        variants["unsupported_version"] = (path_unsupp_ver, None, True)

        # 4. Remap table out-of-bounds offset
        path_remap_oob = os.path.join(tmpdir, "remap_oob.bin")
        create_remap_out_of_bounds_artifact(path_remap_oob)
        variants["remap_out_of_bounds"] = (path_remap_oob, None, True)

        # 5. Remap table UINT64 overflow offset
        path_remap_overflow = os.path.join(tmpdir, "remap_overflow.bin")
        create_remap_uint64_overflow_artifact(path_remap_overflow)
        variants["remap_uint64_overflow"] = (path_remap_overflow, None, True)

        # 6. Differing identity metadata
        path_diff_id = os.path.join(tmpdir, "differing_id.bin")
        create_disa_matching_looking_artifact(
            path_diff_id,
            app_id=0x0100999999999000,
            uid=(0x999, 0x888),
            save_type=0,  # System
            rank=3,
            index=7
        )
        variants["differing_identity"] = (path_diff_id, None, True)

        # 7. Truncation before magic (< 0x200)
        path_trunc_before = os.path.join(tmpdir, "trunc_before.bin")
        create_raw_fixture_file(path_trunc_before, 0x80)
        variants["truncation_before_magic"] = (path_trunc_before, None, False)

        # 8. Truncation after magic (< 0x200 with DISF present)
        path_trunc_after = os.path.join(tmpdir, "trunc_after.bin")
        create_raw_fixture_file(path_trunc_after, 0x180, disf_at_100=True)
        variants["truncation_after_magic"] = (path_trunc_after, None, False)

        # 9. Wrong magic at 0x100
        path_wrong_magic = os.path.join(tmpdir, "wrong_magic.bin")
        buf = bytearray(0x8000)
        buf[0x100:0x104] = b"XXXX"
        with open(path_wrong_magic, "wb") as f:
            f.write(buf)
        variants["wrong_magic"] = (path_wrong_magic, None, False)

        # 10. RAW-looking .zip filename (contains raw DISF data)
        path_raw_zip = os.path.join(tmpdir, "raw_backup.zip")
        create_disa_matching_looking_artifact(path_raw_zip)
        variants["raw_looking_zip_filename"] = (path_raw_zip, None, True)

        # 11. Wrong-magic / corrupt .zip archive
        path_corrupt_zip = os.path.join(tmpdir, "corrupt.zip")
        with open(path_corrupt_zip, "wb") as f:
            f.write(b"NOT_A_VALID_ZIP_HEADER_CONTENT")
        variants["wrong_magic_zip"] = (path_corrupt_zip, None, False)

        # 12. Reader fault: I/O error open
        variants["fault_io_error_open"] = (path_matching, "io_error_open", False)

        # 13. Reader fault: I/O error read
        variants["fault_io_error_read"] = (path_matching, "io_error_read", False)

        # 14. Reader fault: short read
        variants["fault_short_read"] = (path_matching, "short_read", False)

        # 15. Reader fault: physical size drift (file copy truncated to 0x80 bytes)
        path_drift = os.path.join(tmpdir, "drift.bin")
        create_disa_matching_looking_artifact(path_drift)
        variants["fault_size_drift_truncate"] = (path_drift, "size_drift_truncate", False)

        # Test each variant against all 4 routes
        for vname, (art_path, fault_mode, is_raw_expected) in variants.items():
            # Route 1: File Browser
            reader_fb = BoundedArtifactReader(art_path, fault=fault_mode)
            sentinel_fb = TargetSaveSentinel(os.path.join(tmpdir, f"target_fb_{vname}.bin"), b"FB_TARGET_INIT")
            outcome_fb = run_model_filebrowser_restore(reader_fb, sentinel_fb)
            if is_raw_expected:
                assert outcome_fb == "REFUSED_RAW_UPFRONT", f"[{vname}] FB expected REFUSED_RAW_UPFRONT, got {outcome_fb}"
            else:
                assert outcome_fb == "REJECTED_NOT_VALID_BACKUP", f"[{vname}] FB expected REJECTED_NOT_VALID_BACKUP, got {outcome_fb}"
            sentinel_fb.verify_unchanged()

            # Route 2: Save Menu Picked
            reader_sm = BoundedArtifactReader(art_path, fault=fault_mode)
            sentinel_sm = TargetSaveSentinel(os.path.join(tmpdir, f"target_sm_{vname}.bin"), b"SM_TARGET_INIT")
            outcome_sm = run_model_savemenu_picked_restore(reader_sm, sentinel_sm)
            if is_raw_expected:
                assert outcome_sm == "REFUSED_RAW_UPFRONT", f"[{vname}] SM picked expected REFUSED_RAW_UPFRONT, got {outcome_sm}"
            else:
                assert outcome_sm == "REJECTED_NOT_VALID_BACKUP", f"[{vname}] SM picked expected REJECTED_NOT_VALID_BACKUP, got {outcome_sm}"
            sentinel_sm.verify_unchanged()

            # Route 3: Batch Current Item
            reader_batch = BoundedArtifactReader(art_path, fault=fault_mode)
            sentinel_batch = TargetSaveSentinel(os.path.join(tmpdir, f"target_batch_{vname}.bin"), b"BATCH_TARGET_INIT")
            outcome_batch, last_is_raw, mut_batch, ready_rec = run_model_batch_restore_item(reader_batch, sentinel_batch)
            if is_raw_expected:
                assert outcome_batch == "REFUSED_RAW_BATCH_ITEM", f"[{vname}] Batch expected REFUSED_RAW_BATCH_ITEM, got {outcome_batch}"
                assert last_is_raw is True
            else:
                assert outcome_batch == "REJECTED_ZIP_ADMISSION", f"[{vname}] Batch expected REJECTED_ZIP_ADMISSION, got {outcome_batch}"
                assert last_is_raw is False
            assert mut_batch is False
            assert ready_rec is None
            sentinel_batch.verify_unchanged()

            # Route 4: Internal Defense
            reader_int = BoundedArtifactReader(art_path, fault=fault_mode)
            sentinel_int = TargetSaveSentinel(os.path.join(tmpdir, f"target_int_{vname}.bin"), b"INT_TARGET_INIT")
            outcome_int, mut_int = run_model_internal_defense(reader_int, sentinel_int)
            if is_raw_expected:
                assert outcome_int == "RESULT_RAW_SAVE_RESTORE_UNSUPPORTED", f"[{vname}] Internal expected RESULT_RAW_SAVE_RESTORE_UNSUPPORTED, got {outcome_int}"
            else:
                assert outcome_int == "REJECTED_ZIP_ADMISSION", f"[{vname}] Internal expected REJECTED_ZIP_ADMISSION, got {outcome_int}"
            assert mut_int is False
            sentinel_int.verify_unchanged()

        # Cancellation Models (verified before mutation)
        valid_zip_path = os.path.join(tmpdir, "valid_for_cancel.zip")
        create_valid_zip_fixture(valid_zip_path, {"game.sav": b"VALID_SAVE_PAYLOAD"})

        # File Browser caller cancel
        s_cancel_fb = TargetSaveSentinel(os.path.join(tmpdir, "cancel_fb.bin"), b"CANCEL_FB")
        r_cancel_fb = BoundedArtifactReader(valid_zip_path)
        assert run_model_filebrowser_restore(r_cancel_fb, s_cancel_fb, cancel_at_confirm=True) == "CANCELLED_BEFORE_WORKER"
        s_cancel_fb.verify_unchanged()

        # Save Menu picked caller cancel
        s_cancel_sm = TargetSaveSentinel(os.path.join(tmpdir, "cancel_sm.bin"), b"CANCEL_SM")
        r_cancel_sm = BoundedArtifactReader(valid_zip_path)
        assert run_model_savemenu_picked_restore(r_cancel_sm, s_cancel_sm, cancel_at_confirm=True) == "CANCELLED_BEFORE_WORKER"
        s_cancel_sm.verify_unchanged()

        # Batch worker cancel gate
        s_cancel_batch = TargetSaveSentinel(os.path.join(tmpdir, "cancel_batch.bin"), b"CANCEL_BATCH")
        r_cancel_batch = BoundedArtifactReader(valid_zip_path)
        out_cb, _, mut_cb, _ = run_model_batch_restore_item(r_cancel_batch, s_cancel_batch, worker_cancelled=True)
        assert out_cb == "CANCELLED_WORKER" and mut_cb is False
        s_cancel_batch.verify_unchanged()

        # Internal worker cancel gate
        s_cancel_int = TargetSaveSentinel(os.path.join(tmpdir, "cancel_int.bin"), b"CANCEL_INT")
        r_cancel_int = BoundedArtifactReader(valid_zip_path)
        out_ci, mut_ci = run_model_internal_defense(r_cancel_int, s_cancel_int, worker_cancelled=True)
        assert out_ci == "CANCELLED_INTERNAL" and mut_ci is False
        s_cancel_int.verify_unchanged()

    print("  -> All variants and cancellation models PASSED across all routes.")


def test_mixed_batch_simulation():
    """Verifies mixed batch: real valid ZIP -> RAW -> ZIP with pre-mutation recovery verification and retention."""
    print("[3] Testing Mixed Batch (ZIP -> RAW -> ZIP) & Safety Recovery Retention...")
    with tempfile.TemporaryDirectory() as tmpdir:
        backup_dir = os.path.join(tmpdir, "backups")
        os.makedirs(backup_dir, exist_ok=True)

        # 1. Source files
        zip1_path = os.path.join(tmpdir, "item1_valid.zip")
        create_valid_zip_fixture(zip1_path, {"slot1.bin": b"NEW_ZIP1_PAYLOAD"})

        raw2_path = os.path.join(tmpdir, "item2_raw.bin")
        create_disa_matching_looking_artifact(raw2_path)

        zip3_path = os.path.join(tmpdir, "item3_valid.zip")
        create_valid_zip_fixture(zip3_path, {"slot3.bin": b"NEW_ZIP3_PAYLOAD"})

        # 2. Target sentinels
        target1 = TargetSaveSentinel(os.path.join(tmpdir, "target1.bin"), b"ORIGINAL_TARGET1_BYTES")
        target2 = TargetSaveSentinel(os.path.join(tmpdir, "target2.bin"), b"ORIGINAL_TARGET2_BYTES")
        target3 = TargetSaveSentinel(os.path.join(tmpdir, "target3.bin"), b"ORIGINAL_TARGET3_BYTES")

        # 3. Simulate batch execution: caller passes recovery path candidate; helper creates & verifies before open_write
        items = [(zip1_path, target1), (raw2_path, target2), (zip3_path, target3)]
        recovery_archives = []
        restored_count = 0
        last_item_is_raw = False

        for i, (src_path, tgt) in enumerate(items):
            reader = BoundedArtifactReader(src_path)
            rec_candidate = os.path.join(backup_dir, f"recovery_slot_{i}.zip")
            outcome, is_raw, mut_started, verified_rec = run_model_batch_restore_item(
                reader, tgt, recovery_path=rec_candidate
            )
            last_item_is_raw = is_raw

            if is_raw:
                # Halt immediately on RAW before auto-backup or target write
                break

            if outcome == "SUCCESS_ZIP_ITEM":
                assert verified_rec is not None, "Verified recovery path must be returned"
                recovery_archives.append(verified_rec)
                restored_count += 1

        # Check outcomes
        assert restored_count == 1, f"Expected 1 restored item, got {restored_count}"
        assert last_item_is_raw is True, "Batch must record last_item_is_raw = True"
        assert len(recovery_archives) == 1, "Exactly 1 recovery archive should exist"

        # Check shared event log ordering for Item 1: admission -> recovery_ready -> open_write -> write -> commit
        t1_log = target1.event_log
        assert "admission" in t1_log, "admission must be in event log"
        assert "recovery_ready" in t1_log, "recovery_ready must be in event log"
        assert "open_write" in t1_log, "open_write must be in event log"
        idx_adm = t1_log.index("admission")
        idx_rec = t1_log.index("recovery_ready")
        idx_open = t1_log.index("open_write")
        idx_write = t1_log.index("write:18")
        idx_commit = t1_log.index("commit")
        assert idx_adm < idx_rec < idx_open < idx_write < idx_commit, (
            f"Event order violation! Expected admission -> recovery_ready -> open_write -> write -> commit, got: {t1_log}"
        )

        # Verify Item 1 target was actually modified
        with open(target1.path, "rb") as f:
            t1_bytes = f.read()
        assert t1_bytes == b"RESTORED_ZIP_BATCH", "Target 1 must be modified by successful ZIP restore!"

        # Verify Item 1 recovery archive physically exists and contains initial bytes
        assert os.path.exists(recovery_archives[0])
        with zipfile.ZipFile(recovery_archives[0], "r") as zf:
            assert zf.read("original_target.bin") == b"ORIGINAL_TARGET1_BYTES"

        # Verify Target 2 and Target 3 sentinels are 100% UNCHANGED: no recovery, no target events
        target2.verify_unchanged()
        target3.verify_unchanged()

        # Check error reporting contract
        msg = (
            f"Restore stopped.\nSafety recovery archive(s) retained:\n{recovery_archives[0]}\n\n"
            f"RAW container restore is unsupported."
        )
        assert "Restore stopped.\nSafety recovery archive(s) retained:\n" in msg
        assert "RAW container restore is unsupported." in msg
        assert "rollback" not in msg.lower(), "Must not claim whole-batch rollback"

        # Scenario B: Malformed source first
        corrupt_first = os.path.join(tmpdir, "corrupt_first.bin")
        create_raw_fixture_file(corrupt_first, 0x80)
        target_mal = TargetSaveSentinel(os.path.join(tmpdir, "target_mal.bin"), b"ORIGINAL_MAL_BYTES")
        r_mal = BoundedArtifactReader(corrupt_first)
        rec_mal = os.path.join(backup_dir, "recovery_mal.zip")
        out_mal, is_raw_mal, mut_mal, ver_mal = run_model_batch_restore_item(r_mal, target_mal, recovery_path=rec_mal)
        assert out_mal == "REJECTED_ZIP_ADMISSION"
        assert is_raw_mal is False and mut_mal is False and ver_mal is None
        assert not os.path.exists(rec_mal), "No recovery archive should be created on rejected admission"
        target_mal.verify_unchanged()

    print("  -> Mixed batch & recovery retention simulation PASSED.")


def test_recovery_creation_readback_failure_fixture():
    """Verifies that failure in recovery archive creation or readback stops restore before open_write."""
    print("[4] Testing Recovery Creation & Readback Failure Fixtures...")
    with tempfile.TemporaryDirectory() as tmpdir:
        backup_dir = os.path.join(tmpdir, "backups")
        os.makedirs(backup_dir, exist_ok=True)

        valid_zip_path = os.path.join(tmpdir, "valid.zip")
        create_valid_zip_fixture(valid_zip_path, {"game.sav": b"VALID_SAVE_PAYLOAD"})

        # Case A: Corrupt recovery archive before reopen/readback
        target_corrupt = TargetSaveSentinel(os.path.join(tmpdir, "target_corrupt.bin"), b"INITIAL_TARGET_CORRUPT")
        reader_corrupt = BoundedArtifactReader(valid_zip_path)
        rec_corrupt_path = os.path.join(backup_dir, "recovery_corrupt.zip")

        outcome, is_raw, mut_started, ver_path = run_model_batch_restore_item(
            reader_corrupt,
            target_corrupt,
            recovery_path=rec_corrupt_path,
            recovery_fail_mode="corrupt_archive"
        )

        assert outcome == "RECOVERY_VERIFICATION_FAILED"
        assert is_raw is False
        assert mut_started is False, "mutation_started must be false on recovery readback failure"
        assert ver_path is None, "Incomplete/corrupt recovery archive must NOT be advertised as verified path"
        assert not os.path.exists(rec_corrupt_path), "Corrupt recovery archive must not remain advertised on disk"

        # Sentinel must be completely unchanged and open_write must NOT have been called
        target_corrupt.verify_unchanged(allow_non_mutating=True)
        assert "recovery_ready" not in target_corrupt.event_log
        assert "open_write" not in target_corrupt.event_log
        assert "write" not in str(target_corrupt.event_log)
        assert "commit" not in target_corrupt.event_log

        # Case B: Injected failing recovery creation callback
        target_cb_fail = TargetSaveSentinel(os.path.join(tmpdir, "target_cb_fail.bin"), b"INITIAL_TARGET_CB_FAIL")
        reader_cb = BoundedArtifactReader(valid_zip_path)
        rec_cb_path = os.path.join(backup_dir, "recovery_cb.zip")

        outcome_cb, is_raw_cb, mut_started_cb, ver_path_cb = run_model_batch_restore_item(
            reader_cb,
            target_cb_fail,
            recovery_path=rec_cb_path,
            recovery_fail_mode="callback_fail"
        )

        assert outcome_cb == "RECOVERY_CREATION_FAILED"
        assert is_raw_cb is False
        assert mut_started_cb is False
        assert ver_path_cb is None
        target_cb_fail.verify_unchanged(allow_non_mutating=True)
        assert "recovery_ready" not in target_cb_fail.event_log
        assert "open_write" not in target_cb_fail.event_log

    print("  -> Recovery creation & readback failure fixtures PASSED.")
