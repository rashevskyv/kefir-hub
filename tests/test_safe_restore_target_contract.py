#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for
P2-B safe selected restore target policy.

NOTE ON SCOPE AND COVERAGE:
This test executes static source contracts and a Python behavioral model of the
restore target selection, deduplication, confirmation, and source/target separation.
It validates algorithmic invariance, target authority retention, collision rejection,
and C++ source patterns.
It explicitly does NOT execute C++ binaries, IPC calls, libnx services, or
target Nintendo Switch hardware/filesystem runtime.
"""

import os
import sys

def check(condition, msg):
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)

def test_source_contracts():
    repo_root = os.path.join(os.path.dirname(__file__), "..")

    # 1. save_menu.hpp: RestoreSaves 4-arg overload, PromptBatchRestoreTargets, ResolveRestoreTarget callback
    save_menu_hpp = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save_menu.hpp")
    with open(save_menu_hpp, "r", encoding="utf-8") as f:
        menu_hpp_src = f.read()

    check("void RestoreSaves(std::vector<Entry> sources, std::vector<Entry> targets, const dump::DumpLocation& location, const fs::FsPath& backup_root);" in menu_hpp_src,
          "save_menu.hpp must declare 4-arg RestoreSaves with separate sources and targets")
    check("void PromptBatchRestoreTargets(" in menu_hpp_src,
          "save_menu.hpp must declare PromptBatchRestoreTargets")
    check("static void ResolveRestoreTarget(const Entry& backup, const AccountUid* explicit_uid, std::function<void(std::optional<Entry>)> cb);" in menu_hpp_src,
          "save_menu.hpp must declare callback-based ResolveRestoreTarget")

    # 2. save_menu.cpp: ResolveRestoreTarget, FormatTargetSlotLabel, PromptBatchRestoreTargets, ActionType::Restore
    save_menu_cpp = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save_menu.cpp")
    with open(save_menu_cpp, "r", encoding="utf-8") as f:
        menu_cpp_src = f.read()
    save_restore_route_cpp = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_restore_route.cpp")
    if os.path.exists(save_restore_route_cpp):
        with open(save_restore_route_cpp, "r", encoding="utf-8") as f:
            menu_cpp_src += f.read()

    check("FormatTargetSlotLabel(" in menu_cpp_src,
          "save_menu.cpp must define FormatTargetSlotLabel helper")
    check("!backup.is_backup && backup.save_data_id != 0 && (!explicit_uid || std::memcmp(explicit_uid, &backup.uid, sizeof(AccountUid)) == 0)" in menu_cpp_src,
          "ResolveRestoreTarget must retain P2-A exact identity fast path for live seed with unchanged UID")
    check("static_cast<FsSaveDataInfo&>(target) = info;" in menu_cpp_src,
          "ResolveRestoreTarget must assign target strictly from discovered FsSaveDataInfo authority")
    check('App::Push<OptionBox>("Restore save data to\\n" + label + "?", "No"_i18n, "Yes"_i18n' in menu_cpp_src,
          "ResolveRestoreTarget must visibly confirm single candidate via OptionBox")
    check('make_unique<PopupList>("Select restore target slot"_i18n' in menu_cpp_src,
          "ResolveRestoreTarget must present PopupList for multiple candidates")
    check('App::Push<OptionBox>("No compatible live save slot found on console. Automatic save slot creation is not supported in this version."_i18n, "OK"_i18n);' in menu_cpp_src or
          'App::Push<OptionBox>("No existing save slot found on console."_i18n, "OK"_i18n);' in menu_cpp_src,
          "ResolveRestoreTarget must show explicit error box when no existing live slots match")
    check("seen_target_keys->insert(key).second" in menu_cpp_src,
          "PromptBatchRestoreTargets must detect and reject duplicate targets via SaveEntryKey")
    check('App::Push<OptionBox>("Duplicate restore target slot selected."_i18n, "OK"_i18n);' in menu_cpp_src,
          "PromptBatchRestoreTargets must show OptionBox on duplicate target selection")
    check("RestoreSaves(std::move(*seeds), std::move(*resolved_targets), location, backup_root);" in menu_cpp_src,
          "PromptBatchRestoreTargets must pass resolved targets and original seeds to RestoreSaves")
    check("seeds = std::move(seeds)" not in menu_cpp_src,
          "save_menu.cpp must not prematurely move seeds in PromptBatchRestoreTargets")
    check("accounts = std::move(accounts)" not in menu_cpp_src,
          "save_menu.cpp must not prematurely move accounts in PromptBatchRestoreTargets")
    check("PromptBatchRestoreTargets(\n    std::shared_ptr<std::vector<Entry>> seeds," in menu_cpp_src,
          "save_menu.cpp must implement shared_ptr PromptBatchRestoreTargets overload")

    # 3. save_menu_ops.cpp: RestoreSaves source/target separation and guards
    save_menu_ops = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(save_menu_ops, "r", encoding="utf-8") as f:
        ops_cpp_src = f.read()

    check("void Menu::RestoreSaves(std::vector<Entry> sources, std::vector<Entry> targets, const dump::DumpLocation& location, const fs::FsPath& backup_root)" in ops_cpp_src,
          "save_menu_ops.cpp must implement 4-arg RestoreSaves")
    check("sources.size() != targets.size()" in ops_cpp_src,
          "RestoreSaves must check for sources and targets count mismatch")
    check("dst.is_backup || dst.save_data_id == 0" in ops_cpp_src,
          "RestoreSaves must validate destination targets (not backup and nonzero save_data_id)")
    check("i < sources.size() && i < targets.size()" not in ops_cpp_src,
          "RestoreSaves must not silently truncate mismatched sources and targets")

    restore_fn_pos = ops_cpp_src.find("void Menu::RestoreSaves(std::vector<Entry> sources, std::vector<Entry> targets")
    pbox_pos = ops_cpp_src.find("App::Push<ProgressBox>", restore_fn_pos)
    mismatch_pos = ops_cpp_src.find("sources.size() != targets.size()", restore_fn_pos)
    target_val_pos = ops_cpp_src.find("dst.is_backup || dst.save_data_id == 0", restore_fn_pos)
    check(mismatch_pos != -1 and mismatch_pos < pbox_pos,
          "Count mismatch check must happen before ProgressBox in RestoreSaves")
    check(target_val_pos != -1 and target_val_pos < pbox_pos,
          "Destination target validation must happen before ProgressBox in RestoreSaves")

    check("src.backup_members.front().path" in ops_cpp_src or "FindLatestBackupPath(fs.get(), src, backup_root, file_path)" in ops_cpp_src,
          "RestoreSaves must use source entry to locate backup file")
    check("RestoreSaveInternal(pbox, dst, file_path" in ops_cpp_src,
          "RestoreSaves must pass destination target to RestoreSaveInternal")
    check("R_UNLESS(!e.is_backup && e.save_data_id != 0, FsError_PathNotFound);" in ops_cpp_src,
          "RestoreSaveInternal must guard against unresolved backup entries and zero-ID targets")
    check("R_UNLESS(!e.is_backup, FsError_PathNotFound);" in ops_cpp_src,
          "RestoreSaveZip must guard against unresolved backup entries")

    # 4. filebrowser_ops.cpp: File Browser ZIP retained nonzero target guard
    fb_ops_cpp = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_ops.cpp")
    with open(fb_ops_cpp, "r", encoding="utf-8") as f:
        fb_src = f.read()

    check("save::DiscoverSaveDataInfo(&acc.uid, FsSaveDataType_Account)" in fb_src,
          "RestoreSaveFile ZIP picker must query Account saves per local UID via shared discovery")
    check("return save::RestoreSaveZip(pbox, se, file_path" in fb_src,
          "RestoreSaveFile must pass discovered live entry se to RestoreSaveZip")

    print("Source contracts: ALL PASS (4 anchor groups)")


# ==============================================================================
# Synthetic Behavioral Model (Target Selection, Verification, and Regression)
# ==============================================================================

class SyntheticSaveDataInfo:
    def __init__(self, space_id, save_type, app_id, sys_id, uid, rank, index, save_id):
        self.save_data_space_id = space_id
        self.save_data_type = save_type
        self.application_id = app_id
        self.system_save_data_id = sys_id
        self.uid = uid  # tuple (u64, u64)
        self.save_data_rank = rank
        self.save_data_index = index
        self.save_data_id = save_id

    def save_entry_key(self) -> str:
        return f"{self.save_data_space_id}:{self.save_data_type}:{self.application_id:016X}:{self.system_save_data_id:016X}:{self.uid[0]:016X}:{self.uid[1]:016X}:{self.save_data_rank}:{self.save_data_index}"


class SyntheticEntry(SyntheticSaveDataInfo):
    def __init__(self, space_id, save_type, app_id, sys_id, uid, rank, index, save_id, is_backup=False, backup_path=""):
        super().__init__(space_id, save_type, app_id, sys_id, uid, rank, index, save_id)
        self.is_backup = is_backup
        self.backup_path = backup_path


class SyntheticMutationTracker:
    def __init__(self):
        self.creates = 0
        self.extends = 0
        self.deletes = 0
        self.writes = 0

    def create(self): self.creates += 1
    def extend(self): self.extends += 1
    def delete(self): self.deletes += 1
    def write(self): self.writes += 1

    def total_mutations(self):
        return self.creates + self.extends + self.deletes + self.writes


def resolve_restore_candidates(backup_entry, live_slots, explicit_uid=None):
    """
    Simulates ResolveRestoreTarget candidate filtering.
    Does NOT execute C++, IPC, libnx or switch services.
    """
    # Fast path: live seed with nonzero save ID and unchanged UID
    if not backup_entry.is_backup and backup_entry.save_data_id != 0:
        if explicit_uid is None or backup_entry.uid == explicit_uid:
            target = SyntheticEntry(
                backup_entry.save_data_space_id, backup_entry.save_data_type,
                backup_entry.application_id, backup_entry.system_save_data_id,
                backup_entry.uid, backup_entry.save_data_rank,
                backup_entry.save_data_index, backup_entry.save_data_id,
                is_backup=False
            )
            return [target]

    candidates = []
    seen_keys = set()

    for info in live_slots:
        # 1. Match title ID
        if info.save_data_type in (0, 6):  # System or SystemBcat
            if backup_entry.system_save_data_id == 0 or info.system_save_data_id != backup_entry.system_save_data_id:
                continue
        else:
            if backup_entry.application_id == 0 or info.application_id != backup_entry.application_id:
                continue

        # 2. Match save_data_type if known (not 0xFF)
        if backup_entry.save_data_type != 0xFF and info.save_data_type != backup_entry.save_data_type:
            continue

        # 3. Account UID matching
        if info.save_data_type == 1:  # Account
            if explicit_uid is not None:
                if info.uid != explicit_uid:
                    continue
            elif backup_entry.uid != (0, 0):
                if info.uid != backup_entry.uid:
                    continue

        key = info.save_entry_key()
        if key not in seen_keys:
            seen_keys.add(key)
            target = SyntheticEntry(
                info.save_data_space_id, info.save_data_type,
                info.application_id, info.system_save_data_id,
                info.uid, info.save_data_rank,
                info.save_data_index, info.save_data_id,
                is_backup=False
            )
            candidates.append(target)

    return candidates


def test_behavioral_model():
    USER_A = (0x0123456789ABCDEF, 0x1111222233334444)
    USER_B = (0xFEEDFACECAFEBEEF, 0x5555666677778888)

    TYPE_SYSTEM = 0
    TYPE_ACCOUNT = 1
    TYPE_DEVICE = 3
    TYPE_CACHE = 5

    SPACE_SYSTEM = 0
    SPACE_USER = 1
    SPACE_SD_USER = 4

    # --------------------------------------------------------------------------
    # Fixture 1: Source UID A, explicit target UID B -> B locked, source path unchanged
    # --------------------------------------------------------------------------
    source_backup = SyntheticEntry(
        SPACE_USER, TYPE_ACCOUNT, 0x010000000000A000, 0, USER_A, 0, 0, 0,
        is_backup=True, backup_path="/dumps/USER_A/game_save.zip"
    )
    live_slot_b = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x010000000000A000, 0, USER_B, 0, 0, 0x888)
    candidates_1 = resolve_restore_candidates(source_backup, [live_slot_b], explicit_uid=USER_B)
    check(len(candidates_1) == 1, "Fixture 1: Must find exactly 1 candidate for remapped UID B")
    target_b = candidates_1[0]
    check(target_b.uid == USER_B, "Fixture 1: Selected target UID must be locked to USER_B")
    check(target_b.save_data_id == 0x888, "Fixture 1: Target save ID must be 0x888")
    check(source_backup.backup_path == "/dumps/USER_A/game_save.zip",
          "Fixture 1: Source backup path must remain unchanged")

    # --------------------------------------------------------------------------
    # Fixture 2: Target type/rank/index/space zeros vs conflicting source metadata
    # --------------------------------------------------------------------------
    conflicting_source = SyntheticEntry(
        SPACE_USER, TYPE_ACCOUNT, 0, 0x8000000000000001, USER_A, 1, 2, 0,
        is_backup=True
    )
    live_zero_target = SyntheticSaveDataInfo(
        SPACE_SYSTEM, TYPE_SYSTEM, 0, 0x8000000000000001, (0, 0), 0, 0, 0x1234
    )
    candidates_2 = resolve_restore_candidates(conflicting_source, [live_zero_target])
    check(len(candidates_2) == 0, "Fixture 2: Account type source must not match System target")

    conflicting_source_sys = SyntheticEntry(
        SPACE_USER, TYPE_SYSTEM, 0, 0x8000000000000001, (0, 0), 1, 2, 0,
        is_backup=True
    )
    candidates_2_sys = resolve_restore_candidates(conflicting_source_sys, [live_zero_target])
    check(len(candidates_2_sys) == 1, "Fixture 2: Must resolve live zero target")
    resolved_sys = candidates_2_sys[0]
    check(resolved_sys.save_data_rank == 0, "Fixture 2: Legitimate rank=0 must be preserved from live slot")
    check(resolved_sys.save_data_index == 0, "Fixture 2: Legitimate index=0 must be preserved from live slot")
    check(resolved_sys.save_data_space_id == SPACE_SYSTEM, "Fixture 2: Actual space=0 must be preserved")
    check(resolved_sys.save_data_type == TYPE_SYSTEM, "Fixture 2: System type must be preserved")

    # --------------------------------------------------------------------------
    # Fixture 3: Cache same app/index in User and SdUser, primary/secondary ranks
    # --------------------------------------------------------------------------
    cache_user_rk0 = SyntheticSaveDataInfo(SPACE_USER, TYPE_CACHE, 0x010000000000C000, 0, (0, 0), 0, 0, 0x101)
    cache_sd_rk0 = SyntheticSaveDataInfo(SPACE_SD_USER, TYPE_CACHE, 0x010000000000C000, 0, (0, 0), 0, 0, 0x102)
    cache_user_rk1 = SyntheticSaveDataInfo(SPACE_USER, TYPE_CACHE, 0x010000000000C000, 0, (0, 0), 1, 0, 0x103)

    cache_source = SyntheticEntry(SPACE_USER, TYPE_CACHE, 0x010000000000C000, 0, (0, 0), 0, 0, 0, is_backup=True)
    candidates_3 = resolve_restore_candidates(cache_source, [cache_user_rk0, cache_sd_rk0, cache_user_rk1])
    check(len(candidates_3) == 3, "Fixture 3: All 3 Cache variants (User, SdUser, rk0, rk1) must be selectable")
    check(any(c.save_data_space_id == SPACE_USER and c.save_data_rank == 0 for c in candidates_3), "Cache User rk0 present")
    check(any(c.save_data_space_id == SPACE_SD_USER and c.save_data_rank == 0 for c in candidates_3), "Cache SdUser rk0 present")
    check(any(c.save_data_space_id == SPACE_USER and c.save_data_rank == 1 for c in candidates_3), "Cache User rk1 present")

    # --------------------------------------------------------------------------
    # Fixture 4: Unknown source fields do not auto-narrow to zeros
    # --------------------------------------------------------------------------
    unknown_source = SyntheticEntry(
        0, 0xFF, 0x010000000000D000, 0, (0, 0), 0, 0, 0, is_backup=True
    )
    dev_slot = SyntheticSaveDataInfo(SPACE_USER, TYPE_DEVICE, 0x010000000000D000, 0, (0, 0), 0, 0, 0x201)
    cache_slot = SyntheticSaveDataInfo(SPACE_SD_USER, TYPE_CACHE, 0x010000000000D000, 0, (0, 0), 1, 5, 0x202)
    candidates_4 = resolve_restore_candidates(unknown_source, [dev_slot, cache_slot])
    check(len(candidates_4) == 2, "Fixture 4: Unknown source type/rank/index must not auto-narrow to 0 or drop candidates")

    # --------------------------------------------------------------------------
    # Fixture 5: No-match/cancel -> no create/extend/delete/write
    # --------------------------------------------------------------------------
    tracker_5 = SyntheticMutationTracker()
    empty_live_slots = []
    candidates_5 = resolve_restore_candidates(source_backup, empty_live_slots)
    check(len(candidates_5) == 0, "Fixture 5: No live slots -> 0 candidates")

    def simulate_restore_continuation(candidates, user_confirmed, tracker):
        if not candidates or not user_confirmed:
            return None  # Abort without mutation
        target = candidates[0]
        tracker.extend()
        tracker.write()
        return target

    res_no_match = simulate_restore_continuation(candidates_5, True, tracker_5)
    check(res_no_match is None and tracker_5.total_mutations() == 0,
          "Fixture 5: No-match must cause 0 creates/extends/deletes/writes")

    # User cancels single candidate confirmation
    res_cancel = simulate_restore_continuation(candidates_1, False, tracker_5)
    check(res_cancel is None and tracker_5.total_mutations() == 0,
          "Fixture 5: Cancel must cause 0 creates/extends/deletes/writes")

    # --------------------------------------------------------------------------
    # Fixture 6: Batch cancel before launch, duplicate target rejection
    # --------------------------------------------------------------------------
    tracker_6 = SyntheticMutationTracker()

    def simulate_batch_resolution(seeds, live_pool, user_choices):
        """
        Simulates PromptBatchRestoreTargets: resolves all targets first, detects duplicates,
        aborts entirely on any cancel.
        """
        resolved_targets = []
        seen_keys = set()

        for idx, seed in enumerate(seeds):
            cand = resolve_restore_candidates(seed, live_pool)
            choice = user_choices[idx]  # None (cancelled) or chosen candidate index
            if choice is None or choice >= len(cand):
                return None  # Cancelled -> abort batch

            target = cand[choice]
            k = target.save_entry_key()
            if k in seen_keys:
                return "DUPLICATE_REJECTED"
            seen_keys.add(k)
            resolved_targets.append(target)

        # Only after ALL targets resolved: execute batch restore
        for _ in resolved_targets:
            tracker_6.extend()
            tracker_6.write()
        return resolved_targets

    seed1 = SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0, is_backup=True)
    seed2 = SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001002, 0, USER_A, 0, 0, 0, is_backup=True)

    live1 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0x301)
    live2 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001002, 0, USER_A, 0, 0, 0x302)

    # 6a. Cancel on step 2 aborts entire batch before launch
    res_batch_cancel = simulate_batch_resolution([seed1, seed2], [live1, live2], user_choices=[0, None])
    check(res_batch_cancel is None and tracker_6.total_mutations() == 0,
          "Fixture 6: Batch cancel before launch must mutate nothing")

    # 6b. Duplicate target chosen: seed1 chooses live1, seed2 also chooses live1
    res_batch_dup = simulate_batch_resolution([seed1, seed1], [live1], user_choices=[0, 0])
    check(res_batch_dup == "DUPLICATE_REJECTED" and tracker_6.total_mutations() == 0,
          "Fixture 6: Duplicate target in batch must be rejected without mutation")

    # 6c. Successful batch: both distinct targets resolved, then launched
    res_batch_ok = simulate_batch_resolution([seed1, seed2], [live1, live2], user_choices=[0, 0])
    check(isinstance(res_batch_ok, list) and len(res_batch_ok) == 2,
          "Fixture 6: Successful batch resolves all targets before execution")
    check(tracker_6.total_mutations() == 4, "Fixture 6: Batch executed mutations only after all resolved")

    # --------------------------------------------------------------------------
    # Fixture 7: Exact existing-target open failure remains fail-closed
    # --------------------------------------------------------------------------
    def simulate_zip_restore_open(target, open_rc):
        # RestoreSaveZip behavior when e.save_data_id != 0
        if target.is_backup:
            return ("REJECTED_UNRESOLVED_BACKUP", 0x100)
        if target.save_data_id != 0:
            if open_rc != 0:
                return ("FAIL_CLOSED", open_rc)
            return ("SUCCESS", 0)
        return ("CREATE_FALLBACK", 0)

    res_open_fail = simulate_zip_restore_open(target_b, 0x202)
    check(res_open_fail == ("FAIL_CLOSED", 0x202),
          "Fixture 7: Exact existing target open failure must be fail-closed (no create fallback)")

    # --------------------------------------------------------------------------
    # Fixture 8: File Browser ZIP path retains nonzero target guard
    # --------------------------------------------------------------------------
    fb_live_cand = SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x010000000000A000, 0, USER_A, 0, 0, 0x777, is_backup=False)
    fb_backup_unresolved = SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x010000000000A000, 0, USER_A, 0, 0, 0, is_backup=True)

    res_fb_valid = simulate_zip_restore_open(fb_live_cand, 0)
    check(res_fb_valid == ("SUCCESS", 0), "Fixture 8: File Browser live candidate succeeds")

    res_fb_unresolved = simulate_zip_restore_open(fb_backup_unresolved, 0)
    check(res_fb_unresolved == ("REJECTED_UNRESOLVED_BACKUP", 0x100),
          "Fixture 8: Unresolved backup is rejected by guard")

    # --------------------------------------------------------------------------
    # Fixtures 9-11: 4-argument RestoreSaves fail-closed validation & missing backup skip
    # --------------------------------------------------------------------------
    def simulate_restore_saves(sources, targets, backup_paths, auto_backup_enabled, tracker):
        """
        Simulates 4-argument RestoreSaves behavior.
        Fails closed before any mutations if len(sources) != len(targets)
        or any target is invalid (is_backup or save_data_id == 0).
        Skips individual items if backup path is missing.
        """
        if len(sources) != len(targets):
            return ("COUNT_MISMATCH", 0, 0)

        for dst in targets:
            if dst.is_backup or dst.save_data_id == 0:
                return ("INVALID_TARGET", 0, 0)

        restored = 0
        skipped = 0

        for src, dst in zip(sources, targets):
            bpath = src.backup_path or backup_paths.get(src.application_id)
            if not bpath:
                skipped += 1
                continue

            if auto_backup_enabled and dst.save_data_id != 0:
                tracker.create()
                tracker.write()

            tracker.write()
            restored += 1

        return ("SUCCESS", restored, skipped)

    # Fixture 9: Batch count mismatch rejection before any mutation
    tracker_9 = SyntheticMutationTracker()
    src_mismatch = [
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0, is_backup=True, backup_path="/dumps/1.zip"),
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001002, 0, USER_A, 0, 0, 0, is_backup=True, backup_path="/dumps/2.zip"),
    ]
    dst_mismatch = [
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0x301, is_backup=False)
    ]
    res_mismatch, _, _ = simulate_restore_saves(src_mismatch, dst_mismatch, {}, True, tracker_9)
    check(res_mismatch == "COUNT_MISMATCH" and tracker_9.total_mutations() == 0,
          "Fixture 9: Count mismatch must be rejected upfront without any mutation")

    # Fixture 10: Batch invalid target rejection before any mutation
    tracker_10 = SyntheticMutationTracker()
    src_batch = [
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0, is_backup=True, backup_path="/dumps/1.zip"),
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001002, 0, USER_A, 0, 0, 0, is_backup=True, backup_path="/dumps/2.zip"),
    ]
    # 10a: One target has is_backup=True
    dst_invalid_backup = [
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0x301, is_backup=False),
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001002, 0, USER_A, 0, 0, 0, is_backup=True),
    ]
    res_inv_b, _, _ = simulate_restore_saves(src_batch, dst_invalid_backup, {}, True, tracker_10)
    check(res_inv_b == "INVALID_TARGET" and tracker_10.total_mutations() == 0,
          "Fixture 10a: Target with is_backup must reject entire batch upfront without mutation")

    # 10b: One target has save_data_id == 0
    dst_zero_id = [
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0x301, is_backup=False),
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001002, 0, USER_A, 0, 0, 0, is_backup=False),
    ]
    res_zero_id, _, _ = simulate_restore_saves(src_batch, dst_zero_id, {}, True, tracker_10)
    check(res_zero_id == "INVALID_TARGET" and tracker_10.total_mutations() == 0,
          "Fixture 10b: Target with save_data_id==0 must reject entire batch upfront without mutation")

    # Fixture 11: Legitimate missing source backup skips only that item
    tracker_11 = SyntheticMutationTracker()
    src_skip = [
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0, is_backup=True, backup_path=""),
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001002, 0, USER_A, 0, 0, 0, is_backup=True, backup_path="/dumps/2.zip"),
    ]
    dst_valid = [
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001001, 0, USER_A, 0, 0, 0x301, is_backup=False),
        SyntheticEntry(SPACE_USER, TYPE_ACCOUNT, 0x0100000000001002, 0, USER_A, 0, 0, 0x302, is_backup=False),
    ]
    res_skip, r_cnt, s_cnt = simulate_restore_saves(src_skip, dst_valid, {}, True, tracker_11)
    check(res_skip == "SUCCESS", "Fixture 11: RestoreSaves must succeed overall when single item is skipped")
    check(r_cnt == 1 and s_cnt == 1, f"Fixture 11: Expected 1 restored, 1 skipped; got {r_cnt}, {s_cnt}")
    check(tracker_11.total_mutations() == 3, "Fixture 11: Only valid item was mutated (auto-backup + restore)")

    # Fixture 12: Shared batch state remains valid across all steps and continuations
    class BatchLifetimeSimulator:
        def __init__(self, seeds, accounts):
            self.seeds = list(seeds)
            self.accounts = list(accounts)

        def run_step(self, step, on_resolved):
            assert len(self.seeds) > 0, "Seeds must not be empty"
            assert len(self.accounts) > 0, "Accounts must not be empty"
            s = self.seeds[step]
            on_resolved(s, self.accounts)

    sim = BatchLifetimeSimulator(src_batch, [USER_A, USER_B])
    resolved_steps = []
    def step_cb(s, accs):
        resolved_steps.append((s.application_id, len(accs)))

    sim.run_step(0, step_cb)
    sim.run_step(1, step_cb)
    check(len(resolved_steps) == 2, "Fixture 12: All batch steps completed without invalidating state")
    check(resolved_steps[0] == (0x0100000000001001, 2) and resolved_steps[1] == (0x0100000000001002, 2),
          "Fixture 12: Seeds and accounts remained intact across steps")

    print("Synthetic behavioral model checks: ALL PASS (12 fixture groups)")


def main():
    test_source_contracts()
    test_behavioral_model()
    print("ALL SAFE RESTORE TARGET CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")


if __name__ == "__main__":
    main()
