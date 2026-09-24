# Deletion models and Test Suite 9 for save backup library contract.
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
    inspect_backup_archive_model, check, make_zip
)
from contract_fixtures.save_backup_library_scanner import (
    read_backup_entries_model, collect_group_archives_model,
    collect_group_archives_legacy_model, FsSaveDataExtraDataModel,
    create_backup_if_newer_model, disambiguate_picker_labels
)

# ==============================================================================
# Suite 9: Connected Save & Backup Deletion Fail-Closed & Exact-ID Closure Contract
# ==============================================================================

class MockProgressBox:
    def __init__(self, exit_at_step: int = -1, exit_rc: int = 0x01EC01):  # SvcError_Cancelled
        self.step = 0
        self.exit_at_step = exit_at_step
        self.exit_rc = exit_rc
        self.cancelled = False

    def should_exit_result(self) -> int:
        if self.exit_at_step >= 0 and self.step >= self.exit_at_step:
            self.cancelled = True
            return self.exit_rc
        self.step += 1
        return 0


class MockLiveSaveFsOps:
    def __init__(self, live_registry=None, injected_space_failures=None):
        # registry: (space_id, save_data_id) -> metadata dict
        self.live_registry = dict(live_registry or {})
        self.injected_space_failures = dict(injected_space_failures or {})
        self.space_id_calls: List[Tuple[int, int]] = []
        self.attr_calls: List[Tuple[int, dict]] = []

    def delete_by_space_id(self, space_id: int, save_data_id: int) -> int:
        self.space_id_calls.append((space_id, save_data_id))
        key = (space_id, save_data_id)
        if key in self.injected_space_failures:
            return self.injected_space_failures[key]
        if key in self.live_registry:
            del self.live_registry[key]
            return 0
        return 0x202  # FsError_PathNotFound

    def delete_by_attribute(self, space_id: int, attr: dict) -> int:
        self.attr_calls.append((space_id, dict(attr)))
        for (sp, sid), info in list(self.live_registry.items()):
            if (sp == space_id and
                info.get("application_id") == attr.get("application_id") and
                info.get("uid") == attr.get("uid") and
                info.get("save_data_type") == attr.get("save_data_type")):
                del self.live_registry[(sp, sid)]
                return 0
        return 0x202


class MockSdFs:
    def __init__(self, existing_files=None, existing_dirs=None, injected_file_failures=None):
        self.files = set(existing_files or [])
        self.dirs = set(existing_dirs or [])
        self.injected_file_failures = dict(injected_file_failures or {})
        self.delete_file_calls: List[str] = []
        self.delete_dir_calls: List[str] = []

    def delete_file(self, path: str) -> int:
        self.delete_file_calls.append(path)
        if path in self.injected_file_failures:
            return self.injected_file_failures[path]
        if path in self.files:
            self.files.remove(path)
            return 0
        return 0x202

    def delete_directory(self, path: str) -> int:
        self.delete_dir_calls.append(path)
        p = path.rstrip('/') + '/'
        has_files = any(f.startswith(p) for f in self.files)
        if not has_files and path in self.dirs:
            self.dirs.remove(path)
            return 0
        return 0x202


# Post-fix model functions (exact mirrors of save_menu_ops.cpp)
def delete_live_save_entry_model(entry: EntryModel, fs_ops: MockLiveSaveFsOps) -> int:
    if entry.save_data_id == 0:
        return 0x20345  # MAKERESULT(Module_Libnx, LibnxError_BadInput)
    return fs_ops.delete_by_space_id(entry.save_data_space_id, entry.save_data_id)


def delete_saves_on_model(pbox: MockProgressBox, entries: List[EntryModel], fs_ops: MockLiveSaveFsOps) -> int:
    for e in entries:
        rc = pbox.should_exit_result()
        if rc != 0:
            return rc
        rc = delete_live_save_entry_model(e, fs_ops)
        if rc != 0:
            return rc
    return 0


def users_profile_run_delete_model(pbox: MockProgressBox, all_saves: List[EntryModel], fs_ops: MockLiveSaveFsOps) -> Tuple[int, str]:
    rc = delete_saves_on_model(pbox, all_saves, fs_ops)
    if rc != 0:
        return rc, "Could not delete the user."
    return 0, "User deleted."


def delete_saves_model(entries: List[EntryModel], pbox: MockProgressBox, sd_fs: MockSdFs, fs_ops: MockLiveSaveFsOps, backup_collector=None) -> Tuple[int, int, int, str]:
    if not entries:
        return 0, 0, 0, "NOOP"

    deleted_count = 0
    failed_count = 0

    for e in entries:
        rc_exit = pbox.should_exit_result()
        if rc_exit != 0:
            return rc_exit, deleted_count, failed_count, "ERROR_BOX:Delete failed!"

        if e.is_backup:
            backups = backup_collector(e) if backup_collector else [c.path for c in e.backup_members]
            for b_path in backups:
                rc_exit = pbox.should_exit_result()
                if rc_exit != 0:
                    return rc_exit, deleted_count, failed_count, "ERROR_BOX:Delete failed!"
                rc = sd_fs.delete_file(b_path)
                if rc != 0:
                    failed_count += 1
                    return rc, deleted_count, failed_count, f"ERROR_BOX:Delete failed! (0x{rc:X})"
                deleted_count += 1

            # Empty directory cleanup (best effort, outcome ignored; only reached if all backups succeeded)
            sd_fs.delete_directory(f"/dumps/{e.name}")
        else:
            rc = delete_live_save_entry_model(e, fs_ops)
            if rc != 0:
                failed_count += 1
                return rc, deleted_count, failed_count, f"ERROR_BOX:Delete failed! (0x{rc:X})"
            deleted_count += 1

    return 0, deleted_count, failed_count, "NOTIFY:Delete successful!"


# Pre-fix model functions (demonstrating the proven defect)
def delete_saves_on_old_model(pbox: MockProgressBox, entries: List[EntryModel], fs_ops: MockLiveSaveFsOps) -> int:
    for e in entries:
        rc_exit = pbox.should_exit_result()
        if rc_exit != 0:
            return rc_exit
        space_id = e.save_data_space_id
        rc = 0
        if e.save_data_id != 0:
            rc = fs_ops.delete_by_space_id(space_id, e.save_data_id)
        if e.save_data_id == 0 or rc != 0:
            attr = {"application_id": e.application_id, "uid": (e.uid_low, e.uid_high), "save_data_type": e.save_data_type}
            rc = fs_ops.delete_by_attribute(space_id, attr)
        # BUG: (void)rc; discards error!
    return 0


def users_profile_run_delete_old_model(pbox: MockProgressBox, all_saves: List[EntryModel], fs_ops: MockLiveSaveFsOps) -> Tuple[int, str]:
    rc = delete_saves_on_old_model(pbox, all_saves, fs_ops)
    if rc != 0:
        return rc, "Could not delete the user."
    return 0, "User deleted."


def delete_saves_old_model(entries: List[EntryModel], pbox: MockProgressBox, sd_fs: MockSdFs, fs_ops: MockLiveSaveFsOps, backup_collector=None) -> Tuple[int, int, int, str]:
    if not entries:
        return 0, 0, 0, "NOOP"

    deleted_count = 0
    failed_count = 0

    for e in entries:
        rc_exit = pbox.should_exit_result()
        if rc_exit != 0:
            return rc_exit, deleted_count, failed_count, "ERROR_BOX:Delete failed!"

        if e.is_backup:
            backups = backup_collector(e) if backup_collector else [c.path for c in e.backup_members]
            for b_path in backups:
                sd_fs.delete_file(b_path)
            # BUG: Unconditional increment!
            deleted_count += 1
        else:
            space_id = e.save_data_space_id
            rc = 0
            if e.save_data_id != 0:
                rc = fs_ops.delete_by_space_id(space_id, e.save_data_id)
            if e.save_data_id == 0 or rc != 0:
                attr = {"application_id": e.application_id, "uid": (e.uid_low, e.uid_high), "save_data_type": e.save_data_type}
                rc2 = fs_ops.delete_by_attribute(space_id, attr)
                if rc2 == 0:
                    rc = 0
            if rc == 0:
                deleted_count += 1
            else:
                failed_count += 1

    rc_worker = 0  # BUG: always succeeded from worker lambda!
    if rc_worker != 0:
        msg = "ERROR_BOX:Delete failed!"
    elif failed_count > 0 and deleted_count == 0:
        msg = "OPTION_BOX:Failed to delete save data."
    else:
        # BUG: Partial batch emits Delete successful!
        msg = "NOTIFY:Delete successful!"

    return rc_worker, deleted_count, failed_count, msg


def test_suite_9_connected_save_and_backup_deletion_fail_closed() -> None:
    print("[10] Running Suite 9: Connected Save & Backup Deletion Fail-Closed & Exact-ID Contract...")

    # --------------------------------------------------------------------------
    # 1. Exact Live Save Deletion: attempted once with retained space and save ID
    # --------------------------------------------------------------------------
    reg_1 = {(0, 0x1001): {"application_id": 0x0100000000010001, "uid": (1, 1), "save_data_type": SAVE_TYPE_ACCOUNT}}
    fs_1 = MockLiveSaveFsOps(live_registry=reg_1)
    e_1 = EntryModel(application_id=0x0100000000010001, save_data_space_id=0, save_data_id=0x1001, is_backup=False)

    rc_1, del_1, fail_1, msg_1 = delete_saves_model([e_1], MockProgressBox(), MockSdFs(), fs_1)
    check(rc_1 == 0 and del_1 == 1 and fail_1 == 0, "1.1: Exact live deletion must succeed")
    check(msg_1 == "NOTIFY:Delete successful!", "1.2: Successful live deletion must notify success")
    check(fs_1.space_id_calls == [(0, 0x1001)], f"1.3: Expected single call to space ID (0, 0x1001), got {fs_1.space_id_calls}")
    check(len(fs_1.attr_calls) == 0, "1.4: Attribute deletion must never be called on success")
    check((0, 0x1001) not in fs_1.live_registry, "1.5: Save 0x1001 must be deleted from registry")

    # --------------------------------------------------------------------------
    # 2. Unsafe Attribute Fallback Removed: Tempting Alternate Target Unused
    # --------------------------------------------------------------------------
    # Live registry contains current save 0x9999 matching the attributes, while entry retains stale ID 0x1002
    attr_match = {"application_id": 0x0100000000010002, "uid": (2, 2), "save_data_type": SAVE_TYPE_ACCOUNT}
    reg_2 = {
        (0, 0x9999): dict(attr_match),  # Current live save matching attributes (tempting alternate target!)
    }
    fs_2_old = MockLiveSaveFsOps(live_registry=reg_2)
    e_2 = EntryModel(application_id=0x0100000000010002, uid_low=2, uid_high=2, save_data_type=SAVE_TYPE_ACCOUNT,
                     save_data_space_id=0, save_data_id=0x1002, is_backup=False)

    # Pre-fix defect reproduction: exact ID 0x1002 fails (not found), fallback deletes tempting alternate target 0x9999!
    _, _, _, _ = delete_saves_old_model([e_2], MockProgressBox(), MockSdFs(), fs_2_old)
    check((0, 0x9999) not in fs_2_old.live_registry,
          "2.1 Defect reproduction: old implementation must delete tempting alternate target by attribute fallback")
    check(len(fs_2_old.attr_calls) == 1, "2.2 Defect reproduction: old implementation called delete_by_attribute")

    # Post-fix: exact ID failure fails closed, alternate target is preserved, attribute deletion is NEVER called!
    fs_2_new = MockLiveSaveFsOps(live_registry=reg_2)
    rc_2, del_2, fail_2, msg_2 = delete_saves_model([e_2], MockProgressBox(), MockSdFs(), fs_2_new)
    check(rc_2 == 0x202, f"2.3: Post-fix must return exact failure 0x202, got 0x{rc_2:X}")
    check(fail_2 == 1 and del_2 == 0, "2.4: Post-fix must count 1 failed and 0 deleted")
    check(msg_2.startswith("ERROR_BOX:Delete failed!"), f"2.5: Must surface ErrorBox on failure, got {msg_2}")
    check((0, 0x9999) in fs_2_new.live_registry,
          "2.6 Policy requirement: tempting alternate target 0x9999 MUST remain preserved and untouched in registry")
    check(len(fs_2_new.attr_calls) == 0, "2.7 Policy requirement: attribute deletion must strictly NEVER be called")

    # --------------------------------------------------------------------------
    # 3. Zero Save ID Rejection: Fails before any deletion call
    # --------------------------------------------------------------------------
    fs_3 = MockLiveSaveFsOps()
    e_3_zero = EntryModel(application_id=0x0100000000010003, save_data_space_id=0, save_data_id=0, is_backup=False)
    rc_3, del_3, fail_3, msg_3 = delete_saves_model([e_3_zero], MockProgressBox(), MockSdFs(), fs_3)
    check(rc_3 != 0, "3.1: Zero save ID must fail with error code")
    check(del_3 == 0 and fail_3 == 1, "3.2: Zero save ID must count as failed")
    check(len(fs_3.space_id_calls) == 0, "3.3: Zero save ID must NOT call space ID deletion")
    check(len(fs_3.attr_calls) == 0, "3.4: Zero save ID must NOT call attribute deletion")
    check(msg_3.startswith("ERROR_BOX:Delete failed!"), "3.5: Zero save ID must surface ErrorBox")

    # --------------------------------------------------------------------------
    # 4. DeleteSavesOn Failure Propagation to users_profile.cpp
    # --------------------------------------------------------------------------
    e_4_save = EntryModel(application_id=0x0100000000010004, save_data_space_id=0, save_data_id=0x1004, is_backup=False)

    # Pre-fix defect reproduction: DeleteSavesOn discards error and reports success!
    fs_4_old = MockLiveSaveFsOps(injected_space_failures={(0, 0x1004): 0x202})
    rc_up_old, msg_up_old = users_profile_run_delete_old_model(MockProgressBox(), [e_4_save], fs_4_old)
    check(rc_up_old == 0 and msg_up_old == "User deleted.",
          "4.1 Defect reproduction: old DeleteSavesOn discarded error and reported 'User deleted.'")

    # Post-fix: failure propagates and surfaces "Could not delete the user."
    fs_4_new = MockLiveSaveFsOps(injected_space_failures={(0, 0x1004): 0x202})
    rc_up_new, msg_up_new = users_profile_run_delete_model(MockProgressBox(), [e_4_save], fs_4_new)
    check(rc_up_new == 0x202, f"4.2: Post-fix DeleteSavesOn must propagate failure 0x202, got 0x{rc_up_new:X}")
    check(msg_up_new == "Could not delete the user.",
          f"4.3: users_profile must report 'Could not delete the user.', got '{msg_up_new}'")

    # Successful user profile deletion path remains successful:
    fs_4_ok = MockLiveSaveFsOps(live_registry={(0, 0x1004): {}})
    rc_up_ok, msg_up_ok = users_profile_run_delete_model(MockProgressBox(), [e_4_save], fs_4_ok)
    check(rc_up_ok == 0 and msg_up_ok == "User deleted.",
          "4.4: Successful profile deletion must report 'User deleted.'")

    # --------------------------------------------------------------------------
    # 5. DeleteSaves Failure Propagation: Cannot reach "Delete successful!"
    # --------------------------------------------------------------------------
    fs_5 = MockLiveSaveFsOps(injected_space_failures={(0, 0x1005): 0x402})
    e_5 = EntryModel(application_id=0x0100000000010005, save_data_space_id=0, save_data_id=0x1005, is_backup=False)
    rc_5, del_5, fail_5, msg_5 = delete_saves_model([e_5], MockProgressBox(), MockSdFs(), fs_5)
    check(rc_5 == 0x402, f"5.1: Failure must be returned, got 0x{rc_5:X}")
    check(msg_5 != "NOTIFY:Delete successful!", "5.2: Deletion failure must NEVER reach 'Delete successful!'")
    check(msg_5.startswith("ERROR_BOX:Delete failed!"), f"5.3: Failure must surface ErrorBox, got '{msg_5}'")

    # --------------------------------------------------------------------------
    # 6. Backup Archive Deletion Accounting & Surfacing
    # --------------------------------------------------------------------------
    cand_6a = BackupCandidateModel(ts=100, path="/dumps/Game6/20260918_100000.zip", source=1)
    cand_6b = BackupCandidateModel(ts=200, path="/dumps/Game6/20260918_110000.zip", source=1)
    e_6_bkp = EntryModel(name="Game6", application_id=0x0100000000010006, is_backup=True,
                         backup_members=[cand_6a, cand_6b])

    # Pre-fix defect reproduction: DeleteFile failure ignored, deleted_count incremented, success emitted!
    sd_6_old = MockSdFs(existing_files=[cand_6a.path, cand_6b.path],
                        injected_file_failures={cand_6b.path: 0x339402})  # FsError_FileNotFound
    _, del_6_old, _, msg_6_old = delete_saves_old_model([e_6_bkp], MockProgressBox(), sd_6_old, MockLiveSaveFsOps())
    check(del_6_old == 1 and msg_6_old == "NOTIFY:Delete successful!",
          "6.1 Defect reproduction: old backup delete emitted 'Delete successful!' despite archive failure")

    # Post-fix: exact file failure is retained, failed archive NOT counted, ErrorBox surfaced!
    sd_6_new = MockSdFs(existing_files=[cand_6a.path, cand_6b.path], existing_dirs=["/dumps/Game6"],
                        injected_file_failures={cand_6b.path: 0x339402})
    rc_6, del_6, fail_6, msg_6 = delete_saves_model([e_6_bkp], MockProgressBox(), sd_6_new, MockLiveSaveFsOps())
    check(del_6 == 1, f"6.2: Exactly 1 archive deleted successfully, got {del_6}")
    check(fail_6 == 1, f"6.3: Failed archive must be counted in failed_count, got {fail_6}")
    check(rc_6 == 0x339402, f"6.4: Exact file error must be returned, got 0x{rc_6:X}")
    check(msg_6.startswith("ERROR_BOX:Delete failed!"), f"6.5: Must surface ErrorBox, got '{msg_6}'")
    check(msg_6 != "NOTIFY:Delete successful!", "6.6: Must never emit success when an archive deletion failed")
    check(cand_6a.path not in sd_6_new.files, "6.7: Succeeded archive must be deleted from filesystem")
    check(cand_6b.path in sd_6_new.files, "6.8: Failed archive must remain intact on filesystem")

    # --------------------------------------------------------------------------
    # 7. Partial Batch Failure: Earlier Succeeded + Later Failed -> No Success
    # --------------------------------------------------------------------------
    e_7_ok = EntryModel(name="Game7A", application_id=0x0100000000010007, save_data_space_id=0, save_data_id=0x7001, is_backup=False)
    e_7_fail = EntryModel(name="Game7B", application_id=0x0100000000010008, save_data_space_id=0, save_data_id=0x7002, is_backup=False)
    fs_7 = MockLiveSaveFsOps(live_registry={(0, 0x7001): {}}, injected_space_failures={(0, 0x7002): 0x202})

    # Pre-fix defect reproduction: deleted_count > 0 and failed_count > 0 emitted "Delete successful!"
    _, del_7_old, fail_7_old, msg_7_old = delete_saves_old_model([e_7_ok, e_7_fail], MockProgressBox(), MockSdFs(), fs_7)
    check(del_7_old == 1 and fail_7_old == 1 and msg_7_old == "NOTIFY:Delete successful!",
          "7.1 Defect reproduction: old partial batch emitted 'Delete successful!'")

    # Post-fix: partial batch returns first failure and surfaces ErrorBox!
    fs_7_new = MockLiveSaveFsOps(live_registry={(0, 0x7001): {}}, injected_space_failures={(0, 0x7002): 0x202})
    rc_7, del_7, fail_7, msg_7 = delete_saves_model([e_7_ok, e_7_fail], MockProgressBox(), MockSdFs(), fs_7_new)
    check(del_7 == 1 and fail_7 == 1, f"7.2: Expected 1 deleted and 1 failed, got del={del_7} fail={fail_7}")
    check(rc_7 == 0x202, f"7.3: Must return first failure 0x202, got 0x{rc_7:X}")
    check(msg_7 != "NOTIFY:Delete successful!", "7.4: Partial batch must NOT report success")
    check(msg_7.startswith("ERROR_BOX:Delete failed!"), f"7.5: Partial batch must surface ErrorBox, got '{msg_7}'")

    # --------------------------------------------------------------------------
    # 8. Cancellation Propagation Before and Between Entries / Archives
    # --------------------------------------------------------------------------
    # 8a: Cancel before entry 0
    pbox_8a = MockProgressBox(exit_at_step=0)
    fs_8a = MockLiveSaveFsOps(live_registry={(0, 0x8001): {}})
    rc_8a, del_8a, fail_8a, msg_8a = delete_saves_model([e_7_ok], pbox_8a, MockSdFs(), fs_8a)
    check(rc_8a == 0x01EC01, f"8.1: Cancel before entry 0 must return cancel code, got 0x{rc_8a:X}")
    check(del_8a == 0 and len(fs_8a.space_id_calls) == 0, "8.2: Zero deletions attempted on early cancellation")
    check(msg_8a != "NOTIFY:Delete successful!", "8.3: Cancelled run must not report success")

    # 8b: Cancel between entry 0 and entry 1
    pbox_8b = MockProgressBox(exit_at_step=1)
    fs_8b = MockLiveSaveFsOps(live_registry={(0, 0x7001): {}, (0, 0x7002): {}})
    rc_8b, del_8b, fail_8b, msg_8b = delete_saves_model([e_7_ok, e_7_fail], pbox_8b, MockSdFs(), fs_8b)
    check(rc_8b == 0x01EC01, "8.4: Cancel between entries must return cancel code")
    check(del_8b == 1, "8.5: Entry 0 succeeded before cancellation")
    check(len(fs_8b.space_id_calls) == 1, "8.6: Entry 1 was not attempted")
    check(msg_8b != "NOTIFY:Delete successful!", "8.7: Mid-batch cancellation must not report success")

    # 8c: Cancel between backup archive 0 and backup archive 1
    pbox_8c = MockProgressBox(exit_at_step=2)
    sd_8c = MockSdFs(existing_files=[cand_6a.path, cand_6b.path])
    rc_8c, del_8c, fail_8c, msg_8c = delete_saves_model([e_6_bkp], pbox_8c, sd_8c, MockLiveSaveFsOps())
    check(rc_8c == 0x01EC01, "8.8: Cancel between archives must return cancel code")
    check(del_8c == 1, "8.9: Archive 0 deleted before cancellation")
    check(len(sd_8c.delete_file_calls) == 1, "8.10: Archive 1 deletion was not attempted")
    check(msg_8c != "NOTIFY:Delete successful!", "8.11: Mid-archive cancellation must not report success")

    # --------------------------------------------------------------------------
    # 9. Directory Cleanup Isolation: Best Effort, Does Not Mask Failure
    # --------------------------------------------------------------------------
    sd_9 = MockSdFs(existing_files=[cand_6a.path], existing_dirs=["/dumps/Game6"],
                    injected_file_failures={cand_6a.path: 0x202})
    rc_9, del_9, fail_9, msg_9 = delete_saves_model([e_6_bkp], MockProgressBox(), sd_9, MockLiveSaveFsOps(),
                                                   backup_collector=lambda e: [cand_6a.path])
    check(fail_9 == 1 and del_9 == 0, "9.1: Archive failure must be recorded")
    check(rc_9 == 0x202, "9.2: Archive failure must be returned regardless of directory cleanup")
    check(len(sd_9.delete_dir_calls) == 0, "9.3: Directory cleanup must NEVER be attempted after an archive deletion failure")
    check("/dumps/Game6" in sd_9.dirs, "9.4: Directory must remain untouched on archive deletion failure")
    check(msg_9 != "NOTIFY:Delete successful!", "9.5: Directory cleanup must not mask archive failure")

    # --------------------------------------------------------------------------
    # 10. Successful Paths Preserved (Live, Backup, Multi-entry)
    # --------------------------------------------------------------------------
    # Live success
    fs_10_live = MockLiveSaveFsOps(live_registry={(0, 0x1000): {}})
    e_10_live = EntryModel(application_id=0x010000000001000A, save_data_space_id=0, save_data_id=0x1000, is_backup=False)
    rc_10a, del_10a, fail_10a, msg_10a = delete_saves_model([e_10_live], MockProgressBox(), MockSdFs(), fs_10_live)
    check(rc_10a == 0 and del_10a == 1 and fail_10a == 0 and msg_10a == "NOTIFY:Delete successful!",
          "10.1: Successful live delete must succeed and notify success")

    # Backup success
    sd_10_bkp = MockSdFs(existing_files=[cand_6a.path], existing_dirs=["/dumps/Game6"])
    e_10_bkp = EntryModel(name="Game6", application_id=0x0100000000010006, is_backup=True, backup_members=[cand_6a])
    rc_10b, del_10b, fail_10b, msg_10b = delete_saves_model([e_10_bkp], MockProgressBox(), sd_10_bkp, MockLiveSaveFsOps())
    check(rc_10b == 0 and del_10b == 1 and fail_10b == 0 and msg_10b == "NOTIFY:Delete successful!",
          "10.2: Successful backup delete must succeed and notify success")
    check("/dumps/Game6" not in sd_10_bkp.dirs, "10.3: Empty directory must be cleaned up on complete success")

    # Multi-entry batch all success
    fs_10_multi = MockLiveSaveFsOps(live_registry={(0, 0x1001): {}, (0, 0x1002): {}})
    e_10_m1 = EntryModel(application_id=1, save_data_space_id=0, save_data_id=0x1001, is_backup=False)
    e_10_m2 = EntryModel(application_id=2, save_data_space_id=0, save_data_id=0x1002, is_backup=False)
    rc_10c, del_10c, fail_10c, msg_10c = delete_saves_model([e_10_m1, e_10_m2], MockProgressBox(), MockSdFs(), fs_10_multi)
    check(rc_10c == 0 and del_10c == 2 and fail_10c == 0 and msg_10c == "NOTIFY:Delete successful!",
          "10.4: Multi-entry batch where all succeed must notify success")

    # --------------------------------------------------------------------------
    # 11. Failure-Ordering: Deletion Failure Precedes Subsequent Cancellation
    # --------------------------------------------------------------------------
    # Scenario 11a: Live deletion failure followed by cancellation checkpoint
    # Entry A fails with concrete error 0x202.
    # Progress checkpoint between Entry A and Entry B would return cancellation (0x01EC01) if reached.
    pbox_11a = MockProgressBox(exit_at_step=1, exit_rc=0x01EC01)
    fs_11a = MockLiveSaveFsOps(injected_space_failures={(0, 0x1101): 0x202}, live_registry={(0, 0x1102): {}})
    e_11a_fail = EntryModel(name="Game11A", application_id=0x0100000000010011, save_data_space_id=0, save_data_id=0x1101, is_backup=False)
    e_11a_unreached = EntryModel(name="Game11B", application_id=0x0100000000010012, save_data_space_id=0, save_data_id=0x1102, is_backup=False)

    rc_11a, del_11a, fail_11a, msg_11a = delete_saves_model([e_11a_fail, e_11a_unreached], pbox_11a, MockSdFs(), fs_11a)
    check(rc_11a == 0x202, f"11.1: Concrete deletion failure 0x202 must be returned, not cancellation; got 0x{rc_11a:X}")
    check(del_11a == 0 and fail_11a == 1, f"11.2: Counts must be del=0 fail=1, got del={del_11a} fail={fail_11a}")
    check(not pbox_11a.cancelled, "11.3: Subsequent cancellation checkpoint must NOT be reached after deletion failure")
    check(pbox_11a.step == 1, f"11.4: ProgressBox must stop before step 1 checkpoint, got step {pbox_11a.step}")
    check(len(fs_11a.space_id_calls) == 1, "11.5: Subsequent deletion must NOT be reached")
    check((0, 0x1102) in fs_11a.live_registry, "11.6: Subsequent entry must remain completely untouched in registry")
    check(msg_11a.startswith("ERROR_BOX:Delete failed!"), "11.7: ErrorBox must be surfaced with concrete failure")

    # Scenario 11b: Archive deletion failure followed by cancellation checkpoint
    # Archive A fails with concrete error 0x339402.
    # Checkpoint before Archive B would return cancellation (0x01EC01) if reached.
    cand_11a = BackupCandidateModel(ts=100, path="/dumps/Game11/20260918_100000.zip", source=1)
    cand_11b = BackupCandidateModel(ts=200, path="/dumps/Game11/20260918_110000.zip", source=1)
    e_11b_bkp = EntryModel(name="Game11", application_id=0x0100000000010013, is_backup=True,
                           backup_members=[cand_11a, cand_11b])
    pbox_11b = MockProgressBox(exit_at_step=2, exit_rc=0x01EC01)
    sd_11b = MockSdFs(existing_files=[cand_11a.path, cand_11b.path], existing_dirs=["/dumps/Game11"],
                      injected_file_failures={cand_11a.path: 0x339402})

    rc_11b, del_11b, fail_11b, msg_11b = delete_saves_model([e_11b_bkp], pbox_11b, sd_11b, MockLiveSaveFsOps())
    check(rc_11b == 0x339402, f"11.8: Concrete archive failure 0x339402 must be returned, not cancellation; got 0x{rc_11b:X}")
    check(del_11b == 0 and fail_11b == 1, f"11.9: Counts must be del=0 fail=1, got del={del_11b} fail={fail_11b}")
    check(not pbox_11b.cancelled, "11.10: Subsequent archive cancellation checkpoint must NOT be reached")
    check(len(sd_11b.delete_file_calls) == 1, "11.11: Subsequent archive deletion must NOT be attempted")
    check(cand_11b.path in sd_11b.files, "11.12: Subsequent archive must remain intact on filesystem")
    check(len(sd_11b.delete_dir_calls) == 0, "11.13: Directory cleanup must NOT be attempted after archive failure")
    check("/dumps/Game11" in sd_11b.dirs, "11.14: Backup directory must remain intact")

    print("  -> Suite 9 (Connected Save & Backup Deletion Fail-Closed & Exact-ID Contract) PASSED.")


# ==============================================================================
# Main Runner
# ==============================================================================
