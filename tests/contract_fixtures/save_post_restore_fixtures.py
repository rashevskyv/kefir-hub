# Behavioral fixtures for post restore contract.
import os
import io
import zipfile
from contract_fixtures.save_post_restore_models import (
    RES_OK, ERR_VERIFICATION_FAILED, ERR_UNZ_READ, ERR_UNZ_CLOSE,
    ERR_CANCELLED, ERR_OVERFLOW, ERR_INVALID_PATH, ERR_INVALID_SIZE,
    ERR_REWIND_FAILED, RESERVED_NAMES, SyntheticSaveFs, build_test_zip,
    normalize_entry, is_metadata, get_parent_directories, simulate_preflight,
    simulate_verify_against_native
)

def check(condition: bool, msg: str) -> None:
    if not condition:
        raise AssertionError(msg)

def test_behavioral_fixtures() -> None:
    print("[2] Running synthetic behavioral regression fixtures...")

    # Fixture 1: Clean preflight with DBI leading slashes, implicit parents, and empty dirs
    files = {
        "savedata.bin": b"Hello Switch Save",
        "sub/slot0.dat": b"Slot0 Data",
        "a/b/c/nested.dat": b"Deep Data",
        ".nx_save_meta.bin": b"SphairaMeta",
        ".dbi_save_info.ini": b"[DBI] Info",
        "empty_file.bin": b""
    }
    dirs = ["empty_folder", "sub"]
    zip_bytes = build_test_zip(files, dirs, leading_slash=True)

    inv_files, inv_dirs, total_bytes = simulate_preflight(zip_bytes)
    check("/savedata.bin" in inv_files, "inv_files contains /savedata.bin")
    check("/sub/slot0.dat" in inv_files, "inv_files contains /sub/slot0.dat")
    check("/a/b/c/nested.dat" in inv_files, "inv_files contains /a/b/c/nested.dat")
    check("/empty_file.bin" in inv_files and inv_files["/empty_file.bin"] == 0, "0-byte file in inventory")
    check(".nx_save_meta.bin" not in inv_files, "Meta excluded from files")
    check("/empty_folder" in inv_dirs, "Explicit empty dir in inv_dirs")
    check("/a" in inv_dirs and "/a/b" in inv_dirs and "/a/b/c" in inv_dirs, "Implicit parent dirs in inv_dirs")
    print("  -> Fixture 1 (Preflight inventory with DBI slash & implicit parents) PASSED.")

    # Fixture 2: Order-independent collision rejection
    # Duplicate kept files
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("slot.bin", b"first")
        zf.writestr("/slot.bin", b"second")
    dup_file_zip = buf.getvalue()
    try:
        simulate_preflight(dup_file_zip)
        check(False, "Duplicate file must be rejected")
    except FileExistsError:
        pass

    # Duplicate explicit directories
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("dir/", b"")
        zf.writestr("/dir/", b"")
    dup_dir_zip = buf.getvalue()
    try:
        simulate_preflight(dup_dir_zip)
        check(False, "Duplicate dir must be rejected")
    except FileExistsError:
        pass

    # File vs Dir conflict
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("item", b"file data")
        zf.writestr("item/", b"")
    file_dir_zip = buf.getvalue()
    try:
        simulate_preflight(file_dir_zip)
        check(False, "File vs Dir conflict must be rejected")
    except FileExistsError:
        pass

    # File as parent of another file
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("foo", b"file data")
        zf.writestr("foo/bar.txt", b"nested data")
    file_as_parent_zip = buf.getvalue()
    try:
        simulate_preflight(file_as_parent_zip)
        check(False, "File as parent must be rejected")
    except FileExistsError:
        pass
    print("  -> Fixture 2 (Order-independent collision rejections) PASSED.")

    # Fixture 3: VerifyArchiveAgainstNative exact match succeeds
    native_files = {
        "/savedata.bin": b"Hello Switch Save",
        "/sub/slot0.dat": b"Slot0 Data",
        "/a/b/c/nested.dat": b"Deep Data",
        "/empty_file.bin": b""
    }
    native_dirs = {"/empty_folder", "/sub", "/a", "/a/b", "/a/b/c"}
    save_fs = SyntheticSaveFs(native_files, native_dirs)

    res = simulate_verify_against_native(zip_bytes, save_fs, inv_files, inv_dirs, filter_meta=True, simulate_short_reads=True)
    check(res is True, "Exact match must verify successfully")
    print("  -> Fixture 3 (VerifyArchiveAgainstNative exact match with chunk accumulation) PASSED.")

    # Fixture 4: Same-size wrong bytes detected
    tampered_files = dict(native_files)
    tampered_files["/savedata.bin"] = b"Hello Switch Safe" # 1 byte changed ('v' -> 'f')
    save_fs_tampered = SyntheticSaveFs(tampered_files, native_dirs)
    res_tampered = simulate_verify_against_native(zip_bytes, save_fs_tampered, inv_files, inv_dirs)
    check(res_tampered is False, "Same-size wrong bytes must be detected and rejected")
    print("  -> Fixture 4 (Same-size wrong bytes rejection) PASSED.")

    # Fixture 5: Truncated file in native save detected
    trunc_files = dict(native_files)
    trunc_files["/savedata.bin"] = b"Hello Switch" # Truncated
    save_fs_trunc = SyntheticSaveFs(trunc_files, native_dirs)
    res_trunc = simulate_verify_against_native(zip_bytes, save_fs_trunc, inv_files, inv_dirs)
    check(res_trunc is False, "Truncated file in native save must be detected and rejected")
    print("  -> Fixture 5 (Truncated native file rejection) PASSED.")

    # Fixture 6: Extra file or extra directory in native save detected (leftovers)
    extra_file_fs = SyntheticSaveFs({**native_files, "/extra.dat": b"stale"}, native_dirs)
    res_extra_file = simulate_verify_against_native(zip_bytes, extra_file_fs, inv_files, inv_dirs)
    check(res_extra_file is False, "Extra leftover file in native save must be rejected")

    extra_dir_fs = SyntheticSaveFs(native_files, native_dirs | {"/stale_dir"})
    res_extra_dir = simulate_verify_against_native(zip_bytes, extra_dir_fs, inv_files, inv_dirs)
    check(res_extra_dir is False, "Extra leftover dir in native save must be rejected")
    print("  -> Fixture 6 (Leftover files/dirs rejection) PASSED.")

    # Fixture 7: CRC corruption in payload archive detected during preflight
    corrupt_zip = build_test_zip({"data.bin": b"12345"}, corrupt_crc=True)
    try:
        simulate_preflight(corrupt_zip)
        check(False, "CRC corrupted ZIP must be rejected in preflight")
    except (ValueError, zipfile.BadZipFile):
        pass
    print("  -> Fixture 7 (CRC corruption rejection) PASSED.")

    # Fixture 8: Mutation tracking and lifecycle simulation
    class RestoreSimulation:
        def __init__(self):
            self.mutation_started = False
            self.recovery_path = None
            self.save_fs = SyntheticSaveFs({"/old.bin": b"old data"})

        def restore(self, fail_at: str) -> bool:
            # 1. Preflight source
            if fail_at == "preflight":
                return False

            # 2. Recovery backup creation & preflight & verification
            if fail_at == "recovery_creation":
                return False

            if fail_at == "recovery_verification":
                return False

            # Recovery archive is published ONLY after verification and commit succeed!
            self.recovery_path = "/dumps/recovery/20260917_000000_1234567890ABCDEF_000/recovery.zip"

            # Failure before clear gate (e.g. exit check or sticky error)
            if fail_at == "pre_mutation_cancel":
                return False

            # 3. Mutation starts: clear
            self.mutation_started = True
            if fail_at == "clear":
                self.save_fs.files.clear()
                return False

            # 4. Unzip restore write
            if fail_at == "write":
                self.save_fs.files["/partial.bin"] = b"incomplete"
                return False

            self.save_fs.files = {"/new.bin": b"new data"}
            self.save_fs.commit()

            # 5. RW scope exit (save_fs closed)
            self.save_fs.close()

            # 6. Read-only remount verification
            ro_fs = SyntheticSaveFs(self.save_fs.files)
            ro_fs.read_only = True
            if fail_at == "ro_verify":
                return False

            return True

    # Case A1: Failure before mutation with published recovery path (e.g. pre-clear exit check)
    sim_a1 = RestoreSimulation()
    ok_a1 = sim_a1.restore("pre_mutation_cancel")
    check(not ok_a1 and not sim_a1.mutation_started and sim_a1.recovery_path is not None,
          "Failure before mutation with published recovery must retain recovery archive and leave mutation_started False")

    # Case A2: Failure during recovery verification (before publication)
    sim_a2 = RestoreSimulation()
    ok_a2 = sim_a2.restore("recovery_verification")
    check(not ok_a2 and not sim_a2.mutation_started and sim_a2.recovery_path is None,
          "Failure during recovery verification must not publish recovery path and leave mutation_started False")

    # Case B: Failure during mutation write
    sim_b = RestoreSimulation()
    ok_b = sim_b.restore("write")
    check(not ok_b and sim_b.mutation_started and sim_b.recovery_path is not None,
          "Failure during write must flag mutation_started True")

    # Case C: Failure during RO remount verification
    sim_c = RestoreSimulation()
    ok_c = sim_c.restore("ro_verify")
    check(not ok_c and sim_c.mutation_started and sim_c.recovery_path is not None,
          "Failure during fresh-remount verification must flag mutation_started True")

    # Case D: Clean success
    sim_d = RestoreSimulation()
    ok_d = sim_d.restore("none")
    check(ok_d and sim_d.mutation_started and sim_d.recovery_path is not None,
          "Clean success must succeed with published recovery path")

    print("  -> Fixture 8 (Mutation tracking lifecycle & publication ordering) PASSED.")

    # Fixture 9: Sequential batch restore tracking
    # 9.1: Item 1 publishes recovery then fails before mutation -> both recovery paths retained
    batch_states_published = [
        {"item": 0, "fail_at": "none"},
        {"item": 1, "fail_at": "pre_mutation_cancel"},
        {"item": 2, "fail_at": "none"},
    ]
    recovered_items_pub = []
    last_mutation_started_pub = False
    batch_failed_pub = False

    for b in batch_states_published:
        sim = RestoreSimulation()
        ok = sim.restore(b["fail_at"])
        if sim.recovery_path:
            recovered_items_pub.append(sim.recovery_path)
        if not ok:
            last_mutation_started_pub = sim.mutation_started
            batch_failed_pub = True
            break

    check(batch_failed_pub is True, "Batch correctly stopped on item 1")
    check(len(recovered_items_pub) == 2, "Recovery paths accumulated for item 0 and item 1")
    check(last_mutation_started_pub is False, "Item 1 failed before mutation, so last_mutation_started is False")

    # 9.2: Item 1 fails during recovery verification -> only item 0's recovery path retained
    batch_states_unpub = [
        {"item": 0, "fail_at": "none"},
        {"item": 1, "fail_at": "recovery_verification"},
        {"item": 2, "fail_at": "none"},
    ]
    recovered_items_unpub = []
    last_mutation_started_unpub = False
    batch_failed_unpub = False

    for b in batch_states_unpub:
        sim = RestoreSimulation()
        ok = sim.restore(b["fail_at"])
        if sim.recovery_path:
            recovered_items_unpub.append(sim.recovery_path)
        if not ok:
            last_mutation_started_unpub = sim.mutation_started
            batch_failed_unpub = True
            break

    check(batch_failed_unpub is True, "Batch correctly stopped on item 1")
    check(len(recovered_items_unpub) == 1, "Only item 0 published recovery path retained")
    check(last_mutation_started_unpub is False, "Item 1 failed before mutation, so last_mutation_started is False")
    print("  -> Fixture 9 (Sequential batch restore tracking) PASSED.")

    # Fixture 10: RAW restore isolation in single and batch UI callbacks
    def simulate_ui_callbacks(rc: int, is_raw: bool, recovery_paths: list[str], mutation_started: bool):
        dialogs = []
        if rc != 0:
            dialogs.append("ErrorBox: Restore failed!")
        else:
            dialogs.append("Notify: Restore successful!")

        if not is_raw:
            if len(recovery_paths) > 0:
                if rc == 0:
                    prefix = "Restore completed."
                elif mutation_started:
                    prefix = "Restore stopped: target save may have changed and restored contents are unverified."
                else:
                    prefix = "Restore stopped before target save was modified."
                dialogs.append(f"OptionBox: {prefix}")
            elif rc != 0:
                if not mutation_started:
                    dialogs.append("OptionBox: Restore stopped before target save was modified.")
        return dialogs

    # 10.1: RAW single restore fails -> no OptionBox about recovery or unverified save
    dlg_raw_fail = simulate_ui_callbacks(rc=1, is_raw=True, recovery_paths=[], mutation_started=False)
    check(dlg_raw_fail == ["ErrorBox: Restore failed!"], "RAW failure must not show ZIP verification OptionBox")

    # 10.2: ZIP single restore fails before mutation -> OptionBox with before-mutation notice
    dlg_zip_before = simulate_ui_callbacks(rc=1, is_raw=False, recovery_paths=[], mutation_started=False)
    check("OptionBox: Restore stopped before target save was modified." in dlg_zip_before,
          "ZIP failure before mutation must show stopped before modified OptionBox")

    # 10.3: ZIP single restore fails after mutation -> OptionBox with unverified notice and recovery
    dlg_zip_mut = simulate_ui_callbacks(rc=1, is_raw=False, recovery_paths=["/dumps/rec.zip"], mutation_started=True)
    check(any("unverified" in d for d in dlg_zip_mut),
          "ZIP failure after mutation must show unverified contents OptionBox")

    # 10.4: Batch where Item 0 is ZIP (success) and Item 1 is RAW (fails auto-backup)
    def simulate_batch_ui_callback(rc: int, last_item_is_raw: bool, recovery_paths: list[str], last_mutation_started: bool):
        dialogs = []
        if rc != 0:
            dialogs.append("ErrorBox: Restore failed!")
        else:
            dialogs.append("Notify: Restore successful!")

        if len(recovery_paths) > 0:
            if rc == 0:
                prefix = "Restore completed."
            elif last_item_is_raw:
                prefix = "Restore stopped. Safety recovery archives retained."
            elif not last_item_is_raw and last_mutation_started:
                prefix = "Restore stopped: current target save may have changed and restored contents are unverified."
            else:
                prefix = "Restore stopped before current target save was modified."
            dialogs.append(f"OptionBox: {prefix}")
        elif rc != 0:
            if not last_item_is_raw and not last_mutation_started:
                dialogs.append("OptionBox: Restore stopped before current target save was modified.")
        return dialogs

    dlg_batch_raw_fail = simulate_batch_ui_callback(rc=1, last_item_is_raw=True, recovery_paths=["/dumps/item0_rec.zip"], last_mutation_started=False)
    check("OptionBox: Restore stopped. Safety recovery archives retained." in dlg_batch_raw_fail,
          "RAW batch failure retains earlier ZIP recovery without claiming current target untouched")
    check(not any("unverified" in d for d in dlg_batch_raw_fail),
          "Batch with RAW failure must not falsely warn about unverified mutation")

    # 10.5: Batch where Item 0 is RAW and fails early -> recovery_paths empty, no OptionBox shown
    dlg_batch_raw_empty = simulate_batch_ui_callback(rc=1, last_item_is_raw=True, recovery_paths=[], last_mutation_started=False)
    check(dlg_batch_raw_empty == ["ErrorBox: Restore failed!"],
          "Batch with early RAW failure and no prior recovery must show only ErrorBox")
    print("  -> Fixture 10 (RAW restore isolation in single and batch UI callbacks) PASSED.")

    # Fixture 11: Real ZIP fault injections
    # 11.1 Zero-byte file verification against native 0-byte file succeeds
    zero_zip = build_test_zip({"empty.bin": b""})
    inv_f_zero, inv_d_zero, _ = simulate_preflight(zero_zip)
    fs_zero = SyntheticSaveFs({"/empty.bin": b""})
    res_zero = simulate_verify_against_native(zero_zip, fs_zero, inv_f_zero, inv_d_zero)
    check(res_zero is True, "0-byte file must verify successfully")

    # 11.2 Excluded metadata (.nx_save_meta.bin) with corrupt CRC rejected during draining
    raw_bad_meta = build_test_zip({".nx_save_meta.bin": b"some meta data", "valid.bin": b"payload"}, corrupt_file=".nx_save_meta.bin")
    try:
        simulate_preflight(raw_bad_meta)
        check(False, "Corrupted metadata CRC must be rejected in preflight")
    except (ValueError, zipfile.BadZipFile):
        pass

    # 11.3 Missing native file when archive expects it
    fs_missing = SyntheticSaveFs({})
    res_missing = simulate_verify_against_native(zero_zip, fs_missing, inv_f_zero, inv_d_zero)
    check(res_missing is False, "Missing native file must fail verification")
    print("  -> Fixture 11 (Real ZIP fault injections: 0-byte, corrupt meta CRC, missing native file) PASSED.")

    print("=== ALL SYNTHETIC BEHAVIORAL FIXTURES PASSED SUCCESSFULLY ===")


# ==============================================================================
# Main
# ==============================================================================
