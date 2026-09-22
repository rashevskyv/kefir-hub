#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for
Sphaira existing-save capacity admission and removal of unsafe auto-growth.

NOTE ON SCOPE AND BOUNDARIES:
This test executes static source pattern verification and a synthetic Python
behavioral reference model.
- Static source checks inspect specific syntax and sequence patterns inside the
  shared RestoreSaveZip function in sphaira/source/ui/menus/save/save_menu_ops.cpp,
  and related caller wiring in sphaira/source/ui/menus/filebrowser/filebrowser_ops.cpp.
  These checks are text anchors and do NOT constitute a C++ control-flow proof,
  compiler-verified call graph, or filesystem mount-lifetime guarantee.
- Synthetic behavioral fixtures simulate the admission decision pipeline and event
  sequencing. They do NOT execute compiled C++ code, Nintendo Switch HOS kernel/FS
  services, libnx IPC calls, hardware transactions, or physical filesystem
  allocation slack / directory entry overhead.
"""

import os
import sys

INT64_MAX = 9223372036854775807

# Diagnostic tokens for model simulation (matching libnx/sphaira defines where applicable)
RES_OK = 0
FS_ERROR_PATH_NOT_FOUND = 0x202
FS_ERROR_INVALID_SIZE = 0x2F5C02  # defines.hpp FsError_InvalidSize
ERR_PREFLIGHT_FAILED = 0x1001
ERR_IPC_READ_FAILED = 0x1002

def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ==============================================================================
# 1. Source Contract Tests
# ==============================================================================

def test_source_contracts() -> None:
    print("[1] Running static source contract checks...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # Extract RestoreSaveZip function body from save_restore_zip.cpp
    rsz_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    with open(rsz_path, "r", encoding="utf-8") as f:
        rsz_src = f.read()

    save_ops_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(save_ops_path, "r", encoding="utf-8") as f:
        save_ops_src = f.read()

    rsz_start = rsz_src.find("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path")
    check(rsz_start != -1, "RestoreSaveZip definition must exist in save_restore_zip.cpp")

    rsz_end = rsz_src.find("} // namespace sphaira::ui::menu::save", rsz_start)
    check(rsz_end != -1, "Namespace close must follow RestoreSaveZip in save_restore_zip.cpp")

    rsz_body = rsz_src[rsz_start:rsz_end]

    # 1.1 Ordering: Checked preflight before exact-space live extra read, capacity guard, writable mount, and destructive clear
    pos_preflight = rsz_body.find("R_TRY(thread::TransferUnzipPreflight(pbox, zfile, \"/\", save_filter, true, &summary")
    check(pos_preflight != -1,
          "RestoreSaveZip must execute checked TransferUnzipPreflight (R_TRY) with &summary")

    pos_read_extra = rsz_body.find("R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&live, sizeof(live), save_data_space_id, target_save_data_id));")
    if pos_read_extra == -1:
        pos_read_extra = rsz_body.find("R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&live, sizeof(live), save_data_space_id, e.save_data_id));")
    check(pos_read_extra != -1,
          "RestoreSaveZip must execute exact R_TRY-wrapped fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId using save_data_space_id and target_save_data_id")

    pos_capacity_guard = rsz_body.find("summary.file_bytes > live.data_size")
    check(pos_capacity_guard != -1,
          "RestoreSaveZip must compare summary.file_bytes > live.data_size")

    pos_writable_mount = rsz_body.find("fs::FsNativeSave save_fs{(FsSaveDataType)attr.save_data_type, save_data_space_id, &attr, false};")
    check(pos_writable_mount != -1,
          "RestoreSaveZip must open final writable restore mount save_fs")

    pos_delete_coll = rsz_body.find("filebrowser::FsView::DeleteAllCollections(pbox, &save_fs, collections)")
    check(pos_delete_coll != -1,
          "RestoreSaveZip must call DeleteAllCollections before restore extraction")

    pos_unzip_all = rsz_body.find("thread::TransferUnzipAll(pbox, zfile, &save_fs, \"/\", save_filter, thread::Mode::SingleThreadedIfSmaller, true")
    check(pos_unzip_all != -1,
          "RestoreSaveZip must extract ZIP contents via TransferUnzipAll with save_filter")

    pos_commit = rsz_body.find("save_fs.Commit()", pos_unzip_all)
    check(pos_commit != -1,
          "RestoreSaveZip must commit save_fs after TransferUnzipAll")

    check(pos_preflight < pos_read_extra,
          "Checked preflight must complete before live extra read")
    check(pos_read_extra < pos_capacity_guard,
          "Live extra read must occur before capacity guard")
    check(pos_capacity_guard < pos_writable_mount,
          "Capacity guard must precede final writable restore mount")
    check(pos_writable_mount < pos_delete_coll,
          "Final writable mount must precede destructive DeleteAllCollections")
    check(pos_delete_coll < pos_unzip_all < pos_commit,
          "Delete collections, unzip all, and commit must follow sequential ordering")

    # 1.2 Scoped probe destruction before live extra read
    pos_probe_scope = rsz_body.find("fs::FsNativeSave check_save_fs{")
    check(pos_probe_scope != -1, "RestoreSaveZip must probe existing save with scoped FsNativeSave")
    check(pos_probe_scope < pos_read_extra,
          "Scoped probe must precede live extra read")

    # 1.3 Exact 7 identity field comparisons on live.attr vs attr (destination authority)
    identity_fields = [
        "live.attr.application_id != attr.application_id",
        "live.attr.uid.uid[0] != attr.uid.uid[0]",
        "live.attr.uid.uid[1] != attr.uid.uid[1]",
        "live.attr.system_save_data_id != attr.system_save_data_id",
        "live.attr.save_data_type != attr.save_data_type",
        "live.attr.save_data_rank != attr.save_data_rank",
        "live.attr.save_data_index != attr.save_data_index",
    ]
    for field_check in identity_fields:
        pos_field = rsz_body.find(field_check)
        check(pos_field != -1,
              f"RestoreSaveZip must compare action identity field: {field_check}")
        check(pos_read_extra < pos_field < pos_writable_mount,
              f"Identity field check {field_check} must occur between live read and writable mount")

    check("return FsError_PathNotFound;" in rsz_body,
          "RestoreSaveZip must return FsError_PathNotFound on identity mismatch")

    # 1.4 Positive data size and nonnegative journal size guards
    pos_size_guard = rsz_body.find("live.data_size <= 0 || live.journal_size < 0")
    check(pos_size_guard != -1,
          "RestoreSaveZip must guard live.data_size > 0 and live.journal_size >= 0")
    check(pos_read_extra < pos_size_guard < pos_writable_mount,
          "Signed size guard must occur between live read and writable mount")
    check("return FsError_InvalidSize;" in rsz_body,
          "RestoreSaveZip must return FsError_InvalidSize on invalid size or capacity failure")

    # 1.5 Ponytail caveat comment present with exact ceiling and without false guarantees
    check("// ponytail: payload bytes are a rejection lower bound" in rsz_body,
          "RestoreSaveZip must contain exact '// ponytail:' lower-bound rejection caveat comment")
    check("capacity-guaranteed" not in rsz_body and "safe-transaction" not in rsz_body,
          "RestoreSaveZip comment must not claim capacity-guaranteed or safe transaction")

    # 1.6 Complete removal of extend calls, second ZIP sizing loop, and existing-slot metadata sizing authority
    check("fsExtendSaveDataFileSystem" not in rsz_body,
          "RestoreSaveZip must NOT call fsExtendSaveDataFileSystem")
    check("unzGetGlobalInfo64" not in rsz_body,
          "RestoreSaveZip must NOT perform second unzGetGlobalInfo64 ZIP sizing scan")
    check("total_size % extra.journal_size" not in rsz_body,
          "RestoreSaveZip must NOT perform journal remainder sizing rounding")

    # Verify metadata sizing authority applies only to legacy create branch (save_data_id == 0)
    check("if (target_entry.save_data_id == 0)" in rsz_body or "if (e.save_data_id == 0)" in rsz_body,
          "Metadata sizing authority must be restricted strictly to target_entry.save_data_id == 0")
    check("if (e.save_data_id != 0)" not in rsz_body[:pos_probe_scope],
          "e.save_data_id != 0 must not read sizing from archive metadata")

    # 1.7 Shared restore owners across Save Menu and File Browser
    fb_ops_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_ops.cpp")
    with open(fb_ops_path, "r", encoding="utf-8") as f:
        fb_ops_src = f.read()

    check("return save::RestoreSaveZip(pbox, se, file_path" in fb_ops_src,
          "File Browser restore must delegate to save::RestoreSaveZip")
    check("return RestoreSaveZip(pbox, e, path" in save_ops_src,
          "Save Menu RestoreSaveInternal must delegate to RestoreSaveZip")

    print("    OK: All static source contracts passed.")


# ==============================================================================
# 2. Synthetic Behavioral Simulation Model
# ==============================================================================

class SyntheticAttribute:
    def __init__(self, app_id: int, sys_id: int, uid0: int, uid1: int,
                 save_type: int, rank: int, index: int):
        self.application_id = app_id
        self.system_save_data_id = sys_id
        self.uid0 = uid0
        self.uid1 = uid1
        self.save_data_type = save_type
        self.save_data_rank = rank
        self.save_data_index = index

    def matches(self, other: "SyntheticAttribute") -> bool:
        return (
            self.application_id == other.application_id and
            self.uid0 == other.uid0 and
            self.uid1 == other.uid1 and
            self.system_save_data_id == other.system_save_data_id and
            self.save_data_type == other.save_data_type and
            self.save_data_rank == other.save_data_rank and
            self.save_data_index == other.save_data_index
        )


class SyntheticExtraData:
    def __init__(self, attr: SyntheticAttribute, data_size: int, journal_size: int):
        self.attr = attr
        self.data_size = data_size
        self.journal_size = journal_size


class SyntheticTargetEntry:
    def __init__(self, space_id: int, save_id: int, attr: SyntheticAttribute, is_backup: bool = False):
        self.save_data_space_id = space_id
        self.save_data_id = save_id
        self.attr = attr
        self.is_backup = is_backup


class SyntheticPayloadSummary:
    def __init__(self, file_bytes: int, file_count: int, directory_count: int):
        self.file_bytes = file_bytes
        self.file_count = file_count
        self.directory_count = directory_count


class RestorePipelineSimulator:
    def __init__(self):
        self.events: list[str] = []
        self.destructive_events: list[str] = []
        self.requested_live_read_keys: list[tuple[int, int]] = []

    def execute(self,
                target: SyntheticTargetEntry,
                summary: SyntheticPayloadSummary,
                live_extra: SyntheticExtraData | None = None,
                read_rc: int = RES_OK,
                preflight_rc: int = RES_OK,
                archive_meta_data_size: int | None = None) -> int:
        self.events.clear()
        self.destructive_events.clear()

        # Phase 1: Full preflight
        self.events.append("preflight")
        if preflight_rc != RES_OK:
            self.events.append("preflight_error")
            return preflight_rc

        # For existing target: e.save_data_id != 0
        if target.save_data_id != 0:
            # Metadata sizing authority is completely ignored for existing targets
            # (archive_meta_data_size has no effect)

            # Phase 2: Probe existing save filesystem
            self.events.append("probe_existing_open")
            self.events.append("probe_existing_close")

            # Phase 3: Exact-space live extra read
            # Record requested IPC key: (save_data_space_id, save_data_id)
            read_key = (target.save_data_space_id, target.save_data_id)
            self.requested_live_read_keys.append(read_key)
            self.events.append(f"read_live_extra:{read_key[0]}:{read_key[1]:X}")
            if read_rc != RES_OK:
                self.events.append("read_live_extra_failed")
                return read_rc

            assert live_extra is not None

            # Phase 4: Full 7-field action identity comparison
            self.events.append("check_identity")
            if not live_extra.attr.matches(target.attr):
                self.events.append("identity_mismatch")
                return FS_ERROR_PATH_NOT_FOUND

            # Phase 5: Signed live sizes validation
            self.events.append("check_live_sizes")
            if live_extra.data_size <= 0 or live_extra.journal_size < 0:
                self.events.append("invalid_live_size")
                return FS_ERROR_INVALID_SIZE

            # Phase 6: Checked lower-bound capacity admission
            self.events.append("check_capacity_lower_bound")
            if summary.file_bytes > live_extra.data_size:
                self.events.append("capacity_exceeded")
                return FS_ERROR_INVALID_SIZE

        # Destructive phase: reached only on successful admission
        self.events.append("open_writable")
        self.events.append("delete_collections")
        self.destructive_events.append("delete_collections")

        self.events.append("unzip_all")
        self.destructive_events.append("unzip_all")

        self.events.append("commit")
        self.destructive_events.append("commit")

        return RES_OK


# ==============================================================================
# 3. Synthetic Model Behavioral Fixtures
# ==============================================================================

def make_valid_fixture(payload_bytes: int = 4096,
                       live_data: int = 1048576,
                       live_journal: int = 262144,
                       space_id: int = 1,
                       save_id: int = 0x1000AABBCCDDEEFF) -> tuple[SyntheticTargetEntry, SyntheticPayloadSummary, SyntheticExtraData]:
    attr_target = SyntheticAttribute(
        app_id=0x0100000000010000,
        sys_id=0,
        uid0=0x1111222233334444,
        uid1=0x5555666677778888,
        save_type=1,  # FsSaveDataType_Account = 1
        rank=0,
        index=0,
    )
    target = SyntheticTargetEntry(space_id=space_id, save_id=save_id, attr=attr_target)

    attr_live = SyntheticAttribute(
        app_id=0x0100000000010000,
        sys_id=0,
        uid0=0x1111222233334444,
        uid1=0x5555666677778888,
        save_type=1,
        rank=0,
        index=0,
    )
    live = SyntheticExtraData(attr=attr_live, data_size=live_data, journal_size=live_journal)
    summary = SyntheticPayloadSummary(file_bytes=payload_bytes, file_count=5, directory_count=2)
    return target, summary, live


def test_synthetic_model_fixtures() -> None:
    print("[2] Running synthetic model behavioral fixtures...")
    sim = RestorePipelineSimulator()

    # Fixture 1: Actual read Result failure -> same error, zero destructive events
    target, summary, live = make_valid_fixture()
    rc = sim.execute(target, summary, live, read_rc=ERR_IPC_READ_FAILED)
    check(rc == ERR_IPC_READ_FAILED, "Read failure must propagate exact Result code")
    check(len(sim.destructive_events) == 0,
          "Read failure must produce ZERO destructive events")
    check("delete_collections" not in sim.events,
          "Read failure must never reach clear")

    # Fixture 2: Mismatch of each of the 7 identity fields (separately testing both UID halves)
    field_mutations = [
        ("application_id", lambda a: setattr(a, "application_id", 0x0100000000099999)),
        ("system_save_data_id", lambda a: setattr(a, "system_save_data_id", 0x8000000000000001)),
        ("uid0", lambda a: setattr(a, "uid0", 0x9999999999999999)),
        ("uid1", lambda a: setattr(a, "uid1", 0xAAAAAAAAAAAAAAAA)),
        ("save_data_type", lambda a: setattr(a, "save_data_type", 3)),  # FsSaveDataType_Device = 3
        ("save_data_rank", lambda a: setattr(a, "save_data_rank", 1)),
        ("save_data_index", lambda a: setattr(a, "save_data_index", 1)),
    ]
    for field_name, mutator in field_mutations:
        target, summary, live = make_valid_fixture()
        mutator(live.attr)
        rc = sim.execute(target, summary, live)
        check(rc == FS_ERROR_PATH_NOT_FOUND,
              f"Identity mismatch on '{field_name}' must return FsError_PathNotFound")
        check(len(sim.destructive_events) == 0,
              f"Identity mismatch on '{field_name}' must produce ZERO destructive events")
        check("delete_collections" not in sim.events,
              f"Identity mismatch on '{field_name}' must reject before clear")

    # Fixture 3: data_size 0 / negative, journal negative -> reject before clear
    invalid_size_cases = [
        ("data_size_zero", 0, 262144),
        ("data_size_negative", -1, 262144),
        ("journal_negative", 1048576, -1),
        ("both_negative", -10, -5),
    ]
    for label, ds, js in invalid_size_cases:
        target, summary, live = make_valid_fixture()
        live.data_size = ds
        live.journal_size = js
        rc = sim.execute(target, summary, live)
        check(rc == FS_ERROR_INVALID_SIZE,
              f"Invalid size case '{label}' must return FsError_InvalidSize")
        check(len(sim.destructive_events) == 0,
              f"Invalid size case '{label}' must produce ZERO destructive events")

    # Fixture 4: journal 0 + admissible payload -> lower-bound admission, no growth
    target, summary, live = make_valid_fixture(payload_bytes=1000, live_data=2000, live_journal=0)
    rc = sim.execute(target, summary, live)
    check(rc == RES_OK, "Zero journal with admissible payload must pass lower-bound admission")
    check(len(sim.destructive_events) == 3, "Admitted target proceeds to restore")
    check("extend" not in sim.events, "Zero journal must not trigger growth")

    # Fixture 5: payload 0, below data, equal data -> lower-bound admission, no growth
    for pb in [0, 524288, 1048576]:
        target, summary, live = make_valid_fixture(payload_bytes=pb, live_data=1048576)
        rc = sim.execute(target, summary, live)
        check(rc == RES_OK, f"Payload {pb} <= live data must be admitted")
        check("extend" not in sim.events, "Admitted payload must not trigger growth")

    # Fixture 6: payload data+1 -> reject before clear
    target, summary, live = make_valid_fixture(payload_bytes=1048577, live_data=1048576)
    rc = sim.execute(target, summary, live)
    check(rc == FS_ERROR_INVALID_SIZE, "Payload exceeding live data by 1 byte must return FsError_InvalidSize")
    check(len(sim.destructive_events) == 0, "Payload excess must produce ZERO destructive events")
    check("delete_collections" not in sim.events, "Payload excess must reject before clear")

    # Fixture 7: INT64_MAX equal boundary / INT64_MAX vs INT64_MAX-1
    target, summary, live = make_valid_fixture(payload_bytes=INT64_MAX, live_data=INT64_MAX)
    rc = sim.execute(target, summary, live)
    check(rc == RES_OK, "Payload equal to INT64_MAX must pass lower-bound admission")

    target, summary, live = make_valid_fixture(payload_bytes=INT64_MAX, live_data=INT64_MAX - 1)
    rc = sim.execute(target, summary, live)
    check(rc == FS_ERROR_INVALID_SIZE, "INT64_MAX payload against INT64_MAX-1 capacity must reject")
    check(len(sim.destructive_events) == 0, "Boundary rejection must produce ZERO destructive events")

    # Fixture 8: archive metadata smaller/larger/absent does NOT alter existing target sizes or identity
    for meta_size in [100, 999999999999, None]:
        target, summary, live = make_valid_fixture(payload_bytes=5000, live_data=10000)
        rc = sim.execute(target, summary, live, archive_meta_data_size=meta_size)
        check(rc == RES_OK, "Archive metadata sizing must have no authority over existing target")
        check(live.data_size == 10000, "Existing live target data_size must remain untouched")

    # Fixture 9: actual System space=0 is preserved without fallback, and recorded in exact-space IPC key
    target_sys, summary_sys, live_sys = make_valid_fixture(space_id=0, save_id=0x8000000000000001)  # FsSaveDataSpaceId_System = 0
    target_sys.attr.save_data_type = 0  # FsSaveDataType_System = 0
    live_sys.attr.save_data_type = 0
    rc = sim.execute(target_sys, summary_sys, live_sys)
    check(rc == RES_OK, "System space target must be admitted directly without fallback")
    check(target_sys.save_data_space_id == 0, "System space ID must remain 0")
    check(sim.requested_live_read_keys[-1] == (0, 0x8000000000000001),
          "Exact-space IPC read must request System space=0 with target save ID")

    # Fixture 10: two slots with identical attr but different spaces request distinct exact-space IPC keys
    # Note: attribute comparison itself checks action identity fields, while the exact-space IPC key supplies space selection.
    target_user, summary_user, live_user = make_valid_fixture(space_id=1, save_id=0x1000000000000001)
    target_sys_same_attr, summary_sys_same, live_sys_same = make_valid_fixture(space_id=0, save_id=0x1000000000000001)
    sim.execute(target_user, summary_user, live_user)
    user_key = sim.requested_live_read_keys[-1]
    sim.execute(target_sys_same_attr, summary_sys_same, live_sys_same)
    sys_key = sim.requested_live_read_keys[-1]
    check(user_key != sys_key, "Slots in different spaces must request distinct exact-space IPC keys")
    check(user_key == (1, 0x1000000000000001), "User slot must request space_id=1")
    check(sys_key == (0, 0x1000000000000001), "System slot must request space_id=0")

    # Separate rank mismatch fixture: different rank values are strictly non-interchangeable
    target_rank0, _, _ = make_valid_fixture(space_id=1)
    target_rank0.attr.save_data_rank = 0
    _, _, live_rank1 = make_valid_fixture(space_id=1)
    live_rank1.attr.save_data_rank = 1
    check(not target_rank0.attr.matches(live_rank1.attr),
          "Slots with differing save_data_rank must never match")

    # Fixture 11: preflight corruption/cancel -> no live probe, no sizing mutation, no clear, no write
    target, summary, live = make_valid_fixture()
    rc = sim.execute(target, summary, live, preflight_rc=ERR_PREFLIGHT_FAILED)
    check(rc == ERR_PREFLIGHT_FAILED, "Preflight failure must halt restore immediately")
    check("read_live_extra" not in sim.events, "Preflight failure must not read live extra data")
    check("probe_existing_open" not in sim.events, "Preflight failure must not probe existing filesystem")
    check(len(sim.destructive_events) == 0, "Preflight failure must produce ZERO destructive events")

    # Fixture 12: Many zero-byte files / implicit parent paths: passing byte guard is NOT a fit proof
    # With file_bytes = 0, summary passes summary.file_bytes <= live.data_size,
    # but 20,000 files and 10,000 directories consume filesystem inode and allocation slack.
    # NOTE: This model cannot determine actual filesystem fit; aggregate payload bytes
    # serve solely as a lower-bound rejection guard.
    target, summary_many_zeros, live = make_valid_fixture(payload_bytes=0, live_data=32768)
    summary_many_zeros.file_count = 20000
    summary_many_zeros.directory_count = 10000
    rc = sim.execute(target, summary_many_zeros, live)
    check(rc == RES_OK, "Byte guard admits 0-byte payload as lower bound")

    print("    OK: All 12 synthetic behavioral fixture groups passed.")


# ==============================================================================
# Main
# ==============================================================================

def main() -> None:
    test_source_contracts()
    test_synthetic_model_fixtures()
    print("ALL EXISTING SAVE CAPACITY CONTRACT CHECKS AND FIXTURES PASSED")


if __name__ == "__main__":
    main()
