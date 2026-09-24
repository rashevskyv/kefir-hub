# Behavioral scenarios for MTP save contract.
import os
import sys
import itertools
from contract_fixtures.mtp_save_models import (
    SyntheticSaveInfo, allocate_save_names, build_mtp_save_tree,
    sanitize_component, truncate_utf8, format_save_game_dir_name,
    disambiguate_final_name
)
from contract_fixtures.mtp_save_proxy import (
    SyntheticNativeFs, SyntheticFsSaveProxy,
    FS_SUCCESS, FS_ERROR_NOT_IMPLEMENTED, FS_ERROR_PATH_NOT_FOUND,
    FS_ERROR_MOUNT_FAILED, FS_OPEN_READ, FS_OPEN_WRITE, FS_OPEN_APPEND
)

def check(condition, msg):
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)

def test_behavioral_model():
    # =========================================================================
    # Part 1: Algorithmic Invariance & Name Allocation Regressions
    # =========================================================================
    # Case 1: Permutation invariance & literal "Player [save-id]" collision
    acc1 = SyntheticSaveInfo(save_data_id=0x10, space_id=1, save_type=1, app_id=0x1000, uid=(1, 0))
    acc2 = SyntheticSaveInfo(save_data_id=0x20, space_id=1, save_type=1, app_id=0x1000, uid=(2, 0))
    acc3 = SyntheticSaveInfo(save_data_id=0x30, space_id=1, save_type=1, app_id=0x1000, uid=(3, 0))

    accounts_dict = {
        (1, 0): "Player",
        (2, 0): "Player",
        (3, 0): "Player [0000000000000010]",  # Literal nickname equals acc1's generated suffixed name
    }

    test_records = [acc1, acc2, acc3]
    baseline = allocate_save_names(test_records, accounts_dict)

    check(len(baseline) == 3, "All 3 distinct account records must be retained")
    check(len(set(baseline.values())) == 3, "All 3 visible names must be distinct")

    for perm in itertools.permutations(test_records):
        mapping = allocate_save_names(list(perm), accounts_dict)
        check(mapping == baseline, f"Permutation {perm} produced different mapping than baseline!")

    # Case 2: Permutation invariance for multiple distinct records sharing a non-account bucket
    dev1 = SyntheticSaveInfo(save_data_id=0x01, space_id=1, save_type=3, app_id=0x1000, uid=(0, 0))
    dev2 = SyntheticSaveInfo(save_data_id=0x02, space_id=1, save_type=3, app_id=0x1000, uid=(0, 0))
    dev_records = [dev1, dev2]
    dev_base = allocate_save_names(dev_records, {})
    check(len(dev_base) == 2, "Both distinct device records must be retained")
    check(dev_base[dev1.identity_tuple] == "Device", "First sorted device record gets unsuffixed Device")
    check(dev_base[dev2.identity_tuple] == "Device [0000000000000002]", "Second device record gets suffixed Device")

    for perm in itertools.permutations(dev_records):
        mapping = allocate_save_names(list(perm), {})
        check(mapping == dev_base, f"Device permutation {perm} produced non-deterministic names")

    # Case 3: Identical record duplicates
    dup_records = [dev1, dev1, acc1, acc1, acc2]
    dup_res = allocate_save_names(dup_records, accounts_dict)
    check(len(dup_res) == 3, "Identical records must be deduplicated by record identity")
    check(dup_res[dev1.identity_tuple] == "Device", "Deduplicated device record retained correctly")

    # Case 4: Long CON.<text> game name at capacity
    cyrillic_long = "CON.Гра_" + ("Пригоди_" * 35)
    game_dir = format_save_game_dir_name(cyrillic_long, 0x0100000000010000, max_len=255)
    game_bytes = game_dir.encode("utf-8")

    check(len(game_bytes) <= 255, f"Game dir name exceeds 255 bytes limit: {len(game_bytes)}")
    check(game_dir.startswith("_CON."), f"Windows reserved stem CON must be prefixed with _: {game_dir[:10]}")
    check(game_dir.endswith(" [0100000000010000]"), f"Stable Title ID suffix must be intact: {game_dir[-25:]}")

    try:
        decoded = game_bytes.decode("utf-8")
        check(decoded == game_dir, "Decoded UTF-8 does not match original string")
    except UnicodeDecodeError:
        check(False, "Game dir name has invalid UTF-8 split")

    enumerated = game_dir[:255]
    check(enumerated == game_dir, "MakeVirtualDirEntry would truncate stored game directory name")

    # Case 5: Nickname collisions with non-account buckets and Unicode preservation
    acc_dev = SyntheticSaveInfo(save_data_id=0x99, space_id=1, save_type=1, app_id=0x2000, uid=(9, 0))
    acc_cyr = SyntheticSaveInfo(save_data_id=0xAA, space_id=1, save_type=1, app_id=0x2000, uid=(10, 0))
    mixed_dict = {
        (9, 0): "Device",
        (10, 0): "Користувач",
    }
    mixed_res = allocate_save_names([acc_dev, acc_cyr], mixed_dict)
    check(mixed_res[acc_dev.identity_tuple] == "Device [0000000000000099]",
          "Account nicknamed 'Device' must be suffixed to not collide with Device bucket")
    check(mixed_res[acc_cyr.identity_tuple] == "Користувач",
          "Unicode Cyrillic nickname must be preserved without distortion")

    # =========================================================================
    # Part 2: All Seven Types Across All Concrete Spaces & Disambiguation
    # =========================================================================
    # Concrete spaces:
    # 0 = System, 1 = User, 2 = SdSystem, 3 = Temporary, 4 = SdUser, 100 = ProperSystem, 101 = SafeMode
    # Save types:
    # 0 = System, 1 = Account, 2 = BCAT, 3 = Device, 4 = Temporary, 5 = Cache, 6 = SystemBcat

    rec_sys1 = SyntheticSaveInfo(save_data_id=0x1001, space_id=0, save_type=0, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000001)
    rec_sys2 = SyntheticSaveInfo(save_data_id=0x1002, space_id=100, save_type=0, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000001) # duplicate sys_id
    rec_sys3 = SyntheticSaveInfo(save_data_id=0x1003, space_id=101, save_type=0, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000003)
    rec_acc_alice = SyntheticSaveInfo(save_data_id=0x2001, space_id=1, save_type=1, app_id=0x0100000000001000, uid=(0x1111, 0x2222))
    rec_acc_bob = SyntheticSaveInfo(save_data_id=0x2002, space_id=4, save_type=1, app_id=0x0100000000001000, uid=(0x3333, 0x4444))
    rec_bcat = SyntheticSaveInfo(save_data_id=0x3001, space_id=1, save_type=2, app_id=0x0100000000001000, uid=(0, 0))
    rec_dev_part2 = SyntheticSaveInfo(save_data_id=0x4001, space_id=1, save_type=3, app_id=0x0100000000001000, uid=(0, 0))
    rec_temp_app = SyntheticSaveInfo(save_data_id=0x5001, space_id=3, save_type=4, app_id=0x0100000000005000, uid=(0, 0))
    rec_temp_noapp = SyntheticSaveInfo(save_data_id=0x5002, space_id=3, save_type=4, app_id=0, uid=(0, 0)) # fallback to save_data_id
    rec_cache0 = SyntheticSaveInfo(save_data_id=0x6001, space_id=1, save_type=5, app_id=0x0100000000001000, uid=(0, 0), index=0)
    rec_cache1 = SyntheticSaveInfo(save_data_id=0x6002, space_id=4, save_type=5, app_id=0x0100000000001000, uid=(0, 0), index=1)
    rec_cache1_dup = SyntheticSaveInfo(save_data_id=0x6003, space_id=4, save_type=5, app_id=0x0100000000001000, uid=(0, 0), index=1) # duplicate index
    rec_sysbcat1 = SyntheticSaveInfo(save_data_id=0x7001, space_id=0, save_type=6, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000010)
    rec_sysbcat2 = SyntheticSaveInfo(save_data_id=0x7002, space_id=2, save_type=6, app_id=0, uid=(0, 0), sys_save_id=0x0100000000000010) # duplicate sys_id
    rec_unknown = SyntheticSaveInfo(save_data_id=0x9999, space_id=1, save_type=99, app_id=0x0100000000009999, uid=(0, 0))

    all_seven_records = [
        rec_sys1, rec_sys2, rec_sys3,
        rec_acc_alice, rec_acc_bob,
        rec_bcat, rec_dev_part2,
        rec_temp_app, rec_temp_noapp,
        rec_cache0, rec_cache1, rec_cache1_dup,
        rec_sysbcat1, rec_sysbcat2,
        rec_unknown,
    ]

    accounts_p2 = {
        (0x1111, 0x2222): "Alice",
        (0x3333, 0x4444): "Bob",
    }
    title_p2 = {
        0x0100000000001000: "Super Game",
    }

    full_tree = build_mtp_save_tree(all_seven_records, accounts_p2, title_p2)

    # Unknown type must be discarded
    check("Super Game [0100000000009999]" not in full_tree and "[0100000000009999]" not in full_tree,
          "Unknown save type 99 must not create a directory")

    # Top level explicit bucket presence
    check("System" in full_tree, "Top-level 'System' bucket must exist")
    check("System BCAT" in full_tree, "Top-level 'System BCAT' bucket must exist")
    check("Temporary" in full_tree, "Top-level 'Temporary' bucket must exist")
    check("Super Game [0100000000001000]" in full_tree, "Game directory must exist")

    # Verify System naming & disambiguation
    sys_entries = full_tree["System"]
    check("System [0100000000000001]" in sys_entries, "First System save gets unsuffixed name")
    check("System [0100000000000001] [0000000000001002]" in sys_entries,
          "Duplicate System save gets save_data_id suffix disambiguation")
    check("System [0100000000000003]" in sys_entries, "System save 3 gets correct name")

    # Verify System BCAT naming & disambiguation
    bcat_sys_entries = full_tree["System BCAT"]
    check("System BCAT [0100000000000010]" in bcat_sys_entries, "First System BCAT gets unsuffixed name")
    check("System BCAT [0100000000000010] [0000000000007002]" in bcat_sys_entries,
          "Duplicate System BCAT gets save_data_id suffix disambiguation")

    # Verify Temporary naming
    temp_entries = full_tree["Temporary"]
    check("0100000000005000" in temp_entries, "Temporary save with application_id uses 16-hex application_id")
    check("0000000000005002" in temp_entries, "Temporary save without application_id uses 16-hex save_data_id")

    # Verify Game entries: Account, BCAT, Device, Cache (index 0, 1, and duplicate 1)
    game_entries = full_tree["Super Game [0100000000001000]"]
    check("Alice" in game_entries, "Account save Alice present")
    check("Bob" in game_entries, "Account save Bob present")
    check("BCAT" in game_entries, "BCAT save present")
    check("Device" in game_entries, "Device save present")
    check("Cache" in game_entries, "Cache index 0 gets unsuffixed 'Cache'")
    check("Cache 1" in game_entries, "First Cache index 1 gets 'Cache 1'")
    check("Cache 1 [0000000000006003]" in game_entries, "Duplicate Cache index 1 gets suffixed disambiguation")

    # Verify permutation independence across all 7 types with explicit deterministic orderings
    test_permutations = [
        all_seven_records,
        list(reversed(all_seven_records)),
        all_seven_records[4:] + all_seven_records[:4],
        all_seven_records[8:] + all_seven_records[:8],
        sorted(all_seven_records, key=lambda r: (r.save_data_id * 1103515245 + 12345) % (2**31)),
    ]
    for perm_idx, reordered in enumerate(test_permutations):
        shuffled_tree = build_mtp_save_tree(reordered, accounts_p2, title_p2)
        check(list(shuffled_tree.keys()) == list(full_tree.keys()),
              f"Top level keys must match regardless of input order (permutation {perm_idx})")
        for k in full_tree:
            check(list(shuffled_tree[k].keys()) == list(full_tree[k].keys()),
                  f"Subtree keys for {k} must match regardless of input order (permutation {perm_idx})")

    # Verify NO Account UID exposure anywhere in the visible tree
    for top_k, sub_tree in full_tree.items():
        for sub_k in sub_tree:
            check("1111" not in top_k and "2222" not in top_k and "3333" not in top_k and "4444" not in top_k,
                  "Account UID must not appear in top-level directory names")
            check("1111" not in sub_k and "2222" not in sub_k and "3333" not in sub_k and "4444" not in sub_k,
                  "Account UID must not appear in save directory names")

    # Verify exact retained routing assertions for every visible node
    p2_proxy = SyntheticFsSaveProxy(full_tree)
    for top_k, sub_tree in full_tree.items():
        for sub_k, info in sub_tree.items():
            rc, _ = p2_proxy.open_directory(f"/{top_k}/{sub_k}")
            check(rc == FS_SUCCESS, f"open_directory failed on /{top_k}/{sub_k}")
            # Find the last routed tuple
            last_route = p2_proxy.routed_tuples[-1]
            key_exp = f"{top_k}/{sub_k}"
            check(last_route[0] == key_exp, f"Routed key mismatch: {last_route[0]} vs {key_exp}")
            check(last_route[1] == info.save_data_space_id, "Routed space_id must match exact retained info")
            check(last_route[2] == info.save_data_type, "Routed save_type must match exact retained info")
            check(last_route[3] == info.application_id, "Routed application_id must match exact retained info")
            check(last_route[4] == info.system_save_data_id, "Routed system_save_data_id must match exact retained info")
            check(last_route[5] == info.uid, "Routed uid must match exact retained info")
            check(last_route[6] == info.save_data_rank, "Routed save_data_rank must match exact retained info")
            check(last_route[7] == info.save_data_index, "Routed save_data_index must match exact retained info")
            check(last_route[8] == info.save_data_id, "Routed save_data_id must match exact retained info")
            check(last_route[9] is True, "Routed read_only must strictly be True")

    # -------------------------------------------------------------------------
    # System Full Disambiguator Collision Matrix (forcing DisambiguateFinalName)
    # -------------------------------------------------------------------------
    # At least three distinct retained System records with:
    # - the same system_save_data_id (0x01000000000000AA)
    # - the same save_data_id (0x5555) for the records needed to collide at base + [save_data_id]
    # - distinct actual spaces, ranks, and indexes
    col_sys1 = SyntheticSaveInfo(save_data_id=0x5555, space_id=0, save_type=0, app_id=0, uid=(0, 0), rank=0, index=0, sys_save_id=0x01000000000000AA)
    col_sys2 = SyntheticSaveInfo(save_data_id=0x5555, space_id=100, save_type=0, app_id=0, uid=(0, 0), rank=0, index=1, sys_save_id=0x01000000000000AA)
    col_sys3 = SyntheticSaveInfo(save_data_id=0x5555, space_id=101, save_type=0, app_id=0, uid=(0, 0), rank=1, index=2, sys_save_id=0x01000000000000AA)

    expected_name1 = "System [01000000000000AA]"
    expected_name2 = "System [01000000000000AA] [0000000000005555]"
    expected_name3 = "System [01000000000000AA] [0000000000005555-s101-t0-r1-i2]"

    # Test explicit orderings: original, reversed, rotated, collision records swapped
    sys_orderings = [
        [col_sys1, col_sys2, col_sys3],  # original
        [col_sys3, col_sys2, col_sys1],  # reversed
        [col_sys2, col_sys3, col_sys1],  # rotated
        [col_sys1, col_sys3, col_sys2],  # collision records swapped
        [col_sys2, col_sys1, col_sys3],
        [col_sys3, col_sys1, col_sys2],
    ]

    tree_col = None
    for ord_idx, perm in enumerate(sys_orderings):
        tree_col = build_mtp_save_tree(perm, {})
        check("System" in tree_col, "System bucket must exist")
        s_entries = tree_col["System"]

        # Every distinct record remains visible
        check(len(s_entries) == 3, f"All 3 colliding records must remain visible (ordering {ord_idx})")

        # All visible names are unique case-insensitively
        lower_names = [name.lower() for name in s_entries.keys()]
        check(len(lower_names) == len(set(lower_names)), f"All visible names must be case-insensitively unique (ordering {ord_idx})")

        # One record gets the base name
        check(expected_name1 in s_entries, f"Record 1 must get exact base name: {expected_name1}")
        check(s_entries[expected_name1] is col_sys1, "Base name must map to col_sys1")

        # One record gets the save-ID suffix
        check(expected_name2 in s_entries, f"Record 2 must get save-ID suffix: {expected_name2}")
        check(s_entries[expected_name2] is col_sys2, "Save-ID suffixed name must map to col_sys2")

        # The next colliding record gets the full deterministic suffix containing exact s<space>-t<type>-r<rank>-i<index>
        check(expected_name3 in s_entries, f"Record 3 must force DisambiguateFinalName with full suffix: {expected_name3}")
        check(s_entries[expected_name3] is col_sys3, "Full disambiguated name must map to col_sys3")

        # No UID value appears in visible names
        for vname in s_entries.keys():
            check("uid" not in vname.lower(), "UID must not appear in visible name")

    # Routing verification for third-stage disambiguated record:
    col_proxy = SyntheticFsSaveProxy(tree_col)
    rc, _ = col_proxy.open_directory(f"/System/{expected_name3}")
    check(rc == FS_SUCCESS, "open_directory must succeed on full disambiguated path")
    check(len(col_proxy.attempted_routes) == 1, "Exactly one route attempt must be made")
    routed_col3 = col_proxy.attempted_routes[0]
    check(routed_col3[0] == f"System/{expected_name3}", "Route key mismatch")
    check(routed_col3[1] == 101, "Space 101 (SafeMode) must survive exact routing")
    check(routed_col3[2] == 0, "Type 0 (System) must survive exact routing")
    check(routed_col3[4] == 0x01000000000000AA, "System save data ID must survive exact routing")
    check(routed_col3[6] == 1, "Rank 1 difference must survive exact routing")
    check(routed_col3[7] == 2, "Index 2 difference must survive exact routing")
    check(routed_col3[8] == 0x5555, "Save data ID must survive exact routing")
    check(routed_col3[9] is True, "read_only=True must survive exact routing")

    # Narrowly scoped test for System BCAT full disambiguator
    col_bcat1 = SyntheticSaveInfo(save_data_id=0x7777, space_id=0, save_type=6, app_id=0, uid=(0, 0), rank=0, index=0, sys_save_id=0x01000000000000BB)
    col_bcat2 = SyntheticSaveInfo(save_data_id=0x7777, space_id=2, save_type=6, app_id=0, uid=(0, 0), rank=0, index=1, sys_save_id=0x01000000000000BB)
    col_bcat3 = SyntheticSaveInfo(save_data_id=0x7777, space_id=2, save_type=6, app_id=0, uid=(0, 0), rank=1, index=2, sys_save_id=0x01000000000000BB)

    expected_bcat1 = "System BCAT [01000000000000BB]"
    expected_bcat2 = "System BCAT [01000000000000BB] [0000000000007777]"
    expected_bcat3 = "System BCAT [01000000000000BB] [0000000000007777-s2-t6-r1-i2]"

    tree_bcat_col = build_mtp_save_tree([col_bcat3, col_bcat1, col_bcat2], {})
    sb_entries = tree_bcat_col["System BCAT"]
    check(len(sb_entries) == 3, "All 3 System BCAT records visible")
    check(expected_bcat1 in sb_entries and sb_entries[expected_bcat1] is col_bcat1, "System BCAT base name correct")
    check(expected_bcat2 in sb_entries and sb_entries[expected_bcat2] is col_bcat2, "System BCAT save-ID suffix correct")
    check(expected_bcat3 in sb_entries and sb_entries[expected_bcat3] is col_bcat3, "System BCAT full disambiguator suffix correct")

    col_bcat_proxy = SyntheticFsSaveProxy(tree_bcat_col)
    rc, _ = col_bcat_proxy.open_directory(f"/System BCAT/{expected_bcat3}")
    check(rc == FS_SUCCESS, "open_directory must succeed on System BCAT full disambiguated path")
    routed_bcat3 = col_bcat_proxy.attempted_routes[0]
    check(routed_bcat3[1] == 2 and routed_bcat3[6] == 1 and routed_bcat3[7] == 2 and routed_bcat3[9] is True,
          "System BCAT rank/index/space/read_only must survive routing")

    # =========================================================================
    # Part 3: Stale Paths, Alternate-Space Fallback Refusal, Presence & Mount Errors
    # =========================================================================
    # 1. Stale paths return FS_ERROR_PATH_NOT_FOUND
    rc, _ = p2_proxy.open_directory("/NonExistentGame")
    check(rc == FS_ERROR_PATH_NOT_FOUND, "open_directory on stale level 1 must return FS_ERROR_PATH_NOT_FOUND")
    rc, _ = p2_proxy.open_file("/NonExistentGame", FS_OPEN_READ)
    check(rc == FS_ERROR_PATH_NOT_FOUND, "open_file on stale level 1 must return FS_ERROR_PATH_NOT_FOUND")

    stale_paths_lvl2 = [
        "/NonExistentGame/slot0",
        "/Super Game [0100000000001000]/NonExistentBucket",
        "/System/NonExistentSystemSave",
        "/Temporary/NonExistentTempSave",
        "/System BCAT/NonExistentSystemBcatSave",
    ]
    for sp in stale_paths_lvl2:
        rc, _ = p2_proxy.open_directory(sp)
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"open_directory on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")
        rc, _ = p2_proxy.open_file(f"{sp}/slot0/savedata.bin", FS_OPEN_READ)
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"open_file on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")
        rc, _ = p2_proxy.get_total_space(sp)
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"get_total_space on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")
        rc, _ = p2_proxy.get_free_space(sp)
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"get_free_space on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")
        rc, _ = p2_proxy.get_entry_type(f"{sp}/slot0/savedata.bin")
        check(rc == FS_ERROR_PATH_NOT_FOUND, f"get_entry_type on stale path {sp} must return FS_ERROR_PATH_NOT_FOUND")

    # 2. Stale/remapped backend situation & alternate-space fallback refusal
    # Bob is stored with actual space 4 (SdUser).
    # A tempting replacement record with the same identity is available in User space (space 1).
    # The original retained-space mount attempt fails with injected real error.
    bob_key = "Super Game [0100000000001000]/Bob"
    alt_bob_user = SyntheticSaveInfo(
        save_data_id=0x2003,
        space_id=1,
        save_type=1,
        app_id=rec_acc_bob.application_id,
        uid=rec_acc_bob.uid,
    )
    mount_err_proxy = SyntheticFsSaveProxy(
        full_tree,
        mount_errors={
            bob_key: FS_ERROR_MOUNT_FAILED,
            "System/System [0100000000000003]": FS_ERROR_MOUNT_FAILED,
        },
        remap_candidates={bob_key: alt_bob_user},
    )

    # Assert remap_candidates registered and observable on the proxy:
    check(bob_key in mount_err_proxy.remap_candidates, "remap_candidates must be registered and observable")
    check(mount_err_proxy.remap_candidates[bob_key] is alt_bob_user, "Registered remap candidate must be alt_bob_user")
    check(mount_err_proxy.remap_candidates[bob_key].save_data_space_id == 1, "Candidate must offer User space 1")

    # Prior to mount attempt, Bob was discovered and present in directory listing:
    rc, game_dir = mount_err_proxy.open_directory("/Super Game [0100000000001000]")
    check(rc == FS_SUCCESS and "Bob" in game_dir["entries"], "Bob must be visible in listing before mount attempt")
    check(len(mount_err_proxy.attempted_routes) == 0, "Directory enumeration must not attempt any mount")

    # Attempt to open Bob's save directory:
    rc, _ = mount_err_proxy.open_directory("/Super Game [0100000000001000]/Bob")
    check(rc == FS_ERROR_MOUNT_FAILED, "Mount error must be returned unchanged")

    # The model records exactly one attempted route:
    check(len(mount_err_proxy.attempted_routes) == 1, "Exactly one mount attempt must be recorded")
    bob_attempt = mount_err_proxy.attempted_routes[0]
    check(bob_attempt[0] == bob_key, "Attempt key must match Bob's path")
    check(bob_attempt[1] == 4, "Attempt MUST route to stored space 4 (SdUser)")
    check(bob_attempt[1] != 1, "Attempt MUST NOT fall back or redirect to space 1 (User)")
    check(bob_attempt[2] == 1, "Attempt must use stored save_data_type (Account)")
    check(bob_attempt[3] == rec_acc_bob.application_id, "Attempt must use stored application_id")
    check(bob_attempt[5] == rec_acc_bob.uid, "Attempt must use stored uid")
    check(bob_attempt[8] == rec_acc_bob.save_data_id, "Attempt must use stored save_data_id")
    check(bob_attempt[9] is True, "Attempt must strictly use read_only=True")

    # Assert no attempted tuple has space_id == 1:
    check(all(r[1] != 1 for r in mount_err_proxy.attempted_routes), "No attempted route may use candidate space_id=1")

    # Assert remap candidates were never probed or used:
    check(len(mount_err_proxy.remap_attempts) == 0, "remap_attempts must remain empty")
    check(mount_err_proxy.remap_candidate_uses == 0, "remap_candidate_uses must remain 0")

    # Verify no second attempt occurred:
    check(len(mount_err_proxy.attempted_routes) == 1, "No second attempt or alternate-space retry must occur")

    # Verify tree is not rewritten or rescanned:
    check(full_tree["Super Game [0100000000001000]"]["Bob"] is rec_acc_bob, "Tree record must remain immutable")
    check(full_tree["Super Game [0100000000001000]"]["Bob"] is not alt_bob_user, "Tree must not be rewritten with alt_bob_user")
    check(full_tree["Super Game [0100000000001000]"]["Bob"].save_data_space_id == 4, "Stored space ID must remain 4 (SdUser)")

    # 3. Discovered directory presence before mount across root and level 1
    fresh_proxy = SyntheticFsSaveProxy(full_tree)
    rc, root_h = fresh_proxy.open_directory("/")
    check(rc == FS_SUCCESS and root_h["is_virtual"], "Root directory enumeration succeeds")
    check(set(root_h["entries"]) == set(full_tree.keys()), "All top-level buckets discovered")
    check(fresh_proxy.mount_count == 0, "No mount occurred for root directory enumeration")

    for top_k in full_tree:
        rc, top_h = fresh_proxy.open_directory(f"/{top_k}")
        check(rc == FS_SUCCESS and top_h["is_virtual"], f"Level 1 directory /{top_k} enumeration succeeds")
        check(set(top_h["entries"]) == set(full_tree[top_k].keys()), f"All sub-entries in /{top_k} discovered")
        check(fresh_proxy.mount_count == 0, f"No mount occurred for /{top_k} directory enumeration")

    # 4. Mount error propagation across all 5 accessors for System save
    # rec_sys3 has space_id = 101 (SafeMode), sys_save_id = 0x0100000000000003, save_data_id = 0x1003
    sys_err_proxy = SyntheticFsSaveProxy(full_tree, mount_errors={
        "System/System [0100000000000003]": FS_ERROR_MOUNT_FAILED,
    })
    err_path = "/System/System [0100000000000003]"
    expected_sys_route = (
        "System/System [0100000000000003]",
        101,  # SafeMode space_id
        0,    # FsSaveDataType_System
        0,    # application_id
        0x0100000000000003,  # system_save_data_id
        (0, 0),  # uid
        0,    # rank
        0,    # index
        0x1003,  # save_data_id
        True, # read_only
    )

    # 1. OpenFile:
    sys_err_proxy.attempted_routes.clear()
    rc, h = sys_err_proxy.open_file(f"{err_path}/slot0/savedata.bin", FS_OPEN_READ)
    check(rc == FS_ERROR_MOUNT_FAILED and h is None, "OpenFile must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "OpenFile must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "OpenFile must attempt exact stored System record without alternate space")

    # 2. OpenDirectory:
    sys_err_proxy.attempted_routes.clear()
    rc, h = sys_err_proxy.open_directory(f"{err_path}/slot0")
    check(rc == FS_ERROR_MOUNT_FAILED and h is None, "OpenDirectory must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "OpenDirectory must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "OpenDirectory must attempt exact stored System record without alternate space")

    # 3. GetEntryType:
    sys_err_proxy.attempted_routes.clear()
    rc, t = sys_err_proxy.get_entry_type(f"{err_path}/slot0/savedata.bin")
    check(rc == FS_ERROR_MOUNT_FAILED and t is None, "GetEntryType must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "GetEntryType must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "GetEntryType must attempt exact stored System record without alternate space")

    # 4. GetTotalSpace:
    sys_err_proxy.attempted_routes.clear()
    rc, total = sys_err_proxy.get_total_space(err_path)
    check(rc == FS_ERROR_MOUNT_FAILED and total == 0, "GetTotalSpace must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "GetTotalSpace must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "GetTotalSpace must attempt exact stored System record without alternate space")

    # 5. GetFreeSpace:
    sys_err_proxy.attempted_routes.clear()
    rc, free = sys_err_proxy.get_free_space(err_path)
    check(rc == FS_ERROR_MOUNT_FAILED and free == 0, "GetFreeSpace must propagate mount error unchanged")
    check(len(sys_err_proxy.attempted_routes) == 1, "GetFreeSpace must record exactly one mount attempt")
    check(sys_err_proxy.attempted_routes[0] == expected_sys_route, "GetFreeSpace must attempt exact stored System record without alternate space")

    # =========================================================================
    # Part 4: Fail-Closed Mutation Matrix, Byte Integrity, LRU Eviction & Handle Lifetime
    # =========================================================================
    p4_proxy = SyntheticFsSaveProxy(full_tree)

    # 1. Fail-closed mutation matrix across game and top-level buckets
    target_mut_paths = [
        "/Super Game [0100000000001000]/Alice/slot0/savedata.bin",
        "/System/System [0100000000000001]/slot0/savedata.bin",
        "/Temporary/0100000000005000/slot0/savedata.bin",
        "/System BCAT/System BCAT [0100000000000010]/slot0/savedata.bin",
    ]
    for mp in target_mut_paths:
        check(p4_proxy.create_file(mp, 1024) == FS_ERROR_NOT_IMPLEMENTED, "create_file must reject with NotImplemented")
        check(p4_proxy.delete_file(mp) == FS_ERROR_NOT_IMPLEMENTED, "delete_file must reject with NotImplemented")
        check(p4_proxy.rename_file(mp, mp + ".bak") == FS_ERROR_NOT_IMPLEMENTED, "rename_file must reject with NotImplemented")
        dir_p = mp.rsplit("/", 1)[0]
        check(p4_proxy.create_directory(dir_p + "/newdir") == FS_ERROR_NOT_IMPLEMENTED, "create_directory must reject with NotImplemented")
        check(p4_proxy.delete_directory_recursively(dir_p) == FS_ERROR_NOT_IMPLEMENTED, "delete_directory_recursively must reject with NotImplemented")
        check(p4_proxy.rename_directory(dir_p, dir_p + "_bak") == FS_ERROR_NOT_IMPLEMENTED, "rename_directory must reject with NotImplemented")

        rc, h = p4_proxy.open_file(mp, FS_OPEN_WRITE)
        check(rc == FS_ERROR_NOT_IMPLEMENTED and h is None, "open_file WRITE must reject before mounting")
        rc, h = p4_proxy.open_file(mp, FS_OPEN_APPEND)
        check(rc == FS_ERROR_NOT_IMPLEMENTED and h is None, "open_file APPEND must reject before mounting")
        rc, h = p4_proxy.open_file(mp, FS_OPEN_READ | FS_OPEN_WRITE)
        check(rc == FS_ERROR_NOT_IMPLEMENTED and h is None, "open_file READ|WRITE must reject before mounting")

    check(p4_proxy.mount_count == 0, "Mutations must reject before mounting save")

    # 2. ReadFile byte integrity and CloseFile without commit
    alice_file_path = "/Super Game [0100000000001000]/Alice/slot0/savedata.bin"
    rc, alice_handle = p4_proxy.open_file(alice_file_path, FS_OPEN_READ)
    check(rc == FS_SUCCESS and alice_handle is not None, "open_file READ on Alice save must succeed")
    check(p4_proxy.mount_count == 1, "Save mounted once on read open")

    fs_alice = alice_handle["fs"]
    check(fs_alice.side_effects == 0 and fs_alice.commits == 0, "No side effects on read open")

    check(p4_proxy.write_file(alice_handle, 0, b"malicious") == FS_ERROR_NOT_IMPLEMENTED, "write_file rejected")
    check(p4_proxy.set_file_size(alice_handle, 0) == FS_ERROR_NOT_IMPLEMENTED, "set_file_size rejected")

    sz = p4_proxy.get_file_size(alice_handle)
    expected_data = b"original_save_data_bytes_12345"
    check(sz == len(expected_data), f"File size mismatch: {sz} vs {len(expected_data)}")
    data = p4_proxy.read_file(alice_handle, 0, sz)
    check(data == expected_data, "Read bytes must match unaltered content")

    # 3. LRU Eviction & Open Handle Lifetime (MOUNT_CACHE_MAX = 4)
    # Alice is in p4_proxy.mounts (1 item). Let's mount 4 additional distinct saves:
    distinct_saves = [
        "/Super Game [0100000000001000]/Bob/slot0",
        "/Super Game [0100000000001000]/BCAT/slot0",
        "/System/System [0100000000000001]/slot0",
        "/Temporary/0100000000005000/slot0",
    ]
    for s_path in distinct_saves:
        rc, _ = p4_proxy.open_directory(s_path)
        check(rc == FS_SUCCESS, f"open_directory failed on {s_path}")

    # Now 5 distinct mounts have been requested. Since MOUNT_CACHE_MAX = 4, Alice must have been evicted from cache
    alice_key = "Super Game [0100000000001000]/Alice"
    check(len(p4_proxy.mounts) == 4, f"Mount cache size must be capped at 4, got {len(p4_proxy.mounts)}")
    check(alice_key not in p4_proxy.mounts, "Alice mount must have been evicted from LRU cache")

    # Verify open file handle still accesses valid native fs and reads unaltered bytes
    data_after_evict = p4_proxy.read_file(alice_handle, 0, sz)
    check(data_after_evict == expected_data, "Read from open handle after LRU eviction must succeed unaltered")

    # CloseFile does not commit or alter data
    p4_proxy.close_file(alice_handle)
    check(alice_handle["closed"] is True, "CloseFile marks handle closed")
    check(fs_alice.commits == 0, "CloseFile must not call commit")
    check(fs_alice.side_effects == 0, "CloseFile must have 0 side effects")

    print("Synthetic behavioral model checks: ALL PASS (22 fail-closed, multi-type, and LRU groups)")

if __name__ == "__main__":
    test_source_contracts()
    test_behavioral_model()
    print("ALL MTP SAVE CONTRACT AND BEHAVIORAL REGRESSIONS PASSED")
