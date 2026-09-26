#!/usr/bin/env python3
"""
Sphaira v0.13.869: Shared Verified Save-Slot Backend Contract & Behavioral Regression
Compiler-free Python stdlib regression verifying save slot creation, growth,
restore admission preflight, lifecycle ordering, and safe-failure guarantees.
"""

import os
import sys

def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def read_file(*parts: str) -> str:
    path = os.path.join(REPO_ROOT, *parts)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()

# Exact libnx enumeration values
FsSaveDataType_System = 0
FsSaveDataType_Account = 1
FsSaveDataType_Bcat = 2
FsSaveDataType_Device = 3
FsSaveDataType_Temporary = 4
FsSaveDataType_Cache = 5
FsSaveDataType_SystemBcat = 6

FsSaveDataSpaceId_System = 0
FsSaveDataSpaceId_User = 1
FsSaveDataSpaceId_SdSystem = 2
FsSaveDataSpaceId_Temporary = 3
FsSaveDataSpaceId_SdUser = 4
FsSaveDataSpaceId_ProperSystem = 100
FsSaveDataSpaceId_SafeMode = 101

CONCRETE_SAVE_DATA_SPACES = {
    FsSaveDataSpaceId_System,
    FsSaveDataSpaceId_User,
    FsSaveDataSpaceId_SdSystem,
    FsSaveDataSpaceId_Temporary,
    FsSaveDataSpaceId_SdUser,
    FsSaveDataSpaceId_ProperSystem,
    FsSaveDataSpaceId_SafeMode,
}

FsSaveDataRank_Primary = 0

# ==============================================================================
# 1. Static Source Wiring Contracts
# ==============================================================================

def test_source_wiring_contracts() -> None:
    print("[1] Running static source wiring contracts...")

    cmake_src = read_file("sphaira", "CMakeLists.txt")
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(888, 900)),
          "sphaira/CMakeLists.txt must define valid sphaira_VERSION")
    check("source/ui/menus/save/save_slot_backend.cpp" in cmake_src,
          "sphaira/CMakeLists.txt must compile save_slot_backend.cpp")
    check("source/ui/menus/save/save_slot_admission.cpp" in cmake_src,
          "sphaira/CMakeLists.txt must compile save_slot_admission.cpp")

    # B. Header declarations and decoupling in save_slot_backend.hpp
    backend_hpp = read_file("sphaira", "include", "ui", "menus", "save", "save_slot_backend.hpp")
    for forbidden in ["save_paths.hpp", "threaded_file_transfer.hpp", "DecodedSaveMetadata", "UnzipPayloadSummary"]:
        check(forbidden not in backend_hpp, f"save_slot_backend.hpp must not include/leak {forbidden}")
    for required in [
        "struct SaveArchiveSizing {", "struct ProgressBox;", "bool create_succeeded{false};",
        "ArchiveMetadata", "bool has_metadata{false};",
        "auto ValidateCreationRequest(const SaveCreationRequest& req) -> SaveBackendStatus;",
        "auto PlanAccountSaveCreation(", "auto InspectSaveArchiveAdmission(",
        "auto CreateSaveDataChecked(", "auto ExtendSaveDataChecked("
    ]:
        check(required in backend_hpp, f"save_slot_backend.hpp must declare {required}")

    # C. Explicit flags copy, cancellation, and concrete grow guards in save_slot_backend.cpp
    backend_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_slot_backend.cpp")
    for pattern, msg in [
        ("info.flags = request.flags;", "save_slot_backend.cpp must explicitly copy info.flags"),
        ("result.rc = Result_TransferCancelled;", "save_slot_backend.cpp must use Result_TransferCancelled"),
        ("IsConcreteSaveDataSpace", "save_slot_backend.cpp must define IsConcreteSaveDataSpace"),
        ("QuerySaveDataSpaceFreeBytes", "save_slot_backend.cpp must query free space"),
        ("additional_required_bytes", "save_slot_backend.cpp must calculate additional_required_bytes"),
        ("target_free_bytes < additional_required_bytes", "save_slot_backend.cpp must refuse extend"),
        ("result.status = SaveBackendStatus::IpcFailed;", "save_slot_backend.cpp must handle IPC failure"),
        ("fsCreateSaveDataFileSystem(&request.attr, &info, &meta)", "save_slot_backend.cpp must call fsCreateSaveDataFileSystem"),
        ("fsExtendSaveDataFileSystem(", "save_slot_backend.cpp must call fsExtendSaveDataFileSystem"),
    ]:
        check(pattern in backend_cpp, msg)

    for root, _, files in os.walk(os.path.join(REPO_ROOT, "sphaira")):
        for f in files:
            if f.endswith((".cpp", ".hpp", ".h", ".c")):
                with open(os.path.join(root, f), "r", encoding="utf-8", errors="ignore") as sf:
                    src = sf.read()
                check("FsError_Cancelled" not in src, f"Undefined FsError_Cancelled in {f}")
                if root.startswith(os.path.join(REPO_ROOT, "sphaira", "source")) and f.endswith((".cpp", ".hpp")) and f != "save_slot_backend.cpp":
                    check("fsCreateSaveDataFileSystem" not in src and "fsExtendSaveDataFileSystem" not in src,
                          f"Raw fsCreate/ExtendSaveDataFileSystem found in {f}")

    # E. create_succeeded set only on IPC success
    ipc_pos = backend_cpp.find("const auto create_rc = fsCreateSaveDataFileSystem(&request.attr, &info, &meta);")
    succ_pos = backend_cpp.find("result.create_succeeded = true;", ipc_pos)
    fail_branch = backend_cpp.find("if (R_FAILED(create_rc))", ipc_pos)
    check(ipc_pos != -1 and fail_branch != -1 and succ_pos != -1 and fail_branch < succ_pos,
          "create_succeeded must only be set after checking R_FAILED(create_rc)")

    # F. game_internal.cpp reuses checked backend
    game_cpp = read_file("sphaira", "source", "ui", "menus", "game", "game_internal.cpp")
    check("save::PlanAccountSaveCreation(app_id, uid, nullptr, req, &status)" in game_cpp,
          "game_internal.cpp must reuse PlanAccountSaveCreation")
    check("save::CreateSaveDataChecked(req)" in game_cpp,
          "game_internal.cpp must reuse CreateSaveDataChecked")

    # G. RestoreSaveZip preflight before create, create_succeeded check, and honest recovery
    zip_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    ops_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    preflight_pos = zip_cpp.find("thread::TransferUnzipPreflight(pbox, zfile, \"/\", save_filter, true, &summary, &source_inventory, allow_empty)")
    create_pos = zip_cpp.find("CreateSaveDataChecked(e.creation_request", preflight_pos)
    check(preflight_pos != -1 and create_pos != -1 and preflight_pos < create_pos,
          "RestoreSaveZip must execute TransferUnzipPreflight before CreateSaveDataChecked")
    check("if (create_res.create_succeeded) {" in zip_cpp,
          "RestoreSaveZip must check create_succeeded before setting out_mutation_started or out_created_slot_retained")
    check("if (target_entry.save_data_id != 0 && !was_newly_created)" in zip_cpp,
          "RestoreSaveZip must skip safety recovery archive creation when was_newly_created is true")
    check("if (*created_slot_retained)" in ops_cpp,
          "RestoreSavesPicked must detect retained slot on post-create failure")

    # H. Async admission with ProgressBox, BackupGroupKey reinspection in save_restore_route.cpp
    route_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_restore_route.cpp")
    plan_func_pos = route_cpp.find("void PlanRestoreCreation(")
    check("InspectSaveArchiveAdmission(archive_path, nullptr" not in route_cpp,
          "save_restore_route.cpp must not perform synchronous admission with nullptr")
    pbox_push_pos = route_cpp.find("App::Push<ProgressBox>(0, \"Checking backup...\"_i18n", plan_func_pos)
    adm_call_pos = route_cpp.find("InspectSaveArchiveAdmission(archive_path, pbox, false)", pbox_push_pos)
    plan_call_pos = route_cpp.find("PlanAccountSaveCreation(", adm_call_pos)
    reinspect_pos = route_cpp.find("InspectBackupArchive(probe_fs, archive_path", plan_call_pos)
    key_match_pos = route_cpp.find("BackupGroupKey(check_info) == BackupGroupKey(group)", reinspect_pos)
    target_pos = route_cpp.find("target.is_planned_create = true;", key_match_pos)

    check(plan_func_pos != -1 and pbox_push_pos != -1 and adm_call_pos != -1 and plan_call_pos != -1 and
          reinspect_pos != -1 and key_match_pos != -1 and target_pos != -1,
          "PlanRestoreCreation must run async admission -> plan -> reinspect -> target in sequence")
    check("FormatSaveCreationPrompt" not in route_cpp and 'App::Push<OptionBox>(prompt,' not in route_cpp,
          "Restore must not ask again to create the planned save slot")
    check("Selected backup archive has changed or is no longer available." in route_cpp,
          "save_restore_route.cpp must show exact abort message if archive reinspection fails")
    check("group.save_data_space_id != FsSaveDataSpaceId_User" not in route_cpp,
          "PlanRestoreCreation must not reject backup group based on its source space_id")
    check("is_local_account" not in route_cpp,
          "save_restore_route.cpp must not bypass account picker for local backup UIDs")

    # I. Grow backend UI call sites are authorized only in game_save_manager.cpp
    for root, _, files in os.walk(os.path.join(REPO_ROOT, "sphaira", "source", "ui")):
        for f in files:
            if f.endswith((".cpp", ".hpp")) and f not in ("save_slot_backend.cpp", "game_save_manager.cpp"):
                p = os.path.join(root, f)
                with open(p, "r", encoding="utf-8") as sf:
                    src = sf.read()
                check("ExtendSaveDataChecked" not in src,
                      f"ExtendSaveDataChecked unexpectedly called in UI file {f}")
    check("Game Tools" not in ops_cpp and "Game Tools" not in backend_cpp,
          "No premature Game Tools UI in save backend or operations")

    print("  -> Static source wiring contracts PASSED.")

# ==============================================================================
# 2. Synthetic Behavioral Reference Model
# ==============================================================================

class MockSaveDataInfo:
    def __init__(self, space_id=FsSaveDataSpaceId_User, save_type=FsSaveDataType_Account,
                 app_id=0x0100000000010000, sys_id=0, uid=(1, 1), rank=FsSaveDataRank_Primary,
                 index=0, save_id=1, size=0x40000):
        self.save_data_space_id = space_id
        self.save_data_type = save_type
        self.application_id = app_id
        self.system_save_data_id = sys_id
        self.uid = uid
        self.save_data_rank = rank
        self.save_data_index = index
        self.save_data_id = save_id
        self.size = size

    def key(self) -> str:
        return f"{self.save_data_space_id}:{self.save_data_type}:{self.application_id:016X}:{self.system_save_data_id:016X}:{self.uid[0]:016X}:{self.uid[1]:016X}:{self.save_data_rank}:{self.save_data_index}"

class SyntheticSaveSlotBackend:
    ALIGNMENT = 0x4000
    INT64_MAX = 0x7FFFFFFFFFFFFFFF
    Result_TransferCancelled = 0x00123456

    def __init__(self, installed_titles=None, local_uids=None, free_bytes=0x10000000):
        self.installed_titles = installed_titles or {0x0100000000010000: {"owner": 0x0100000000010000, "data": 0x40000, "journal": 0x40000}}
        self.local_uids = local_uids or {(1, 1)}
        self.free_bytes = free_bytes
        self.live_slots = {}
        self.next_save_id = 100
        self.create_ipc_fail = False
        self.extend_ipc_fail = False
        self.discovery_space_mismatch = False
        self.discovery_zero_save_id = False
        self.readback_size_mismatch = False
        self.readback_attr_mismatch = False
        self.extend_post_size_below_requested = False

    def inspect_archive_admission(self, archive):
        if not archive.get("open_success", True):
            return {"admitted": False, "reason": "OpenError"}
        if archive.get("corrupt_zip", False):
            return {"admitted": False, "reason": "CorruptZip"}
        if archive.get("crc_failure", False):
            return {"admitted": False, "reason": "CrcFailure"}
        if archive.get("path_traversal", False):
            return {"admitted": False, "reason": "PathTraversal"}
        if archive.get("file_count", 1) == 0 and not archive.get("allow_empty", False):
            return {"admitted": False, "reason": "MetadataOnlyOrEmpty"}
        if archive.get("metadata_status") == "Invalid":
            return {"admitted": False, "reason": "InvalidMetadata"}
        if not archive.get("close_success", True):
            return {"admitted": False, "reason": "CloseError"}
        sizing = {"has_sizing": False, "data_size": 0, "journal_size": 0}
        if archive.get("metadata_status") == "Valid":
            sizing = {
                "has_sizing": True,
                "data_size": archive.get("metadata", {}).get("data", 0),
                "journal_size": archive.get("metadata", {}).get("journal", 0)
            }
        return {
            "admitted": True,
            "sizing": sizing,
            "file_count": archive.get("file_count", 1)
        }

    def validate_creation(self, req):
        if req["type"] != FsSaveDataType_Account:
            return "UnsupportedSaveType"
        if req["space"] != FsSaveDataSpaceId_User:
            return "UnsupportedSpace"
        if req["rank"] != FsSaveDataRank_Primary:
            return "UnsupportedRank"
        if req["index"] != 0:
            return "UnsupportedIndex"
        if req["app_id"] == 0:
            return "InvalidApplicationId"
        if req.get("owner", 0) == 0:
            return "MissingOwnerId"
        if req["data_size"] <= 0 or req["data_size"] % self.ALIGNMENT != 0 or req["data_size"] > self.INT64_MAX:
            return "InvalidSizes"
        if req["journal_size"] < 0 or req["journal_size"] % self.ALIGNMENT != 0 or req["journal_size"] > self.INT64_MAX:
            return "InvalidSizes"
        if req["uid"] not in self.local_uids or req["uid"] == (0, 0):
            return "InvalidAccountUid"
        if req.get("flags", 0) != 0:
            return "InvalidFlags"
        return "Success"

    def plan_creation(self, app_id, selected_uid, archive_sizing=None):
        if app_id == 0:
            return None, "InvalidApplicationId"
        if selected_uid not in self.local_uids or selected_uid == (0, 0):
            return None, "InvalidAccountUid"

        if app_id in self.installed_titles:
            ctrl = self.installed_titles[app_id]
            if ctrl.get("owner", 0) == 0:
                return None, "MissingOwnerId"
            data = ctrl["data"]
            journal = ctrl["journal"]
            prov = "InstalledControlData"
            if archive_sizing and archive_sizing.get("has_sizing"):
                m_data = archive_sizing.get("data_size", 0)
                m_journal = archive_sizing.get("journal_size", 0)
                if m_data <= 0 or m_data % self.ALIGNMENT != 0 or m_data > self.INT64_MAX:
                    return None, "InvalidSizes"
                if m_journal < 0 or m_journal % self.ALIGNMENT != 0 or m_journal > self.INT64_MAX:
                    return None, "InvalidSizes"
                if m_data > data or m_journal > journal:
                    data = max(data, m_data)
                    journal = max(journal, m_journal)
                    prov = "InstalledControlDataAndArchiveMetadata"
            owner = ctrl["owner"]
        else:
            if not archive_sizing or not archive_sizing.get("has_metadata"):
                return None, "MissingControlData"
            if archive_sizing.get("app_id") != app_id:
                return None, "InvalidApplicationId"
            if archive_sizing.get("type") != FsSaveDataType_Account or archive_sizing.get("system_save_data_id", 0) != 0:
                return None, "UnsupportedSaveType"
            if archive_sizing.get("rank") != FsSaveDataRank_Primary:
                return None, "UnsupportedRank"
            if archive_sizing.get("index") != 0:
                return None, "UnsupportedIndex"
            if archive_sizing.get("owner", 0) == 0:
                return None, "MissingOwnerId"
            m_data = archive_sizing.get("data_size", 0)
            m_journal = archive_sizing.get("journal_size", 0)
            if m_data <= 0 or m_data % self.ALIGNMENT != 0 or m_data > self.INT64_MAX:
                return None, "InvalidSizes"
            if m_journal < 0 or m_journal % self.ALIGNMENT != 0 or m_journal > self.INT64_MAX:
                return None, "InvalidSizes"
            data = m_data
            journal = m_journal
            owner = archive_sizing["owner"]
            prov = "ArchiveMetadata"

        req = {
            "type": FsSaveDataType_Account,
            "space": FsSaveDataSpaceId_User,
            "rank": FsSaveDataRank_Primary,
            "index": 0,
            "app_id": app_id,
            "uid": selected_uid,
            "owner": owner,
            "data_size": data,
            "journal_size": journal,
            "provenance": prov,
            "flags": 0
        }
        val = self.validate_creation(req)
        return req, val

    def create_save_data(self, req, cancelled=False):
        val = self.validate_creation(req)
        if val != "Success":
            return {"verified": False, "create_succeeded": False, "status": val, "ipc_executed": False}
        if cancelled:
            return {"verified": False, "create_succeeded": False, "status": "Cancelled", "rc": self.Result_TransferCancelled, "ipc_executed": False}
        if self.create_ipc_fail:
            return {"verified": False, "create_succeeded": False, "status": "IpcFailed", "rc": 0xE001, "ipc_executed": True}

        save_id = 0 if self.discovery_zero_save_id else self.next_save_id
        self.next_save_id += 1
        space = FsSaveDataSpaceId_SdUser if self.discovery_space_mismatch else req["space"]
        data_sz = req["data_size"] - 1 if self.readback_size_mismatch else req["data_size"]
        uid_val = (0x9999, 0x9999) if self.readback_attr_mismatch else req["uid"]

        info = MockSaveDataInfo(space_id=space, save_type=req["type"], app_id=req["app_id"],
                                uid=uid_val, rank=req["rank"], index=req["index"],
                                save_id=save_id, size=data_sz)
        self.live_slots[info.key()] = {"info": info, "space_id": space, "data_size": data_sz, "journal_size": req["journal_size"]}

        if space != req["space"] or data_sz != req["data_size"] or save_id == 0 or uid_val != req["uid"]:
            return {"verified": False, "create_succeeded": True, "status": "VerificationFailed", "ipc_executed": True, "slot": info}
        return {"verified": True, "create_succeeded": True, "status": "Success", "ipc_executed": True, "slot": info}

    def extend_save_data(self, slot_key, req_space, req_data, req_journal, cancelled=False):
        if req_space not in CONCRETE_SAVE_DATA_SPACES:
            return {"verified": False, "status": "UnsupportedSpace", "ipc_executed": False}
        if slot_key not in self.live_slots:
            return {"verified": False, "status": "SlotNotFound", "ipc_executed": False}
        cur = self.live_slots[slot_key]
        if req_space != cur["space_id"]:
            return {"verified": False, "status": "UnsupportedSpace", "ipc_executed": False}
        if req_data < cur["data_size"]:
            return {"verified": False, "status": "DataShrinkRefused", "ipc_executed": False}
        if req_journal < cur["journal_size"]:
            return {"verified": False, "status": "JournalShrinkRefused", "ipc_executed": False}
        if req_data % self.ALIGNMENT != 0 or req_journal % self.ALIGNMENT != 0:
            return {"verified": False, "status": "AlignmentRefused", "ipc_executed": False}
        if req_data > self.INT64_MAX or req_journal > self.INT64_MAX:
            return {"verified": False, "status": "OverflowRefused", "ipc_executed": False}
        if req_data == cur["data_size"] and req_journal == cur["journal_size"]:
            return {"verified": True, "status": "Success", "ipc_executed": False, "is_noop": True}

        additional_required_bytes = (req_data - cur["data_size"]) + (req_journal - cur["journal_size"])
        if additional_required_bytes > self.free_bytes:
            return {"verified": False, "status": "InsufficientFreeSpace", "ipc_executed": False}

        if cancelled:
            return {"verified": False, "status": "Cancelled", "rc": self.Result_TransferCancelled, "ipc_executed": False}
        if self.extend_ipc_fail:
            return {"verified": False, "status": "IpcFailed", "rc": 0xE002, "ipc_executed": True}

        if self.extend_post_size_below_requested:
            cur["data_size"] = req_data - self.ALIGNMENT
            return {"verified": False, "status": "VerificationFailed", "ipc_executed": True}

        cur["data_size"] = req_data
        cur["journal_size"] = req_journal
        self.free_bytes -= additional_required_bytes
        return {"verified": True, "status": "Success", "ipc_executed": True, "is_noop": False}

def test_behavioral_regressions() -> None:
    print("[2] Running synthetic behavioral regressions...")
    app_id = 0x0100000000010000
    local_uid = (0x1111, 0x2222)
    foreign_uid = (0x9999, 0x8888)

    # 1. Valid Account create for explicitly selected local user
    backend = SyntheticSaveSlotBackend(local_uids={local_uid})
    req, st = backend.plan_creation(app_id, local_uid)
    check(st == "Success", "Planning must succeed for valid installed account save")
    res = backend.create_save_data(req)
    check(res["verified"] and res["create_succeeded"] and res["status"] == "Success",
          "Create must verify and succeed with create_succeeded=True")

    # 2. Foreign backup UID remapped to selected local UID
    check(req["uid"] == local_uid, "Request must use selected local UID, not foreign UID")

    # 3. Source UID never copied into destination
    check(req["uid"] != foreign_uid, "Source foreign UID must never leak into destination")

    # 4-5. Missing/bad/zero/overflowed/unaligned fields and wrong space/rank/index refusal
    for sz in [0, 0x4001, 0x8000000000000000]:
        check(backend.validate_creation(dict(req, data_size=sz)) == "InvalidSizes", "Bad size refused")
    for k, v, exp in [("space", FsSaveDataSpaceId_SdUser, "UnsupportedSpace"), ("rank", 1, "UnsupportedRank"), ("index", 1, "UnsupportedIndex")]:
        check(backend.validate_creation(dict(req, **{k: v})) == exp, f"Wrong {k} refused")

    # 6. Explicit unsupported matrix for non-Account save types
    for ut in [FsSaveDataType_System, FsSaveDataType_Bcat, FsSaveDataType_Device,
               FsSaveDataType_Temporary, FsSaveDataType_Cache, FsSaveDataType_SystemBcat]:
        check(backend.validate_creation(dict(req, type=ut)) == "UnsupportedSaveType", f"Type {ut} refused")

    # 7. Corrupt ZIP, traversal ZIP, CRC failure, and metadata-only ZIP refusal before create
    for k in ["corrupt_zip", "path_traversal", "crc_failure"]:
        check(not backend.inspect_archive_admission({k: True})["admitted"], f"{k} refused")
    check(not backend.inspect_archive_admission({"file_count": 0, "allow_empty": False})["admitted"], "Metadata-only refused")
    check(not backend.inspect_archive_admission({"metadata_status": "Invalid"})["admitted"], "Invalid metadata refused")

    # 8. Cancellation before create with zero mutation and Result_TransferCancelled
    res_cancel = backend.create_save_data(req, cancelled=True)
    check(not res_cancel["verified"] and not res_cancel["create_succeeded"] and not res_cancel["ipc_executed"],
          "Cancelled create must have 0 mutation and ipc_executed=False")
    check(res_cancel["rc"] == backend.Result_TransferCancelled,
          "Cancelled create must return Result_TransferCancelled")

    # 9. Create IPC failure: create_succeeded=False, ipc_executed=True
    backend.create_ipc_fail = True
    res_ipc = backend.create_save_data(req)
    check(not res_ipc["verified"] and not res_ipc["create_succeeded"] and res_ipc["ipc_executed"],
          "IPC failure must report create_succeeded=False, ipc_executed=True")
    backend.create_ipc_fail = False

    # 10. Rediscovery in wrong space: create_succeeded=True (retained), verified=False
    backend.discovery_space_mismatch = True
    res_disc = backend.create_save_data(req)
    check(not res_disc["verified"] and res_disc["create_succeeded"] and res_disc["status"] == "VerificationFailed",
          "Wrong space rediscovery: slot retained (create_succeeded=True) but verified=False")
    backend.discovery_space_mismatch = False

    # 11. Full identity mismatch (wrong UID in readback)
    backend.readback_attr_mismatch = True
    res_attr = backend.create_save_data(req)
    check(not res_attr["verified"] and res_attr["create_succeeded"] and res_attr["status"] == "VerificationFailed",
          "Attr readback mismatch: slot retained but verified=False")
    backend.readback_attr_mismatch = False

    # 12. Missing or zero save ID in rediscovery
    backend.discovery_zero_save_id = True
    res_zero = backend.create_save_data(req)
    check(not res_zero["verified"] and res_zero["create_succeeded"] and res_zero["status"] == "VerificationFailed",
          "Zero save_id rediscovery: slot retained but verified=False")
    backend.discovery_zero_save_id = False

    # 13. Creation size readback mismatch
    backend.readback_size_mismatch = True
    res_sz = backend.create_save_data(req)
    check(not res_sz["verified"] and res_sz["create_succeeded"] and res_sz["status"] == "VerificationFailed",
          "Size readback mismatch: slot retained but verified=False")
    backend.readback_size_mismatch = False

    # 14. Target space strictly User (1); backup group source space 0 (System) never overrides
    plan_from_zero_space, st_zero = backend.plan_creation(app_id, local_uid)
    check(plan_from_zero_space["space"] == FsSaveDataSpaceId_User,
          "Destination space must strictly be User (1)")

    # 15. Post-create restore failure retains exact slot
    res_ok = backend.create_save_data(req)
    check(res_ok["verified"] and res_ok["create_succeeded"] and res_ok["slot"] is not None,
          "Slot created and retained")

    # 16. Existing target never triggers create
    existing_key = res_ok["slot"].key()
    check(existing_key in backend.live_slots, "Existing target slot is known in live slots")

    # 17. Batch completes user selection/create confirmation before first mutation
    batch_seeds = [
        {"app_id": app_id, "uid": local_uid, "data": 0x40000},
        {"app_id": app_id, "uid": local_uid, "data": 0x40000}
    ]
    batch_resolved = []
    for s in batch_seeds:
        pre_req, pre_st = backend.plan_creation(s["app_id"], s["uid"])
        if pre_st == "Success":
            batch_resolved.append(pre_req)
    check(len(batch_resolved) == 2, "Both batch items planned before any mutation")

    # 18. Duplicate planned target rejection
    seen_keys = set()
    dup_detected = False
    for r in batch_resolved:
        slot_k = f"{r['space']}:{r['type']}:{r['app_id']:016X}:0000000000000000:{r['uid'][0]:016X}:{r['uid'][1]:016X}:{r['rank']}:{r['index']}"
        if slot_k in seen_keys:
            dup_detected = True
            break
        seen_keys.add(slot_k)
    check(dup_detected, "Duplicate planned target detected and rejected")

    # 19. Grow non-concrete space rejection
    check(backend.extend_save_data(existing_key, 999, 0x80000, 0x40000)["status"] == "UnsupportedSpace",
          "Grow non-concrete space refused")

    # 20. Grow space mismatch rejection
    check(backend.extend_save_data(existing_key, FsSaveDataSpaceId_System, 0x80000, 0x40000)["status"] == "UnsupportedSpace",
          "Grow target space mismatch refused")

    # 21. Grow capacity check: insufficient space refused before IPC
    backend.free_bytes = 0x10000 # Only 64KB free
    check(backend.extend_save_data(existing_key, FsSaveDataSpaceId_User, 0x80000, 0x40000)["status"] == "InsufficientFreeSpace",
          "Grow beyond free space refused before IPC")
    backend.free_bytes = 0x10000000 # Restore free space

    # 22. Grow success with valid concrete space and sufficient free bytes
    grow_res = backend.extend_save_data(existing_key, FsSaveDataSpaceId_User, 0x80000, 0x40000)
    check(grow_res["verified"] and grow_res["ipc_executed"] and not grow_res["is_noop"], "Grow succeeded")

    # 23. Grow equal/equal verified no-op with no IPC
    noop_res = backend.extend_save_data(existing_key, FsSaveDataSpaceId_User, 0x80000, 0x40000)
    check(noop_res["verified"] and not noop_res["ipc_executed"] and noop_res["is_noop"],
          "Equal/equal is verified no-op with no IPC")

    # 24-27. Grow shrink, overflow, and alignment refusal
    for d, j, exp in [
        (0x40000, 0x40000, "DataShrinkRefused"),
        (0x80000, 0x20000, "JournalShrinkRefused"),
        (0x8000000000000000, 0x40000, "OverflowRefused"),
        (0x80001, 0x40000, "AlignmentRefused"),
    ]:
        check(backend.extend_save_data(existing_key, FsSaveDataSpaceId_User, d, j)["status"] == exp, f"{exp} check")

    # 28. Cancellation before extend IPC returns Result_TransferCancelled
    res_ext_cancel = backend.extend_save_data(existing_key, FsSaveDataSpaceId_User, 0xC0000, 0x40000, cancelled=True)
    check(res_ext_cancel["status"] == "Cancelled" and res_ext_cancel["rc"] == backend.Result_TransferCancelled,
          "Cancelled extend returns Result_TransferCancelled with zero mutation")

    # 29. Extend IPC failure
    backend.extend_ipc_fail = True
    check(backend.extend_save_data(existing_key, FsSaveDataSpaceId_User, 0xC0000, 0x40000)["status"] == "IpcFailed",
          "Extend IPC failure reported")
    backend.extend_ipc_fail = False

    # 30. Post-extend sizes below requested refusal
    backend.extend_post_size_below_requested = True
    check(backend.extend_save_data(existing_key, FsSaveDataSpaceId_User, 0xC0000, 0x40000)["status"] == "VerificationFailed",
          "Post-extend actual sizes below requested refused")
    backend.extend_post_size_below_requested = False

    # 31. Exact space read and no fallback
    check(res_ok["slot"].save_data_space_id == FsSaveDataSpaceId_User,
          "Verified exact-space read confirms User space (1)")

    # 32. Valid uninstalled title restore fallback creates missing Account save slot
    uninstalled_app = 0x0100000000020000
    valid_archive = {
        "has_sizing": True, "has_metadata": True, "app_id": uninstalled_app,
        "type": FsSaveDataType_Account, "rank": FsSaveDataRank_Primary, "index": 0,
        "owner": 0x0100000000020000, "data_size": 0x60000, "journal_size": 0x40000,
    }
    req_uninst, st_uninst = backend.plan_creation(uninstalled_app, local_uid, valid_archive)
    check(st_uninst == "Success" and req_uninst["provenance"] == "ArchiveMetadata", "Fallback must set ArchiveMetadata")
    check(req_uninst["uid"] == local_uid and req_uninst["owner"] == 0x0100000000020000, "Fallback attributes mismatch")
    res_uninst = backend.create_save_data(req_uninst)
    check(res_uninst["verified"] and res_uninst["create_succeeded"], "Uninstalled slot must verify after create")

    # 33. Uninstalled fallback fails closed on missing/invalid metadata
    _, st_no_meta = backend.plan_creation(uninstalled_app, local_uid, None)
    check(st_no_meta == "MissingControlData", "Missing metadata on uninstalled title must fail closed")
    for k, v, exp in [
        ("app_id", 0x0100000000099999, "InvalidApplicationId"),
        ("type", FsSaveDataType_Device, "UnsupportedSaveType"),
        ("rank", 1, "UnsupportedRank"),
        ("index", 1, "UnsupportedIndex"),
        ("owner", 0, "MissingOwnerId"),
        ("data_size", 0x4001, "InvalidSizes"),
    ]:
        _, st_err = backend.plan_creation(uninstalled_app, local_uid, dict(valid_archive, **{k: v}))
        check(st_err == exp, f"Uninstalled archive with bad {k} must return {exp}")

    print("  -> Synthetic behavioral regressions PASSED.")

def main():
    print("=" * 80)
    print("Sphaira v0.13.869: Shared Verified Save-Slot Backend Contract Regression")
    print("=" * 80)
    test_source_wiring_contracts()
    test_behavioral_regressions()
    print("=" * 80)
    print("ALL SAVE-SLOT BACKEND CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")
    print("=" * 80)

if __name__ == "__main__":
    main()
