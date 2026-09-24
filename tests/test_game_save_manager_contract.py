#!/usr/bin/env python3
"""
Sphaira v0.13.870 — Game Tools save-slot manager compiler-free contract.
Validates static C++ route contracts, connected behavioral reference fixtures,
and i18n parity for authoritative save-slot discovery, creation, and growth.
"""

import json
import os
import sys

def check(condition: bool, message: str) -> None:
    if not condition:
        print(f"FAIL: {message}")
        sys.exit(1)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def read_file(*parts: str) -> str:
    path = os.path.join(REPO_ROOT, *parts)
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()

# ==============================================================================
# 1. Static Source Wiring & C++ Route Contracts
# ==============================================================================

def test_source_wiring_contracts() -> None:
    print("[1] Checking static source wiring and route contracts...")

    cmake_src = read_file("sphaira", "CMakeLists.txt")
    check("set(sphaira_VERSION 0.13.870)" in cmake_src or "set(sphaira_VERSION 0.13.871)" in cmake_src or "set(sphaira_VERSION 0.13.872)" in cmake_src or "set(sphaira_VERSION 0.13.873)" in cmake_src or "set(sphaira_VERSION 0.13.874)" in cmake_src or "set(sphaira_VERSION 0.13.875)" in cmake_src or "set(sphaira_VERSION 0.13.876)" in cmake_src or "set(sphaira_VERSION 0.13.877)" in cmake_src or "set(sphaira_VERSION 0.13.878)" in cmake_src, "CMakeLists.txt must define sphaira_VERSION as 0.13.870, 0.13.871, 0.13.872, 0.13.873, 0.13.874, 0.13.875, 0.13.876, 0.13.877, or 0.13.878")
    check("source/ui/menus/game/game_save_manager.cpp" in cmake_src, "CMakeLists.txt must compile game_save_manager.cpp")

    hdr_src = read_file("sphaira", "include", "ui", "menus", "game", "game_save_manager.hpp")
    for symbol in ["struct GameSaveRow {", "FsSaveDataInfo info{};", "FsSaveDataExtraData extra{};",
                   "bool has_extra{false};", "void LoadGameSaves(", "void PromptCreateSaveSlot(", "void PromptIncreaseSaveSize("]:
        check(symbol in hdr_src, f"game_save_manager.hpp missing required symbol: {symbol}")

    mgr_src = read_file("sphaira", "source", "ui", "menus", "game", "game_save_manager.cpp")
    check("using grid::FormatBytes;" in mgr_src, "game_save_manager.cpp must bring FormatBytes into scope")
    for req_call in ["save::DiscoverSaveDataInfo(nullptr, std::nullopt)",
                     "fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(",
                     "save::PlanAccountSaveCreation(", "save::CreateSaveDataChecked(",
                     "save::ExtendSaveDataChecked(", "save::GetBackendStatusMessage("]:
        check(req_call in mgr_src, f"game_save_manager.cpp missing required backend call: {req_call}")

    # Authoritative identity retention in grow request
    check("grow_req.target_info = row.info;" in mgr_src, "SaveGrowRequest must retain full discovered row.info")
    check("grow_req.space_id = static_cast<FsSaveDataSpaceId>(row.info.save_data_space_id);" in mgr_src,
          "SaveGrowRequest must use row.info.save_data_space_id")
    check("if (!row.has_extra)" in mgr_src, "PromptIncreaseSaveSize must refuse locally if extra data was not readable")
    check("user_items.emplace_back(acc.nickname);" in mgr_src, "User picker must populate items with nickname only")
    check('user_items.emplace_back(acc.uid' not in mgr_src, "User picker must never display raw AccountUid")

    # Creation execution order: plan -> preset -> confirmation -> progress worker -> CreateSaveDataChecked
    p_plan = mgr_src.find("save::PlanAccountSaveCreation")
    p_preset = mgr_src.find("Select initial save size", p_plan)
    p_conf = mgr_src.find("Create save slot?", p_preset)
    p_worker = mgr_src.find("App::Push<ProgressBox>", p_conf)
    p_create = mgr_src.find("save::CreateSaveDataChecked", p_worker)
    check(all(p != -1 for p in [p_plan, p_preset, p_conf, p_worker, p_create]),
          "Create flow must follow plan -> preset -> confirmation -> ProgressBox -> CreateSaveDataChecked")

    # Legacy callers remain routed through shared backend
    legacy_ops = read_file("sphaira", "source", "ui", "menus", "game", "game_internal.cpp")
    check("save::PlanAccountSaveCreation(app_id, uid, nullptr, req, &status)" in legacy_ops,
          "Legacy CreateSave in game_internal.cpp must call PlanAccountSaveCreation")
    check("save::CreateSaveDataChecked(req)" in legacy_ops,
          "Legacy CreateSave in game_internal.cpp must call CreateSaveDataChecked")

    restore_route = read_file("sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    check("CreateSaveDataChecked(e.creation_request" in restore_route,
          "Restore-time slot creation must route through CreateSaveDataChecked")

    # game_details.cpp integration and localized strings
    details_src = read_file("sphaira", "source", "ui", "menus", "game", "game_details.cpp")
    check("fsOpenSaveDataInfoReader" not in details_src, "game_details.cpp must not contain local FsSaveDataInfoReader")
    check("LoadGameSaves(app_id, m_saves, m_save_allocated_size);" in details_src,
          "game_details.cpp LoadSaves must delegate to LoadGameSaves")
    check("Create save slot" in details_src and "Increase save size" in details_src,
          "game_details.cpp must expose Create and Increase save actions")
    check('"rk:%u' not in details_src, "game_details.cpp must not contain raw unlocalized 'rk:%u'")
    check('"Save ID %016lX"' not in details_src, "game_details.cpp must not contain raw unlocalized 'Save ID %016lX'")
    check('("Rank: %s · Index: %u"_i18n).c_str()' in details_src, "game_details.cpp must use localized Rank/Index template")
    check('("Save ID: %016lX"_i18n).c_str()' in details_src, "game_details.cpp must use localized Save ID template")

    print("  -> Static source wiring contracts PASSED.")

# ==============================================================================
# 2. Localization & Key Parity Contracts
# ==============================================================================

def test_localization_parity() -> None:
    print("[2] Checking localization key parity and save manager strings...")
    en_path = os.path.join(REPO_ROOT, "assets", "romfs", "i18n", "en.json")
    uk_path = os.path.join(REPO_ROOT, "assets", "romfs", "i18n", "uk.json")
    with open(en_path, "r", encoding="utf-8") as f:
        en_dict = json.load(f)
    with open(uk_path, "r", encoding="utf-8") as f:
        uk_dict = json.load(f)

    check(set(en_dict.keys()) == set(uk_dict.keys()),
          f"EN and UK must have exact key parity ({len(en_dict)} vs {len(uk_dict)})")

    required_keys = [
        "Create save slot", "Increase save size", "Create a new save data slot for a local user.",
        "Increase the allocated data size for this save slot.", "No local users found on console.",
        "A save slot for this user already exists.", "Save slot creation contract unsupported.",
        "Select initial save size", "Select new save size", "Default (%s)", "+16 MiB (%s)", "+64 MiB (%s)",
        "Create save slot?", "Increase save size?", "Increase", "Current data size: ", "New data size: ",
        "Current journal size: ", "New journal size: ", "Primary", "Secondary", "Rank: ", "Index: ",
        "Save ID: ", "Allocated: ", "Creating save slot...", "Increasing save size...",
        "Save slot created successfully.", "Save size increased successfully.",
        "Cannot increase save size: extra data unreadable.", "Preset size cannot be represented safely.",
        "Save size increase would not change size.", "No save data found for this title",
        "Extra data unreadable (0x%X)", "Allocated: %s · Data: %s · Journal: %s",
        "Allocated: %s · Extra data unreadable (0x%X)", "Rank: %s · Index: %u", "Save ID: %016lX",
        "SD System", "Temporary", "SD User", "Proper System", "Safe Mode"
    ]
    for k in required_keys:
        check(k in en_dict, f"Key '{k}' missing from en.json")
        check(k in uk_dict, f"Key '{k}' missing from uk.json")
        check(len(uk_dict[k].strip()) > 0, f"Key '{k}' has empty translation in uk.json")

    print(f"  -> Localization key parity PASSED ({len(en_dict)} keys).")

# ==============================================================================
# 3. Behavioral Simulation: Save Slot Discovery & Extra Data
# ==============================================================================

class MockSlot:
    def __init__(self, app_id, space_id, save_type, rank, index, save_id, size, uid):
        self.application_id, self.save_data_space_id = app_id, space_id
        self.save_data_type, self.save_data_rank = save_type, rank
        self.save_data_index, self.save_data_id = index, save_id
        self.size, self.uid = size, uid

class MockExtra:
    def __init__(self, data_size, journal_size):
        self.data_size, self.journal_size = data_size, journal_size

class MockSaveInventory:
    def __init__(self):
        self.slots, self.extra_data = [], {}

    def add_slot(self, slot: MockSlot, extra_rc: int, extra: MockExtra = None):
        self.slots.append(slot)
        self.extra_data[(slot.save_data_space_id, slot.save_data_id)] = (extra_rc, extra)

    def load_game_saves(self, target_app_id: int):
        out = []
        for s in self.slots:
            if s.application_id != target_app_id:
                continue
            rc, extra = self.extra_data.get((s.save_data_space_id, s.save_data_id), (0x1234, None))
            out.append({"info": s, "extra": extra, "extra_rc": rc, "has_extra": (rc == 0 and extra is not None)})
        return out

def test_inventory_behavior() -> None:
    print("[3] Running authoritative inventory behavioral simulation...")
    inv = MockSaveInventory()
    inv.add_slot(MockSlot(0x0100000000010000, 1, 1, 0, 0, 0x101, 0x100000, (1, 0)), 0, MockExtra(0x40000, 0x20000))
    inv.add_slot(MockSlot(0x0100000000010000, 1, 1, 0, 0, 0x102, 0x100000, (2, 0)), 0x00200202, None)
    inv.add_slot(MockSlot(0x0100000000099999, 1, 1, 0, 0, 0x103, 0x100000, (1, 0)), 0, MockExtra(0x40000, 0x20000))

    loaded = inv.load_game_saves(0x0100000000010000)
    check(len(loaded) == 2, f"Should discover exactly 2 slots for target app, got {len(loaded)}")
    check(loaded[0]["has_extra"] is True and loaded[0]["extra"].data_size == 0x40000, "Slot 1 extra mismatch")
    check(loaded[1]["has_extra"] is False and loaded[1]["extra_rc"] == 0x00200202, "Slot 2 error code mismatch")
    print("  -> Authoritative inventory behavioral simulation PASSED.")

# ==============================================================================
# 4. Behavioral Simulation: Checked Backend & Live Extra Data Comparison
# ==============================================================================

class MockSaveBackend:
    ALIGNMENT = 0x4000
    INT64_MAX = 0x7FFFFFFFFFFFFFFF

    def __init__(self):
        self.ipc_log = []

    def plan_account_save_creation(self, app_id, uid, nacp_data=0x40000, nacp_journal=0x40000):
        if app_id == 0 or uid == (0, 0):
            return 0x0001, "InvalidParams", None
        return 0, "Success", {
            "application_id": app_id, "uid": uid, "space_id": 1, "save_data_type": 1,
            "save_data_rank": 0, "save_data_index": 0, "data_size": nacp_data, "journal_size": nacp_journal
        }

    def compute_presets(self, current_data_size):
        presets = [("Default", current_data_size)]
        for delta, label in [(16 * 1024 * 1024, "+16 MiB"), (64 * 1024 * 1024, "+64 MiB")]:
            if current_data_size <= self.INT64_MAX - delta:
                val = current_data_size + delta
                if val % self.ALIGNMENT == 0 and val > current_data_size:
                    presets.append((label, val))
        return presets

    def create_save_data_checked(self, req, fail_ipc=False, fail_verify=False):
        self.ipc_log.append(("create", req))
        if fail_ipc:
            return {"status": "IpcFailed", "rc": 0x00200204, "ipc_executed": True, "verified": False}
        if fail_verify:
            return {"status": "VerificationFailed", "rc": 0x00200205, "ipc_executed": True, "verified": False}
        return {"status": "Success", "rc": 0, "ipc_executed": True, "verified": True,
                "verified_extra": MockExtra(req["data_size"], req["journal_size"])}

    def extend_save_data_checked(self, req, live_extra: MockExtra, fail_ipc=False, fail_verify=False):
        req_d, req_j = req["requested_data_size"], req["requested_journal_size"]
        if req_d < live_extra.data_size or req_j < live_extra.journal_size:
            return {"status": "GrowWouldShrink", "rc": 0x00200206, "ipc_executed": False, "verified": False}
        if req_d == live_extra.data_size and req_j == live_extra.journal_size:
            return {"status": "GrowNoOp", "rc": 0x00200207, "ipc_executed": False, "verified": False}

        self.ipc_log.append(("extend", req))
        if fail_ipc:
            return {"status": "IpcFailed", "rc": 0x00200208, "ipc_executed": True, "verified": False}
        if fail_verify:
            return {"status": "VerificationFailed", "rc": 0x00200209, "ipc_executed": True, "verified": False}
        return {"status": "Success", "rc": 0, "ipc_executed": True, "verified": True,
                "actual_data_size": req_d, "actual_journal_size": req_j}

# ==============================================================================
# 5. UI State Machine Fixtures: Creation & Growth Flows
# ==============================================================================

class CreationUIFlow:
    def __init__(self, backend: MockSaveBackend, app_id: int, users: list, existing_saves: list):
        self.backend, self.app_id, self.users, self.existing_saves = backend, app_id, users, existing_saves
        self.state, self.selected_user, self.plan_request = "IDLE", None, None
        self.worker_launched, self.backend_called, self.result = False, 0, None
        self.error_message, self.refreshed, self.toast_shown = None, False, False

    def open_menu(self) -> bool:
        if not self.users:
            self.state, self.error_message = "ABORTED", "No local users found on console."
            return False
        self.state = "USER_PICKER"
        return True

    def select_user(self, user, cancel=False) -> bool:
        if cancel:
            self.state = "CANCELLED"
            return False
        if any(r["info"].uid == user["uid"] and r["info"].save_data_type == 1 for r in self.existing_saves):
            self.state, self.error_message = "ABORTED", "A save slot for this user already exists."
            return False
        rc, _, req = self.backend.plan_account_save_creation(self.app_id, user["uid"])
        if rc != 0:
            self.state, self.error_message = "ABORTED", "Save slot creation contract unsupported."
            return False
        self.selected_user, self.plan_request, self.state = user, req, "PRESET_PICKER"
        return True

    def select_preset(self, preset_size: int, cancel=False) -> bool:
        if cancel:
            self.state = "CANCELLED"
            return False
        self.plan_request["data_size"] = preset_size
        self.state = "CONFIRMATION_MODAL"
        return True

    def confirm(self, accepted=True, fail_ipc=False, fail_verify=False) -> bool:
        if not accepted:
            self.state = "CANCELLED"
            return False
        self.state, self.worker_launched = "WORKER_ACTIVE", True
        self.backend_called += 1
        res = self.backend.create_save_data_checked(self.plan_request, fail_ipc=fail_ipc, fail_verify=fail_verify)
        self.result, self.state = res, "COMPLETED"
        self.refreshed = (res["verified"] or res["ipc_executed"])
        self.toast_shown = res["verified"]
        return res["verified"]

class GrowUIFlow:
    def __init__(self, backend: MockSaveBackend, row: dict):
        self.backend, self.row, self.state = backend, row, "IDLE"
        self.presets, self.grow_request = [], None
        self.worker_launched, self.backend_called, self.result = False, 0, None
        self.error_message, self.refreshed, self.toast_shown = None, False, False

    def open_menu(self) -> bool:
        if not self.row.get("has_extra", False) or self.row.get("extra") is None:
            self.state, self.error_message = "ABORTED", "Cannot increase save size: extra data unreadable."
            return False
        presets = self.backend.compute_presets(self.row["extra"].data_size)
        if len(presets) <= 1:
            self.state, self.error_message = "ABORTED", "Preset size cannot be represented safely."
            return False
        self.presets, self.state = presets, "PRESET_PICKER"
        return True

    def select_preset(self, new_data_size: int, cancel=False) -> bool:
        if cancel:
            self.state = "CANCELLED"
            return False
        cur_data = self.row["extra"].data_size
        if new_data_size <= cur_data:
            self.state, self.error_message = "ABORTED", "Save size increase would not change size."
            return False
        self.grow_request = {
            "target_info": self.row["info"], "space_id": self.row["info"].save_data_space_id,
            "requested_data_size": new_data_size, "requested_journal_size": self.row["extra"].journal_size
        }
        self.state = "CONFIRMATION_MODAL"
        return True

    def confirm(self, accepted=True, fail_ipc=False, fail_verify=False) -> bool:
        if not accepted:
            self.state = "CANCELLED"
            return False
        self.state, self.worker_launched = "WORKER_ACTIVE", True
        self.backend_called += 1
        res = self.backend.extend_save_data_checked(self.grow_request, self.row["extra"], fail_ipc=fail_ipc, fail_verify=fail_verify)
        self.result, self.state = res, "COMPLETED"
        self.refreshed = (res["verified"] or res["ipc_executed"])
        self.toast_shown = res["verified"]
        return res["verified"]

# ==============================================================================
# 6. Test Runner for Creation & Growth State Transitions
# ==============================================================================

def test_creation_flow() -> None:
    print("[4] Running creation UI flow & state transition validation...")
    backend = MockSaveBackend()
    users = [{"uid": (1, 1), "nickname": "User1"}, {"uid": (2, 2), "nickname": "User2"}]
    existing = [{"info": MockSlot(0x0100000000010000, 1, 1, 0, 0, 1, 0x40000, (1, 1))}]

    # 1. Cancelled at user picker: no worker, no backend call, no refresh
    flow1 = CreationUIFlow(backend, 0x0100000000010000, users, existing)
    flow1.open_menu()
    flow1.select_user(users[1], cancel=True)
    check(flow1.state == "CANCELLED" and not flow1.worker_launched and flow1.backend_called == 0,
          "Cancelled user picker must not launch worker")
    check(not flow1.refreshed, "Cancelled user picker must not refresh inventory")

    # 2. Duplicate user: blocked before preset/worker
    flow2 = CreationUIFlow(backend, 0x0100000000010000, users, existing)
    flow2.open_menu()
    ok = flow2.select_user(users[0])
    check(not ok and flow2.state == "ABORTED" and flow2.error_message == "A save slot for this user already exists.",
          "Duplicate user must abort")
    check(not flow2.worker_launched and flow2.backend_called == 0, "Duplicate check must not launch worker")

    # 3. Cancelled at preset picker: no worker, no backend call, no refresh
    flow3 = CreationUIFlow(backend, 0x0100000000010000, users, existing)
    flow3.open_menu()
    flow3.select_user(users[1])
    flow3.select_preset(0x40000, cancel=True)
    check(flow3.state == "CANCELLED" and not flow3.worker_launched and flow3.backend_called == 0,
          "Cancelled preset must not launch worker")
    check(not flow3.refreshed, "Cancelled preset must not refresh inventory")

    # 4. Cancelled at confirmation gate: no worker, no backend call, no refresh
    flow4 = CreationUIFlow(backend, 0x0100000000010000, users, existing)
    flow4.open_menu()
    flow4.select_user(users[1])
    flow4.select_preset(0x40000)
    check(flow4.state == "CONFIRMATION_MODAL", "Must be at confirmation modal")
    flow4.confirm(accepted=False)
    check(flow4.state == "CANCELLED" and not flow4.worker_launched and flow4.backend_called == 0,
          "Declined confirmation must not launch worker")
    check(not flow4.refreshed, "Declined confirmation must not refresh inventory")

    # 5. Accepted confirmation -> worker runs backend once -> success & refresh
    flow5 = CreationUIFlow(backend, 0x0100000000010000, users, existing)
    flow5.open_menu()
    flow5.select_user(users[1])
    flow5.select_preset(0x40000 + 16*1024*1024)
    flow5.confirm(accepted=True)
    check(flow5.state == "COMPLETED" and flow5.worker_launched and flow5.backend_called == 1,
          "Worker must complete with exactly 1 backend call")
    check(flow5.refreshed is True and flow5.toast_shown is True, "Success must refresh and show toast")

    # 6. Post-IPC verification failure -> refreshed is True, error shown
    flow6 = CreationUIFlow(backend, 0x0100000000010000, users, existing)
    flow6.open_menu()
    flow6.select_user(users[1])
    flow6.select_preset(0x40000)
    flow6.confirm(accepted=True, fail_verify=True)
    check(flow6.worker_launched and flow6.backend_called == 1, "Worker must run once on failure")
    check(flow6.refreshed is True and flow6.toast_shown is False, "Post-IPC verify failure must refresh without toast")

    print("  -> Creation UI flow & state transitions PASSED.")

def test_grow_flow() -> None:
    print("[5] Running grow UI flow & state transition validation...")
    backend = MockSaveBackend()

    # 1. Unreadable extra data: aborted before picker/confirmation/worker
    bad_row = {"info": MockSlot(0x0100000000010000, 1, 1, 0, 0, 5, 0x40000, (1, 1)), "extra": None, "has_extra": False}
    flow1 = GrowUIFlow(backend, bad_row)
    ok = flow1.open_menu()
    check(not ok and flow1.state == "ABORTED" and flow1.error_message == "Cannot increase save size: extra data unreadable.",
          "Unreadable extra data must abort")
    check(not flow1.worker_launched and flow1.backend_called == 0, "Aborted grow must not launch worker")

    # Valid slot for subsequent tests
    target_slot = MockSlot(0x0100000000010000, 1, 1, 0, 0, 10, 0x200000, (1, 1))
    target_extra = MockExtra(0x40000, 0x20000)
    valid_row = {"info": target_slot, "extra": target_extra, "has_extra": True}

    # 2. Cancelled at preset picker: no worker, no backend call, no refresh
    flow2 = GrowUIFlow(backend, valid_row)
    flow2.open_menu()
    check(len(flow2.presets) == 3, f"Must provide 3 presets, got {len(flow2.presets)}")
    flow2.select_preset(flow2.presets[1][1], cancel=True)
    check(flow2.state == "CANCELLED" and not flow2.worker_launched and flow2.backend_called == 0,
          "Cancelled preset must not launch worker")
    check(not flow2.refreshed, "Cancelled preset must not refresh inventory")

    # 3. Cancelled at confirmation gate: no worker, no backend call, no refresh
    flow3 = GrowUIFlow(backend, valid_row)
    flow3.open_menu()
    p16 = flow3.presets[1][1]
    flow3.select_preset(p16)
    check(flow3.state == "CONFIRMATION_MODAL", "Must reach confirmation modal")
    check(flow3.grow_request["target_info"] is target_slot, "SaveGrowRequest must retain exact target_info object identity")
    check(flow3.grow_request["space_id"] == target_slot.save_data_space_id, "SaveGrowRequest must retain exact space_id")
    flow3.confirm(accepted=False)
    check(flow3.state == "CANCELLED" and not flow3.worker_launched and flow3.backend_called == 0,
          "Declined confirmation must not launch worker")
    check(not flow3.refreshed, "Declined confirmation must not refresh inventory")

    # 4. Accepted confirmation -> worker runs backend once -> success & refresh
    flow4 = GrowUIFlow(backend, valid_row)
    flow4.open_menu()
    flow4.select_preset(p16)
    flow4.confirm(accepted=True)
    check(flow4.state == "COMPLETED" and flow4.worker_launched and flow4.backend_called == 1,
          "Worker must complete with exactly 1 backend call")
    check(flow4.refreshed is True and flow4.toast_shown is True, "Success must refresh and show toast")

    # 5. Live extra data shrink check: grow checked against live extra data, NOT target_info.size
    shrink_req = {"target_info": target_slot, "space_id": target_slot.save_data_space_id,
                  "requested_data_size": 0x30000, "requested_journal_size": 0x20000}
    shrink_res = backend.extend_save_data_checked(shrink_req, target_extra)
    check(shrink_res["status"] == "GrowWouldShrink" and shrink_res["ipc_executed"] is False,
          "Shrink against live extra must be rejected without IPC")

    noop_req = {"target_info": target_slot, "space_id": target_slot.save_data_space_id,
                "requested_data_size": 0x40000, "requested_journal_size": 0x20000}
    noop_res = backend.extend_save_data_checked(noop_req, target_extra)
    check(noop_res["status"] == "GrowNoOp" and noop_res["ipc_executed"] is False,
          "No-op against live extra must be rejected without IPC")

    # Request larger than live extra (0x100000 > 0x40000) even if smaller than container size (0x200000)
    larger_req = {"target_info": target_slot, "space_id": target_slot.save_data_space_id,
                  "requested_data_size": 0x100000, "requested_journal_size": 0x20000}
    grow_res = backend.extend_save_data_checked(larger_req, target_extra)
    check(grow_res["status"] == "Success" and grow_res["ipc_executed"] is True,
          "Growth compared to live extra must succeed with IPC")

    print("  -> Grow UI flow & state transitions PASSED.")

# ==============================================================================
# Main Runner
# ==============================================================================

def main() -> None:
    print("=" * 80)
    print("Sphaira v0.13.870: Game Tools Save-Slot Manager Contract & Regressions")
    print("=" * 80)
    test_source_wiring_contracts()
    test_localization_parity()
    test_inventory_behavior()
    test_creation_flow()
    test_grow_flow()
    print("=" * 80)
    print("ALL SAVE-SLOT MANAGER CONTRACT CHECKS PASSED (5/5 groups).")
    print("=" * 80)

if __name__ == "__main__":
    main()
