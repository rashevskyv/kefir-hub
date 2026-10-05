# Connected scenario tests for save backup publication contract.
import os
import zipfile
import tempfile
import shutil
import struct
from typing import List, Dict
from contract_fixtures.save_backup_publication_encoding import (
    SaveAttribute, SelectedEntry, encode_extra_data, decode_extra_data,
    encode_nx_save_meta, decode_nx_save_meta, get_actual_dbi_account_label,
    get_actual_dbi_space_label, NX_SAVE_META_MAGIC, NX_SAVE_META_VERSION,
    NX_SAVE_META_NAME, DBI_SAVE_INFO_NAME, DBI_SAVE_EXTRA_NAME,
    FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_TYPE_BCAT,
    FS_SAVE_DATA_TYPE_DEVICE, FS_SAVE_DATA_TYPE_TEMPORARY, FS_SAVE_DATA_TYPE_CACHE,
    FS_SAVE_DATA_TYPE_SYSTEM_BCAT, FS_SAVE_DATA_SPACE_ID_SYSTEM, FS_SAVE_DATA_SPACE_ID_USER,
    FS_SAVE_DATA_SPACE_ID_SD_SYSTEM, FS_SAVE_DATA_SPACE_ID_TEMPORARY,
    FS_SAVE_DATA_SPACE_ID_SD_USER, FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM
)
from contract_fixtures.save_backup_publication_simulator import (
    RES_OK, FS_ERROR_PATH_NOT_FOUND, FS_ERROR_PATH_ALREADY_EXISTS,
    FS_ERROR_NOT_MOUNTED, FS_ERROR_TARGET_LOCKED, FS_ERROR_TOO_LONG_PATH,
    FS_ERROR_INVALID_SIZE, RESULT_ZIP_WRITE_IN_FILE, RESULT_FS_UNKNOWN_STDIO_ERROR,
    RESULT_UNZ_OPEN2_64, SVC_ERROR_CANCELLED,
    ERR_IPC_READ_FAILED, ERR_CANNOT_RESTORE_TO_SYSTEM,
    ERR_PREFLIGHT_EMPTY, ERR_OVERFLOW, ERR_VERIFICATION_MISMATCH,
    ERR_PREFLIGHT_FAILED, ModeledRecoverySink, ConnectedBackupSimulator,
    assert_successful_ordered_subsequence
)

def check(condition: bool, msg: str) -> None:
    if not condition:
        raise AssertionError(msg)

def test_seven_save_types_connected_pipeline():
    print("[2] Running connected 7-save-type publication & independent wire readback tests...")

    test_cases = [
        # (name, type, space, uid_low, uid_high, sid, rank, idx, compressed, is_dbi, expect_account, expect_space)
        ("Account_Main", FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_SPACE_ID_USER, 0x1234567890ABCDEF, 0xFEDCBA0987654321, 0, 0, 0, False, True, "TestAccount", "User"),
        ("Account_Rank1_Idx3", FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_SPACE_ID_USER, 0x1234567890ABCDEF, 0xFEDCBA0987654321, 0, 1, 3, True, True, "TestAccount", "User"),
        ("Device", FS_SAVE_DATA_TYPE_DEVICE, FS_SAVE_DATA_SPACE_ID_USER, 0, 0, 0, 0, 0, True, True, "Device", "User"),
        ("Bcat", FS_SAVE_DATA_TYPE_BCAT, FS_SAVE_DATA_SPACE_ID_USER, 0, 0, 0, 0, 0, False, True, "BCAT", "User"),
        ("Cache_User", FS_SAVE_DATA_TYPE_CACHE, FS_SAVE_DATA_SPACE_ID_USER, 0x1111222233334444, 0x5555666677778888, 0, 0, 0, True, True, "Cache", "User"),
        ("Cache_SdUser", FS_SAVE_DATA_TYPE_CACHE, FS_SAVE_DATA_SPACE_ID_SD_USER, 0x1111222233334444, 0x5555666677778888, 0, 0, 1, False, True, "Cache", "SdUser"),
        ("Temporary", FS_SAVE_DATA_TYPE_TEMPORARY, FS_SAVE_DATA_SPACE_ID_TEMPORARY, 0, 0, 0, 0, 0, True, True, "Temporary", "Temporary"),
        ("System", FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_SPACE_ID_SYSTEM, 0, 0, 0x8000000000000010, 0, 0, False, False, "System", "System"),
        ("System_SdSystem", FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_SPACE_ID_SD_SYSTEM, 0, 0, 0x8000000000000020, 0, 0, True, False, "System", "System"),
        ("System_ProperSystem", FS_SAVE_DATA_TYPE_SYSTEM, FS_SAVE_DATA_SPACE_ID_PROPER_SYSTEM, 0, 0, 0x8000000000000030, 0, 0, False, False, "System", "System"),
        ("SystemBcat", FS_SAVE_DATA_TYPE_SYSTEM_BCAT, FS_SAVE_DATA_SPACE_ID_SYSTEM, 0, 0, 0x8000000000000040, 0, 0, True, False, "System BCAT", "System"),
    ]

    with tempfile.TemporaryDirectory() as td:
        root_dir = os.path.join(td, "sdcard")
        os.makedirs(root_dir, exist_ok=True)

        source_dir = os.path.join(td, "source_payload")
        os.makedirs(os.path.join(source_dir, "nested", "dir"), exist_ok=True)
        main_payload_bytes = b"MAIN_SAVE_PAYLOAD_BYTES_0123456789"
        nested_payload_bytes = b"NESTED_SLOT_DATA_PAYLOAD"
        with open(os.path.join(source_dir, "save.dat"), "wb") as f:
            f.write(main_payload_bytes)
        with open(os.path.join(source_dir, "nested", "dir", "slot.bin"), "wb") as f:
            f.write(nested_payload_bytes)
        with open(os.path.join(source_dir, "empty_file.bin"), "wb") as f:
            pass

        sim = ConnectedBackupSimulator(root_dir)

        for name, stype, space, ul, uh, sid, rank, idx, comp, expect_dbi, exp_account, exp_space in test_cases:
            app_id = 0x0100000000010000 if sid == 0 else 0
            attr = SaveAttribute(
                application_id=app_id,
                uid_low=ul,
                uid_high=uh,
                system_save_data_id=sid,
                save_data_type=stype,
                save_data_rank=rank,
                save_data_index=idx,
            )
            entry = SelectedEntry(
                save_data_id=0x99990000 + stype,
                save_data_space_id=space,
                attr=attr,
                name=f"Game_{name}",
                size=0x80000,
            )
            extra_bytes = encode_extra_data(
                attr,
                owner_id=0x0100000000001000 + stype,
                timestamp=1726000000 + stype,
                flags=0x12,
                unk_x54=0x34,
                data_size=0x200000,
                journal_size=0x200000,
                commit_id=100 + stype,
            )
            rel_zip = f"/backup/saves/{name}.zip"

            rc, final_path = sim.run_backup(entry, extra_bytes, source_dir, rel_zip, compressed=comp)
            check(rc == RES_OK, f"Backup failed for {name} with rc 0x{rc:X}")
            check(final_path is not None and os.path.exists(final_path), f"Final zip missing for {name}")
            check(sim.pub_state == "Published", f"Publication state must be Published for {name}")
            check(not os.path.exists(sim.stage_dir), f"Stage directory must be cleaned up for {name}")

            # Verify ordered subsequence via event indices
            assert_successful_ordered_subsequence(sim.events)

            # Reopen ZIP and verify actual independent wire encoding and payload
            with zipfile.ZipFile(final_path, "r") as zf:
                names = zf.namelist()
                # Check compression type
                expected_compress = zipfile.ZIP_DEFLATED if comp else zipfile.ZIP_STORED
                if expect_dbi:
                    info = zf.getinfo("/save.dat")
                    check(info.compress_type == expected_compress, f"{name}: compress_type mismatch")
                else:
                    info = zf.getinfo("save.dat")
                    check(info.compress_type == expected_compress, f"{name}: compress_type mismatch")

                if expect_dbi:
                    check(DBI_SAVE_INFO_NAME in names, f"{name}: missing {DBI_SAVE_INFO_NAME}")
                    check(DBI_SAVE_EXTRA_NAME in names, f"{name}: missing {DBI_SAVE_EXTRA_NAME}")

                    # Independent unpack of DBI extra512 at raw offsets
                    read_extra = zf.read(DBI_SAVE_EXTRA_NAME)
                    check(len(read_extra) == 512, f"{name}: DBI extra size must be 512")
                    u_app, u_ul, u_uh, u_sid, u_stype, u_rank, u_idx = struct.unpack("<QQQQBBH", read_extra[:36])
                    u_oid, u_ts, u_flags, u_unk, u_dsize, u_jsize, u_cid = struct.unpack("<QQIIqqQ", read_extra[64:112])
                    check(u_app == app_id, f"{name}: raw app mismatch")
                    check(u_ul == ul and u_uh == uh, f"{name}: raw uid mismatch")
                    check(u_sid == sid, f"{name}: raw sid mismatch")
                    check(u_stype == stype, f"{name}: raw stype mismatch")
                    check(u_rank == rank, f"{name}: raw rank mismatch")
                    check(u_idx == idx, f"{name}: raw index mismatch")
                    check(u_oid == 0x0100000000001000 + stype, f"{name}: raw owner_id mismatch")
                    check(u_ts == 1726000000 + stype, f"{name}: raw timestamp mismatch")
                    check(u_flags == 0x12, f"{name}: raw flags mismatch")
                    check(u_unk == 0x34, f"{name}: raw unk mismatch")
                    check(u_dsize == 0x200000, f"{name}: raw data_size mismatch")
                    check(u_jsize == 0x200000, f"{name}: raw journal_size mismatch")
                    check(u_cid == 100 + stype, f"{name}: raw commit_id mismatch")

                    # Verify exact DBI info fields matching actual writer
                    read_info = zf.read(DBI_SAVE_INFO_NAME).decode("utf-8")
                    check(f"TitleId={app_id:016X}" in read_info, f"{name}: DBI info TitleId mismatch")
                    check(f"Account={exp_account}" in read_info, f"{name}: DBI info Account mismatch, got: {read_info}")
                    check(f"Space={exp_space}" in read_info, f"{name}: DBI info Space mismatch, got: {read_info}")

                    # Check payload files with absolute slash
                    check("/save.dat" in names, f"{name}: missing /save.dat")
                    check("/nested/dir/slot.bin" in names, f"{name}: missing /nested/dir/slot.bin")
                    check("/empty_file.bin" in names, f"{name}: missing /empty_file.bin")
                    check(zf.read("/save.dat") == main_payload_bytes, f"{name}: main payload mismatch")
                    check(zf.read("/nested/dir/slot.bin") == nested_payload_bytes, f"{name}: nested payload mismatch")
                    check(zf.read("/empty_file.bin") == b"", f"{name}: empty file must be 0 bytes")
                else:
                    check(NX_SAVE_META_NAME in names, f"{name}: missing {NX_SAVE_META_NAME}")
                    read_meta = zf.read(NX_SAVE_META_NAME)
                    check(len(read_meta) == 128, f"{name}: NXSaveMeta size must be 128")

                    # Independent unpack of NX128 at raw offsets
                    m_magic, m_ver = struct.unpack("<II", read_meta[:8])
                    check(m_magic == NX_SAVE_META_MAGIC, f"{name}: raw magic mismatch")
                    check(m_ver == NX_SAVE_META_VERSION, f"{name}: raw version mismatch")
                    m_app, m_ul, m_uh, m_sid, m_stype, m_rank, m_idx = struct.unpack("<QQQQBBH", read_meta[8:44])
                    check(m_app == app_id, f"{name}: raw NX app mismatch")
                    check(m_ul == ul and m_uh == uh, f"{name}: raw NX uid mismatch")
                    check(m_sid == sid, f"{name}: raw NX sid mismatch")
                    check(m_stype == stype, f"{name}: raw NX type mismatch")
                    check(m_rank == rank, f"{name}: raw NX rank mismatch")
                    check(m_idx == idx, f"{name}: raw NX idx mismatch")
                    m_oid, m_ts, m_flags, m_unk, m_dsize, m_jsize, m_cid, m_raw = struct.unpack("<QQIIqqQQ", read_meta[72:128])
                    check(m_oid == 0x0100000000001000 + stype, f"{name}: raw NX owner_id mismatch")
                    check(m_ts == 1726000000 + stype, f"{name}: raw NX timestamp mismatch")
                    check(m_flags == 0x12, f"{name}: raw NX flags mismatch")
                    check(m_unk == 0x34, f"{name}: raw NX unk mismatch")
                    check(m_dsize == 0x200000, f"{name}: raw NX data_size mismatch")
                    check(m_jsize == 0x200000, f"{name}: raw NX journal_size mismatch")
                    check(m_cid == 100 + stype, f"{name}: raw NX cid mismatch")
                    check(m_raw == entry.size, f"{name}: raw NX raw_size mismatch")

                    # Check payload files without root slash
                    check("save.dat" in names, f"{name}: missing save.dat")
                    check("nested/dir/slot.bin" in names, f"{name}: missing nested/dir/slot.bin")
                    check("empty_file.bin" in names, f"{name}: missing empty_file.bin")
                    check(zf.read("save.dat") == main_payload_bytes, f"{name}: main payload mismatch")
                    check(zf.read("nested/dir/slot.bin") == nested_payload_bytes, f"{name}: nested payload mismatch")
                    check(zf.read("empty_file.bin") == b"", f"{name}: empty file must be 0 bytes")

    print("  -> Connected 7-save-type publication & independent wire readback tests PASSED.")


# ==============================================================================
# 4. Connected Failure & Mutation Matrix With Full Lifecycle Assertions
# ==============================================================================

def test_failure_matrix_connected():
    print("[3] Running connected failure matrix & event order assertions...")

    with tempfile.TemporaryDirectory() as td:
        root_dir = os.path.join(td, "sdcard")
        os.makedirs(root_dir, exist_ok=True)

        source_dir = os.path.join(td, "source_payload")
        os.makedirs(source_dir, exist_ok=True)
        sentinel_payload = b"SENTINEL_SOURCE_PAYLOAD_DO_NOT_TOUCH_012345"
        source_file = os.path.join(source_dir, "data.bin")
        with open(source_file, "wb") as f:
            f.write(sentinel_payload)

        attr = SaveAttribute(
            application_id=0x0100000000010000,
            uid_low=0x1234,
            uid_high=0x5678,
            system_save_data_id=0,
            save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT,
            save_data_rank=0,
            save_data_index=0,
        )
        entry = SelectedEntry(
            save_data_id=0x12345678,
            save_data_space_id=FS_SAVE_DATA_SPACE_ID_USER,
            attr=attr,
            name="MatrixGame",
        )
        extra_bytes = encode_extra_data(attr)

        def assert_source_sentinel_unchanged():
            with open(source_file, "rb") as f:
                check(f.read() == sentinel_payload, "ASSERTION FAILED: Source sentinel payload was mutated!")

        # 3.1 Zero save ID early exit
        sim = ConnectedBackupSimulator(root_dir)
        zero_entry = SelectedEntry(save_data_id=0, save_data_space_id=1, attr=attr)
        rc, p = sim.run_backup(zero_entry, extra_bytes, source_dir, "/backup/zero.zip")
        check(rc == RES_OK and p is None, "Zero ID must return RES_OK with no archive")
        check("zero_id_early_return" in sim.events, "Must log zero_id_early_return")
        check("sd_open_check" not in sim.events, "Must not open SD on zero save ID")
        assert_source_sentinel_unchanged()

        # 3.2 Empty collections early return
        empty_src_dir = os.path.join(td, "empty_source")
        os.makedirs(empty_src_dir, exist_ok=True)
        sim = ConnectedBackupSimulator(root_dir)
        rc, p = sim.run_backup(entry, extra_bytes, empty_src_dir, "/backup/empty.zip")
        check(rc == RES_OK and p is None, "Empty source collections must exit 0x0 without archive")
        check("empty_collections_early_return" in sim.events, "Must log empty_collections_early_return")
        check("sd_open_check" not in sim.events, "Must not open SD on empty collections")

        # 3.3 SD native open failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_sd_open_error = 0x22202
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/sd_open_fail.zip")
        check(rc == 0x22202, "SD open failure must propagate")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check("pre_probe_final" not in sim.events, "Must not probe final after SD open failure")
        check("reserve_stage_dir" not in sim.events, "Must not reserve stage dir after SD open failure")
        assert_source_sentinel_unchanged()

        # 3.4 Pre-work final probe IO error (propagated)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_pre_probe_error = 0x33302
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/pre_probe_err.zip")
        check(rc == 0x33302, "Pre-probe IO error must propagate")
        check("create_parent_dirs" not in sim.events, "Must not create parent dirs after pre-probe error")
        assert_source_sentinel_unchanged()

        # 3.5 Real valid ZIP collision BEFORE work
        sim = ConnectedBackupSimulator(root_dir)
        coll_final_path = os.path.join(root_dir, "backup", "coll_pre.zip")
        os.makedirs(os.path.dirname(coll_final_path), exist_ok=True)
        prior_zip_sentinel = b"VALID_EXISTING_ZIP_PAYLOAD_PROTECTED"
        with zipfile.ZipFile(coll_final_path, "w") as zf:
            zf.writestr("prior_save.dat", prior_zip_sentinel)
        with open(coll_final_path, "rb") as f:
            captured_prior_bytes = f.read()

        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/coll_pre.zip")
        check(rc == FS_ERROR_PATH_ALREADY_EXISTS, "Pre-work collision must return FsError_PathAlreadyExists")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check("reserve_stage_dir" not in sim.events, "Must not reserve stage on pre-work collision")
        with open(coll_final_path, "rb") as f:
            check(f.read() == captured_prior_bytes, "ASSERTION FAILED: Pre-existing ZIP bytes mutated!")
        with zipfile.ZipFile(coll_final_path, "r") as zf:
            check(zf.read("prior_save.dat") == prior_zip_sentinel, "ASSERTION FAILED: Pre-existing ZIP payload corrupt!")
        assert_source_sentinel_unchanged()

        # 3.6 Parent directory creation failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_parent_create_error = 0x44402
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/parent_err.zip")
        check(rc == 0x44402, "Parent create error must propagate")
        check("cancellation_gate_1" not in sim.events, "Must not reach gate 1 after parent create error")
        assert_source_sentinel_unchanged()

        # 3.7 Cancellation at Gate 1 (before reservation)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_cancel_at_gate = 1
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cancel_g1.zip")
        check(rc == SVC_ERROR_CANCELLED, "Must return SVC_ERROR_CANCELLED at gate 1")
        check(not os.path.exists(sim.stage_dir), "Stage dir must not exist on cancellation at gate 1")
        check("reserve_stage_dir" not in sim.events, "Must not reserve stage dir after gate 1 cancel")
        assert_source_sentinel_unchanged()

        # 3.8 Stage path length bounds overflow (FsError_TooLongPath)
        sim = ConnectedBackupSimulator(root_dir)
        long_name = "x" * 800
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, f"/backup/{long_name}.zip")
        check(rc == FS_ERROR_TOO_LONG_PATH, "Long path must return FsError_TooLongPath")
        check("reserve_stage_dir" not in sim.events, "Must not reserve stage on length overflow")

        # 3.9 Sibling stage collision (foreign .stage already exists with real sentinel)
        sim = ConnectedBackupSimulator(root_dir)
        foreign_stage_path = os.path.join(root_dir, "backup", "stage_coll.zip.stage")
        os.makedirs(foreign_stage_path, exist_ok=True)
        foreign_sentinel = os.path.join(foreign_stage_path, "foreign_lock.txt")
        foreign_sentinel_bytes = b"FOREIGN_STAGE_SENTINEL_DO_NOT_DELETE_0123"
        with open(foreign_sentinel, "wb") as f:
            f.write(foreign_sentinel_bytes)

        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/stage_coll.zip")
        check(rc == FS_ERROR_PATH_ALREADY_EXISTS, "Stage collision must return FsError_PathAlreadyExists")
        check(sim.owned_stage_created is False, "owned_stage_created must be false on stage collision")
        check(os.path.exists(foreign_sentinel), "ASSERTION FAILED: Foreign stage sentinel was deleted!")
        with open(foreign_sentinel, "rb") as f:
            check(f.read() == foreign_sentinel_bytes, "Foreign stage sentinel was corrupted!")
        assert_source_sentinel_unchanged()

        # 3.10 Native stage primitive reservation failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_stage_primitive_error = 0x55502
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/stage_prim_fail.zip")
        check(rc == 0x55502, "Stage primitive failure must propagate")
        check(sim.owned_stage_created is False, "owned_stage_created must be false on primitive failure")
        check("stage_dir_commit" not in sim.events, "Must not commit stage dir on primitive failure")
        check("write_save_backup_zip_start" not in sim.events, "Must not start writer on stage failure")

        # 3.11 Stage commit failure after primitive success
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_stage_commit_error = 0x66602
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/stage_commit_fail.zip")
        check(rc == 0x66602, "Stage commit error must propagate")
        check("ownership_set_owned_stage_true" in sim.events, "Ownership must be set true before commit")
        check(not os.path.exists(sim.stage_dir), "Owned stage dir must be deleted on stage commit failure")
        check("write_save_backup_zip_start" not in sim.events, "Must not start writer on stage commit failure")

        # 3.12 Cancellation at Gate 2 (before write)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_cancel_at_gate = 2
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cancel_g2.zip")
        check(rc == SVC_ERROR_CANCELLED, "Must return SVC_ERROR_CANCELLED at gate 2")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on cancellation at gate 2")
        check("write_save_backup_zip_start" not in sim.events, "Must not write on gate 2 cancel")

        # 3.13 Source read error during write
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_source_read_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/src_read_err.zip")
        check(rc == RESULT_ZIP_WRITE_IN_FILE, "Must return write error on source read failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on write failure")
        check("primitive_rename" not in sim.events, "Must not rename on write failure")

        # 3.14 Short write in modeled sink
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_short_write = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/short_write.zip")
        check(rc == RESULT_ZIP_WRITE_IN_FILE, "Must return write error on short write")
        check("sink_write_failed_detected" in sim.events, "Must detect sink write failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on short write")
        check("primitive_rename" not in sim.events, "Must not rename on short write")

        # 3.15 ZIP entry close failure hooked at entry close boundary
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_zip_entry_close_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/entry_close_err.zip")
        check(rc == RESULT_ZIP_WRITE_IN_FILE, "Must return write error on zip entry close failure")
        check("writer_fsdev_commit_device_sdmc" not in sim.events, "Must not reach writer commits on entry close failure")
        check("primitive_rename" not in sim.events, "Must not rename on entry close failure")
        check("backup_success" not in sim.events, "Must not succeed on entry close failure")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on entry close failure")

        # 3.16 ZIP final archive close failure hooked at actual zf.close()
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_zip_final_close_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/final_close_err.zip")
        check(rc == RESULT_ZIP_WRITE_IN_FILE, "Must return write error on zip final close failure")
        check("writer_fsdev_commit_device_sdmc" not in sim.events, "Must not reach writer commits on final close failure")
        check("primitive_rename" not in sim.events, "Must not rename on final close failure")
        check("backup_success" not in sim.events, "Must not succeed on final close failure")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on final close failure")

        # 3.17 fflush failure in modeled sink
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_fflush_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/fflush_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on fflush failure")
        check("sink_flush_failed_detected" in sim.events, "Must detect sink flush failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on fflush failure")

        # 3.18 invalid fd (-1) in modeled sink
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_invalid_fd = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/inv_fd_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on invalid fd")
        check("sink_sync_failed_detected" in sim.events, "Must detect sink sync failure")

        # 3.19 Real os.fsync failure via narrow fault hook throwing OSError
        sim = ConnectedBackupSimulator(root_dir)
        def fault_hook_close_os_fd(fd):
            # Close underlying OS file descriptor so actual os.fsync(fd) call raises OSError(EBADF)
            os.close(fd)
        sim.fsync_fault_hook = fault_hook_close_os_fd
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/fsync_hook_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on fsync failure")
        check("sink_sync_failed_detected" in sim.events, "Must detect sink sync failure")
        check("primitive_rename" not in sim.events, "Must not rename on fsync failure")
        check("backup_success" not in sim.events, "Must not succeed on fsync failure")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check(p is None or not os.path.exists(sim.final_path), "Must not have published final path")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on fsync failure")

        # 3.19b Real os.fsync failure via inject_fsync_error flag throwing OSError
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_fsync_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/fsync_flag_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on fsync flag failure")
        check("sink_sync_failed_detected" in sim.events, "Must detect sink sync failure")
        check("primitive_rename" not in sim.events, "Must not rename on fsync flag failure")
        check("backup_success" not in sim.events, "Must not succeed on fsync flag failure")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished")
        check(p is None or not os.path.exists(sim.final_path), "Must not have published final path")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on fsync flag failure")

        # 3.20 fclose failure in modeled sink
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_fclose_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/fclose_err.zip")
        check(rc == RESULT_FS_UNKNOWN_STDIO_ERROR, "Must return stdio error on fclose failure")
        check("sink_close_failed_detected" in sim.events, "Must detect sink close failure")

        # 3.21 Writer fsdevCommitDevice("sdmc") failure BEFORE rename
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_writer_sdmc_commit_error = 0x77702
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/w_sdmc_fail.zip")
        check(rc == 0x77702, "Writer sdmc commit error must propagate")
        check("primitive_rename" not in sim.events, "Must not rename on writer sdmc commit failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on writer commit failure")

        # 3.22 Writer native SD Commit failure BEFORE rename
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_writer_sd_commit_error = 0x77802
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/w_sd_fail.zip")
        check(rc == 0x77802, "Writer native commit error must propagate")
        check("primitive_rename" not in sim.events, "Must not rename on writer native commit failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on writer native commit failure")

        # 3.23 Cancellation at Gate 3 (before rename)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_cancel_at_gate = 3
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cancel_g3.zip")
        check(rc == SVC_ERROR_CANCELLED, "Must return SVC_ERROR_CANCELLED at gate 3")
        check("primitive_rename" not in sim.events, "Must not rename on gate 3 cancel")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on cancellation at gate 3")
        check(not os.path.exists(sim.final_path), "No final archive on cancellation at gate 3")

        # 3.24 Real concurrent collision before rename: operation hook creates valid ZIP
        sim = ConnectedBackupSimulator(root_dir)
        foreign_final_sentinel = b"CONCURRENT_EXTERNAL_ZIP_SENTINEL_PAYLOAD"
        foreign_final_file = os.path.join(root_dir, "backup", "coll_prerename.zip")

        captured_foreign_bytes: Optional[bytes] = None
        def concurrent_writer_hook():
            nonlocal captured_foreign_bytes
            with zipfile.ZipFile(foreign_final_file, "w") as zf:
                zf.writestr("concurrent.dat", foreign_final_sentinel)
            with open(foreign_final_file, "rb") as f:
                captured_foreign_bytes = f.read()

        sim.pre_rename_hook = concurrent_writer_hook
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/coll_prerename.zip")
        check(rc == FS_ERROR_PATH_ALREADY_EXISTS, "Pre-rename collision must return PathAlreadyExists")
        check("primitive_rename" not in sim.events, "Must not execute rename on pre-rename collision")
        check(not os.path.exists(sim.stage_dir), "Owned stage dir must be cleaned on pre-rename collision")
        # Verify exact byte comparison after refusal
        check(captured_foreign_bytes is not None and len(captured_foreign_bytes) > 0, "Captured foreign bytes must be non-empty")
        with open(foreign_final_file, "rb") as f:
            check(f.read() == captured_foreign_bytes, "ASSERTION FAILED: Foreign concurrent ZIP bytes were mutated after refusal!")
        # Verify real foreign ZIP is intact and can be reopened
        with zipfile.ZipFile(foreign_final_file, "r") as zf:
            check(zf.read("concurrent.dat") == foreign_final_sentinel, "Foreign concurrent ZIP was corrupted!")

        # 3.25 Pre-rename final probe IO error (propagated)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_post_probe_error = 0x88802
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/post_probe_err.zip")
        check(rc == 0x88802, "Post-probe error must propagate")
        check("primitive_rename" not in sim.events, "Must not rename on post-probe error")
        check(not os.path.exists(sim.stage_dir), "Owned stage dir must be cleaned on post-probe error")

        # 3.26 Native rename primitive failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_rename_error = 0x99902
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/rename_fail.zip")
        check(rc == 0x99902, "Rename failure must propagate")
        check(sim.pub_state == "Unpublished", "Pub state must remain Unpublished if rename failed")
        check("ownership_set_renamed" not in sim.events, "Must not transition to Renamed on rename failure")
        check(not os.path.exists(sim.final_path), "No final path created if rename failed")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be cleaned on rename failure")

        # 3.27 Post-rename sdmc commit failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_post_rename_sdmc_commit_error = 0xAAA02
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/pr_sdmc_fail.zip")
        check(rc == 0xAAA02, "Post-rename sdmc commit error must propagate")
        check("ownership_set_renamed" in sim.events, "Must reach Renamed state")
        check("ownership_set_published" not in sim.events, "Must not reach Published on commit failure")
        check(not os.path.exists(sim.final_path), "Owned final must be deleted on post-rename commit failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be deleted")

        # 3.28 Post-rename native SD commit failure
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_post_rename_sd_commit_error = 0xBBB02
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/pr_sd_fail.zip")
        check(rc == 0xBBB02, "Post-rename native SD commit error must propagate")
        check("ownership_set_renamed" in sim.events, "Must reach Renamed state")
        check("ownership_set_published" not in sim.events, "Must not reach Published on commit failure")
        check(not os.path.exists(sim.final_path), "Owned final must be deleted on post-rename commit failure")
        check(not os.path.exists(sim.stage_dir), "Stage dir must be deleted")

        # 3.29 Cleanup directory failure (honest retained artifact, error returned)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_stage_commit_error = 0x66602
        sim.inject_cleanup_delete_dir_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cleanup_dir_fail.zip")
        check(rc == 0x66602, "Must return stage commit error")
        check("injected_cleanup_dir_fail" in sim.events, "Must record directory cleanup failure")
        check(os.path.exists(sim.stage_dir), "Honest retained stage dir remains when cleanup fails")

        # 3.30 Cleanup file failure after post-rename failure (honest retained final artifact)
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_post_rename_sdmc_commit_error = 0xAAA02
        sim.inject_cleanup_delete_file_error = True
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cleanup_file_fail.zip")
        check(rc == 0xAAA02, "Must return commit failure even when cleanup fails")
        check(os.path.exists(sim.final_path), "Honest retained artifact remains when file cleanup fails")
        check("injected_cleanup_file_fail" in sim.events, "Must record file cleanup failure")

        # 3.31 Cancellation after committed Published must NOT fail
        sim = ConnectedBackupSimulator(root_dir)
        sim.inject_cancel_at_gate = 4
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/cancel_g4_success.zip")
        check(rc == RES_OK, "Cancellation after committed Published must return RES_OK")
        check(p is not None and os.path.exists(p), "Published archive must exist")
        check(sim.pub_state == "Published", "Pub state must be Published")
        check("ignored_cancellation_post_publication" in sim.events, "Must record ignored post-pub cancellation")

        # 3.32 Dedicated successful publication lifecycle ordered subsequence verification
        sim = ConnectedBackupSimulator(root_dir)
        rc, p = sim.run_backup(entry, extra_bytes, source_dir, "/backup/matrix_success.zip")
        check(rc == RES_OK, "Must succeed on clean run")
        check(p is not None and os.path.exists(p), "Published archive must exist")
        check(sim.pub_state == "Published", "Pub state must be Published")
        assert_successful_ordered_subsequence(sim.events)

        # Final assertion: source sentinel was NEVER mutated
        assert_source_sentinel_unchanged()

    print("  -> Connected failure matrix & event order assertions PASSED.")
