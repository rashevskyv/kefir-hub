#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for
P2-A exact save discovery and actual space routing.

NOTE ON SCOPE AND COVERAGE:
This test executes static source contracts and a Python behavioral model of the
discovery, deduplication, and space-routing algorithms. It validates algorithmic
invariance, space retention, collision resolution, and C++ source patterns.
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

    # 1. fs.hpp: FsNativeSave system-ID RW branch
    fs_hpp_path = os.path.join(repo_root, "sphaira", "include", "fs.hpp")
    with open(fs_hpp_path, "r", encoding="utf-8") as f:
        fs_hpp_src = f.read()

    check("m_open_result = fsOpenSaveDataFileSystemBySystemSaveDataId(&m_fs, save_data_space_id, attr);" in fs_hpp_src,
          "FsNativeSave system RW branch must pass caller-supplied save_data_space_id, not hardcoded System space")

    # 2. save_paths.hpp: DiscoverSaveDataInfo declaration
    save_paths_hpp = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    with open(save_paths_hpp, "r", encoding="utf-8") as f:
        paths_hpp_src = f.read()

    check("auto DiscoverSaveDataInfo(const AccountUid* uid_filter = nullptr, const std::optional<u8>& type_filter = std::nullopt) -> std::vector<FsSaveDataInfo>;" in paths_hpp_src,
          "save_paths.hpp must declare DiscoverSaveDataInfo with optional uid_filter and type_filter")

    # 3. save_paths.cpp: DiscoverSaveDataInfo implementation and probe spaces
    save_paths_cpp = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_paths.cpp")
    with open(save_paths_cpp, "r", encoding="utf-8") as f:
        paths_cpp_src = f.read()

    for space_sym in [
        "FsSaveDataSpaceId_System",
        "FsSaveDataSpaceId_User",
        "FsSaveDataSpaceId_SdSystem",
        "FsSaveDataSpaceId_Temporary",
        "FsSaveDataSpaceId_SdUser",
        "FsSaveDataSpaceId_ProperSystem",
        "FsSaveDataSpaceId_SafeMode",
    ]:
        check(space_sym in paths_cpp_src, f"save_paths.cpp must probe concrete space {space_sym}")

    check("fsOpenSaveDataInfoReader(&reader, space)" in paths_cpp_src,
          "save_paths.cpp must use fsOpenSaveDataInfoReader with concrete space")
    check("fsSaveDataInfoReaderClose(&reader)" in paths_cpp_src,
          "save_paths.cpp must close reader on successful open")
    check("fsOpenSaveDataInfoReaderWithFilter" not in paths_cpp_src,
          "save_paths.cpp DiscoverSaveDataInfo must not use filtered reader")
    check("seen_keys.insert(key).second" in paths_cpp_src,
          "save_paths.cpp must deduplicate exact slots using SaveEntryKey")
    check("staged.insert(staged.end(), chunk.begin(), chunk.begin() + count);" in paths_cpp_src,
          "save_paths.cpp must stage records until EOF")
    check("if (read_failed) {" in paths_cpp_src and "continue;" in paths_cpp_src,
          "save_paths.cpp must discard partial records on read failure and continue other spaces")

    # 4. save_menu.cpp: ListAccountSaves, ReadSaveEntries, ScanHomebrew, ResolveRestoreTarget, CreateBackupIfNewer
    save_menu_cpp = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save_menu.cpp")
    with open(save_menu_cpp, "r", encoding="utf-8") as f:
        menu_cpp_src = f.read()

    check("DiscoverSaveDataInfo(&uid, FsSaveDataType_Account)" in menu_cpp_src,
          "ListAccountSaves must delegate to DiscoverSaveDataInfo")
    check("DiscoverSaveDataInfo(&m_accounts[index].uid, FsSaveDataType_Account)" in menu_cpp_src,
          "ReadSaveEntries must delegate account saves to DiscoverSaveDataInfo")
    check("DiscoverSaveDataInfo(nullptr, data_type)" in menu_cpp_src,
          "ReadSaveEntries must delegate non-account saves to DiscoverSaveDataInfo")
    check("!backup.is_backup && backup.save_data_id != 0 && (!explicit_uid || std::memcmp(explicit_uid, &backup.uid, sizeof(AccountUid)) == 0)" in menu_cpp_src,
          "ResolveRestoreTarget must fast-path existing non-backup live seed with nonzero save ID and no UID change")
    check("const auto space_id = static_cast<FsSaveDataSpaceId>(e.save_data_space_id);" in menu_cpp_src,
          "CreateBackupIfNewer must use static_cast<FsSaveDataSpaceId>(e.save_data_space_id) directly")

    # 5. save_menu_ops.cpp: DeleteSavesOn, DeleteSaves, RestoreSaveZip
    save_menu_ops = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_menu_ops.cpp")
    with open(save_menu_ops, "r", encoding="utf-8") as f:
        ops_cpp_src = f.read()

    check("const auto space_id = static_cast<FsSaveDataSpaceId>(e.save_data_space_id);" in ops_cpp_src,
          "DeleteSaves / DeleteSavesOn must use static_cast<FsSaveDataSpaceId>(e.save_data_space_id) directly")
    check(("if (target_entry.save_data_id != 0) {" in ops_cpp_src or "if (e.save_data_id != 0) {" in ops_cpp_src) and "R_TRY(check_rc);" in ops_cpp_src,
          "RestoreSaveZip must return open failure directly without create when save_data_id != 0")
    check("static_cast<FsSaveDataSpaceId>(target_entry.save_data_space_id)" in ops_cpp_src or "(e.save_data_id != 0)\n        ? static_cast<FsSaveDataSpaceId>(e.save_data_space_id)" in ops_cpp_src,
          "RestoreSaveZip must use actual save_data_space_id directly when save_data_id != 0")

    # 6. filebrowser_ops.cpp: RestoreSaveFile ZIP vs DISA
    fb_ops_cpp = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_ops.cpp")
    with open(fb_ops_cpp, "r", encoding="utf-8") as f:
        fb_src = f.read()

    check("if (is_disa) {" in fb_src,
          "RestoreSaveFile must branch on is_disa to preserve legacy DISA discovery")
    check("save::DiscoverSaveDataInfo(&acc.uid, FsSaveDataType_Account)" in fb_src,
          "RestoreSaveFile ZIP picker must query Account saves per local UID via shared discovery")
    check("save::DiscoverSaveDataInfo(nullptr, type)" in fb_src,
          "RestoreSaveFile ZIP picker must query non-account saves once total via shared discovery")
    check("save::SaveEntryKey(info)" in fb_src,
          "RestoreSaveFile ZIP picker must deduplicate candidates using SaveEntryKey")
    check('App::Push<PopupList>("Select Target Save"_i18n' in fb_src,
          "RestoreSaveFile ZIP/folder must always present explicit target picker")

    print("Source contracts: ALL PASS (6 anchor groups)")


# ==============================================================================
# Synthetic Behavioral Model (Algorithmic Invariance & Regression Verification)
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


def synthetic_discover(spaces_data, spaces_fail_read, uid_filter=None, type_filter=None):
    """
    Simulates DiscoverSaveDataInfo according to the SHARED DISCOVERY FIXED CONTRACT.
    Does NOT invoke C++, IPC, libnx or switch services.
    """
    out = []
    seen_keys = set()
    concrete_spaces = [0, 1, 2, 3, 4, 100, 101]

    for space in concrete_spaces:
        if space not in spaces_data:
            continue  # e.g. open failed / unavailable space

        if space in spaces_fail_read:
            # Staged records are discarded on read failure
            continue

        staged = spaces_data[space]
        for info in staged:
            if type_filter is not None and info.save_data_type != type_filter:
                continue
            if uid_filter is not None and info.uid != uid_filter:
                continue

            key = info.save_entry_key()
            if key not in seen_keys:
                seen_keys.add(key)
                out.append(info)

    return out


def test_behavioral_model():
    # Constant save types for testing
    TYPE_SYSTEM = 0
    TYPE_ACCOUNT = 1
    TYPE_BCAT = 2
    TYPE_DEVICE = 3
    TYPE_CACHE = 5

    # Constant spaces
    SPACE_SYSTEM = 0
    SPACE_USER = 1
    SPACE_SD_USER = 4
    SPACE_PROPER_SYSTEM = 100

    USER_A = (0x0123456789ABCDEF, 0x1111222233334444)
    USER_B = (0xFEEDFACECAFEBEEF, 0x5555666677778888)

    # 1. Multi-space survival: Cache in User space vs Cache in SdUser space
    cache_user = SyntheticSaveDataInfo(SPACE_USER, TYPE_CACHE, 0x0100000000001000, 0, (0, 0), 0, 0, 0x10)
    cache_sd = SyntheticSaveDataInfo(SPACE_SD_USER, TYPE_CACHE, 0x0100000000001000, 0, (0, 0), 0, 0, 0x11)
    data = {SPACE_USER: [cache_user], SPACE_SD_USER: [cache_sd]}
    discovered = synthetic_discover(data, set())
    check(len(discovered) == 2, "Cache in User and SdUser spaces must both survive discovery")
    check(any(x.save_data_space_id == SPACE_USER for x in discovered), "Cache/User must survive")
    check(any(x.save_data_space_id == SPACE_SD_USER for x in discovered), "Cache/SdUser must survive")

    # 2. System save in space 0 vs alternate space
    sys_0 = SyntheticSaveDataInfo(SPACE_SYSTEM, TYPE_SYSTEM, 0, 0x8000000000000001, (0, 0), 0, 0, 0x20)
    sys_alt = SyntheticSaveDataInfo(SPACE_PROPER_SYSTEM, TYPE_SYSTEM, 0, 0x8000000000000001, (0, 0), 0, 0, 0x21)
    data_sys = {SPACE_SYSTEM: [sys_0], SPACE_PROPER_SYSTEM: [sys_alt]}
    discovered_sys = synthetic_discover(data_sys, set())
    check(len(discovered_sys) == 2, "System save in space 0 and alternate space must both survive")

    # 3. Rank 0 vs Rank 1 survival
    rank_0 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000002000, 0, USER_A, 0, 0, 0x30)
    rank_1 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000002000, 0, USER_A, 1, 0, 0x31)
    data_ranks = {SPACE_USER: [rank_0, rank_1]}
    discovered_ranks = synthetic_discover(data_ranks, set())
    check(len(discovered_ranks) == 2, "Rank 0 and Rank 1 for same title/user must both survive")

    # 4. Identity differences: UID, index, type, space
    acc_a = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000003000, 0, USER_A, 0, 0, 0x40)
    acc_b = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000003000, 0, USER_B, 0, 0, 0x41)
    idx_0 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000003000, 0, USER_A, 0, 0, 0x42)
    idx_1 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000003000, 0, USER_A, 0, 1, 0x43)
    bcat = SyntheticSaveDataInfo(SPACE_USER, TYPE_BCAT, 0x0100000000003000, 0, (0, 0), 0, 0, 0x44)
    data_diff = {SPACE_USER: [acc_a, acc_b, idx_1, bcat]}
    discovered_diff = synthetic_discover(data_diff, set())
    check(len(discovered_diff) == 4, "Different UID/index/type must each survive deduplication")

    # 5. Duplicate slot deduplication
    dup1 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000004000, 0, USER_A, 0, 0, 0x50)
    dup2 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000004000, 0, USER_A, 0, 0, 0x50)
    data_dup = {SPACE_USER: [dup1, dup2]}
    discovered_dup = synthetic_discover(data_dup, set())
    check(len(discovered_dup) == 1, "Duplicate exact slots must deduplicate into 1")

    # 6. Read failure staging rollback: space with read failure publishes 0 partial records
    partial_1 = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000005000, 0, USER_A, 0, 0, 0x60)
    good_sys = SyntheticSaveDataInfo(SPACE_SYSTEM, TYPE_SYSTEM, 0, 0x8000000000000002, (0, 0), 0, 0, 0x61)
    data_fail = {SPACE_USER: [partial_1], SPACE_SYSTEM: [good_sys]}
    # Simulate read failure in User space
    discovered_fail = synthetic_discover(data_fail, spaces_fail_read={SPACE_USER})
    check(len(discovered_fail) == 1, "Failed space must publish no records; good space must survive")
    check(discovered_fail[0].save_data_space_id == SPACE_SYSTEM, "Only healthy space records survive")

    # 7. Zero accounts: non-account scan succeeds
    device_save = SyntheticSaveDataInfo(SPACE_USER, TYPE_DEVICE, 0x0100000000006000, 0, (0, 0), 0, 0, 0x70)
    data_zero_acc = {SPACE_USER: [device_save]}
    # Zero accounts -> no account query, non-account query runs
    discovered_dev = synthetic_discover(data_zero_acc, set(), uid_filter=None, type_filter=TYPE_DEVICE)
    check(len(discovered_dev) == 1, "Non-account discovery must work with zero accounts")
    discovered_zero_acc = synthetic_discover(data_zero_acc, set(), uid_filter=USER_A, type_filter=TYPE_ACCOUNT)
    check(len(discovered_zero_acc) == 0, "Account discovery with no accounts returns empty")

    # 8. Multiple accounts: non-account types queried once total, account types queried per account
    acc1_save = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000007000, 0, USER_A, 0, 0, 0x80)
    acc2_save = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000007000, 0, USER_B, 0, 0, 0x81)
    dev_shared = SyntheticSaveDataInfo(SPACE_USER, TYPE_DEVICE, 0x0100000000007000, 0, (0, 0), 0, 0, 0x82)
    full_data = {SPACE_USER: [acc1_save, acc2_save, dev_shared]}

    all_accounts = [USER_A, USER_B]
    combined_candidates = []
    seen_cand_keys = set()

    for acc in all_accounts:
        for rec in synthetic_discover(full_data, set(), uid_filter=acc, type_filter=TYPE_ACCOUNT):
            k = rec.save_entry_key()
            if k not in seen_cand_keys:
                seen_cand_keys.add(k)
                combined_candidates.append(rec)

    # Non-account scan once total
    for non_type in [TYPE_DEVICE, TYPE_BCAT]:
        for rec in synthetic_discover(full_data, set(), uid_filter=None, type_filter=non_type):
            k = rec.save_entry_key()
            if k not in seen_cand_keys:
                seen_cand_keys.add(k)
                combined_candidates.append(rec)

    check(len(combined_candidates) == 3, "Account saves per account + non-account saves once total must yield exactly 3 slots")

    # 9. Filename target_id selection model
    def match_target_id(candidates, target_id):
        matches = [c for c in candidates if c.save_data_id == target_id or c.application_id == target_id or c.system_save_data_id == target_id]
        if len(matches) == 1:
            return matches[0]  # Auto-select single match
        return None  # Fall through to manual picker

    cand_single = [acc1_save, dev_shared]
    # Unique match by save_data_id
    check(match_target_id(cand_single, 0x80) == acc1_save, "Unique match must auto-select")
    # Ambiguous match: two candidates share application_id 0x0100000000007000
    check(match_target_id(cand_single, 0x0100000000007000) is None, "Ambiguous match must fall through to picker")

    # 10. Existing target open failure without create model
    def simulate_restore(existing_target, open_result_code):
        if existing_target.save_data_id != 0:
            if open_result_code != 0:
                return ("FAIL_SURFACED", open_result_code)
            return ("SUCCESS_EXTEND_AND_RESTORE", 0)
        else:
            # new target -> create
            return ("CREATED_NEW", 0)

    target_live = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000007000, 0, USER_A, 0, 0, 0x80)
    res_fail = simulate_restore(target_live, 0x202)
    check(res_fail == ("FAIL_SURFACED", 0x202), "Existing target open failure must be surfaced, not create fallback")

    target_new = SyntheticSaveDataInfo(SPACE_USER, TYPE_ACCOUNT, 0x0100000000007000, 0, USER_A, 0, 0, 0)
    res_new = simulate_restore(target_new, 0x202)
    check(res_new == ("CREATED_NEW", 0), "Absent/new target with save_data_id==0 proceeds to create")

    print("Synthetic behavioral model checks: ALL PASS (10 regression groups)")


def main():
    test_source_contracts()
    test_behavioral_model()
    print("ALL EXACT SAVE DISCOVERY CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")


if __name__ == "__main__":
    main()
