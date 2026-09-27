# Suite 10: Top-level game grouping and safe batch admission contract
from typing import List, Dict, Any, Optional, Tuple, Set
from contract_fixtures.save_backup_library_models import (
    FS_SAVE_DATA_TYPE_ACCOUNT, FS_SAVE_DATA_TYPE_DEVICE, FS_SAVE_DATA_TYPE_BCAT,
    FS_SAVE_DATA_TYPE_SYSTEM, is_system_like, EntryModel, check
)

class MockBackupEntry:
    def __init__(self, app_id: int, save_data_type: int, uid: str = "",
                 slot: int = 0, rank: int = 0, source: str = "Kefir Hub",
                 timestamp: int = 0, name: str = "", path: str = ""):
        self.application_id = app_id
        self.save_data_type = save_data_type
        self.uid = uid
        self.save_data_index = slot
        self.save_data_rank = rank
        self.backup_source = source
        self.backup_timestamp = timestamp
        self.name = name
        self.backup_path = path
        self.is_backup = True
        self.is_game_parent = False
        self.children: List['MockBackupEntry'] = []
        self.backup_members = [{"ts": timestamp, "path": path, "is_directory": False}] if path else []

def scan_backups_game_grouping_model(backups: List[MockBackupEntry], category_is_backups: bool, app_id_filter: int = 0) -> List[MockBackupEntry]:
    if not category_is_backups or app_id_filter != 0:
        return list(backups)

    app_order: List[int] = []
    app_groups: Dict[int, List[MockBackupEntry]] = {}
    system_entries: List[MockBackupEntry] = []

    for b in backups:
        if is_system_like(b.save_data_type) or b.application_id == 0:
            system_entries.append(b)
        else:
            if b.application_id not in app_groups:
                app_order.append(b.application_id)
                app_groups[b.application_id] = []
            app_groups[b.application_id].append(b)

    out: List[MockBackupEntry] = []
    for app_id in app_order:
        children = app_groups[app_id]
        if not children:
            continue
        parent = MockBackupEntry(app_id=app_id, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT)
        parent.is_backup = True
        parent.is_game_parent = True
        parent.name = children[0].name or f"Title {app_id:016X}"

        newest_ts = 0
        first_child = True
        multi_source = False
        common_source = "Other"

        for c in children:
            if c.backup_timestamp > newest_ts:
                newest_ts = c.backup_timestamp
                parent.backup_path = c.backup_path
            if first_child:
                common_source = c.backup_source
                first_child = False
            elif c.backup_source != common_source:
                multi_source = True

        parent.backup_timestamp = newest_ts
        parent.backup_source = "Other" if multi_source else common_source
        parent.children = list(children)
        out.append(parent)

    out.extend(system_entries)
    return out

def expand_game_groups_model(entries: List[MockBackupEntry]) -> List[MockBackupEntry]:
    out: List[MockBackupEntry] = []
    for e in entries:
        if e.is_game_parent and e.children:
            out.extend(e.children)
        else:
            out.append(e)
    return out

def restore_all_for_game_model(game: MockBackupEntry, live_slots: Dict[Tuple[int, int, str, int], Any]) -> Tuple[bool, str, List[Tuple[MockBackupEntry, Any]]]:
    """
    Validates candidates and resolves targets safely.
    live_slots key: (app_id, save_data_type, uid, slot)
    """
    if not game.children:
        return False, "No backups found for this game.", []

    for child in game.children:
        if not child.backup_members:
            child_desc = "Account" if child.save_data_type == FS_SAVE_DATA_TYPE_ACCOUNT else ("Device" if child.save_data_type == FS_SAVE_DATA_TYPE_DEVICE else "BCAT")
            return False, f"Cannot restore: no admissible backup archive found for {child_desc}", []

    valid_children = list(game.children)

    # Check Device / BCAT children first for existing live candidates
    for child in valid_children:
        if child.save_data_type != FS_SAVE_DATA_TYPE_ACCOUNT:
            key = (child.application_id, child.save_data_type, "", child.save_data_index)
            if key not in live_slots:
                type_name = "Device" if child.save_data_type == FS_SAVE_DATA_TYPE_DEVICE else "BCAT"
                return False, f"Cannot restore: compatible live save slot is missing for {type_name}", []

    # Resolve targets and check for duplicate target collisions
    resolved: List[Tuple[MockBackupEntry, Any]] = []
    seen_keys: Set[str] = set()

    for child in valid_children:
        if child.save_data_type != FS_SAVE_DATA_TYPE_ACCOUNT:
            target_key = f"{child.application_id}:{child.save_data_type}::{child.save_data_index}"
            target_slot = live_slots[(child.application_id, child.save_data_type, "", child.save_data_index)]
        else:
            # Account slot
            target_key = f"{child.application_id}:{child.save_data_type}:{child.uid}:{child.save_data_index}"
            target_slot = live_slots.get((child.application_id, child.save_data_type, child.uid, child.save_data_index), {
                "is_planned_create": True, "uid": child.uid, "slot": child.save_data_index
            })

        if target_key in seen_keys:
            return False, "Duplicate restore target slot selected.", []
        seen_keys.add(target_key)
        resolved.append((child, target_slot))

    return True, "OK", resolved

def test_suite_10_game_grouping_and_batch_admission() -> None:
    print("[11] Running Suite 10: Top-level Game Grouping & Safe Batch Admission Contract...")

    # Case 1: Multiple backups of Game 1 across sources (Kefir Hub, DBI) and types (Account, Device)
    # Plus Game 2 and a System save
    backups = [
        MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT, uid="user1", slot=0, rank=0, source="Kefir Hub", timestamp=1000, name="Zelda", path="/switch/saves/z1.zip"),
        MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT, uid="user2", slot=0, rank=0, source="DBI", timestamp=1200, name="Zelda", path="/switch/saves/z2.zip"),
        MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_DEVICE, uid="", slot=0, rank=0, source="JKSV", timestamp=1100, name="Zelda", path="/switch/saves/zd.zip"),
        MockBackupEntry(app_id=0x0100000000020000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT, uid="user1", slot=0, rank=0, source="Kefir Hub", timestamp=900, name="Mario", path="/switch/saves/m1.zip"),
        MockBackupEntry(app_id=0x0000000000000000, save_data_type=FS_SAVE_DATA_TYPE_SYSTEM, uid="", slot=0, rank=0, source="Kefir Hub", timestamp=800, name="System Save", path="/switch/saves/sys.zip"),
    ]

    # Category Backups: must group by application_id into 2 game parents + 1 system save
    grouped = scan_backups_game_grouping_model(backups, category_is_backups=True, app_id_filter=0)
    check(len(grouped) == 3, f"Expected 3 top-level items, got {len(grouped)}")

    # Zelda game parent
    zelda = grouped[0]
    check(zelda.is_game_parent, "Zelda must be a game parent")
    check(zelda.application_id == 0x0100000000010000, "Zelda app id must match")
    check(len(zelda.children) == 3, f"Zelda must contain 3 children, got {len(zelda.children)}")
    check(zelda.backup_timestamp == 1200, f"Zelda parent timestamp must be newest (1200), got {zelda.backup_timestamp}")
    check(zelda.backup_source == "Other", f"Multi-source Zelda parent must have 'Other' source, got {zelda.backup_source}")

    # Mario game parent
    mario = grouped[1]
    check(mario.is_game_parent, "Mario must be a game parent")
    check(mario.application_id == 0x0100000000020000, "Mario app id must match")
    check(len(mario.children) == 1, "Mario must contain 1 child")
    check(mario.backup_source == "Kefir Hub", f"Single-source Mario parent must have 'Kefir Hub', got {mario.backup_source}")

    # System save: must remain standalone
    sys_save = grouped[2]
    check(not sys_save.is_game_parent, "System save must not be a game parent")
    check(sys_save.save_data_type == FS_SAVE_DATA_TYPE_SYSTEM, "System save type preserved")

    # Mixed saves or live saves category: must preserve individual entries
    mixed = scan_backups_game_grouping_model(backups, category_is_backups=False, app_id_filter=0)
    check(len(mixed) == 5, f"Mixed category must not group game parents, got {len(mixed)}")

    # ExpandGameGroups: expands parents back to individual children
    expanded = expand_game_groups_model(grouped)
    check(len(expanded) == 5, f"Expanded groups must yield original 5 entries, got {len(expanded)}")
    check(all(not e.is_game_parent for e in expanded), "No expanded entry may remain a game parent")

    # Case 2: Restore all - Device missing live candidate fails closed before mutation
    live_slots_no_device = {
        (0x0100000000010000, FS_SAVE_DATA_TYPE_ACCOUNT, "user1", 0): {"save_data_id": 101},
        (0x0100000000010000, FS_SAVE_DATA_TYPE_ACCOUNT, "user2", 0): {"save_data_id": 102},
    }
    ok, err_msg, targets = restore_all_for_game_model(zelda, live_slots_no_device)
    check(not ok, "Restore all must fail closed when Device save has no live candidate")
    check("Cannot restore: compatible live save slot is missing for Device" in err_msg,
          f"Expected Device missing error message, got: {err_msg}")
    check(len(targets) == 0, "No targets may be resolved when pre-validation fails")

    # Case 3: Restore all - All candidates valid (Account creates planned slot, Device exists)
    live_slots_with_device = {
        (0x0100000000010000, FS_SAVE_DATA_TYPE_ACCOUNT, "user1", 0): {"save_data_id": 101},
        (0x0100000000010000, FS_SAVE_DATA_TYPE_DEVICE, "", 0): {"save_data_id": 103},
    }
    ok, msg, targets = restore_all_for_game_model(zelda, live_slots_with_device)
    check(ok, f"Restore all should succeed when live targets exist, got error: {msg}")
    check(len(targets) == 3, f"Expected 3 resolved targets, got {len(targets)}")
    # user1 has existing live slot, user2 is planned create, device has existing live slot
    check(targets[0][1]["save_data_id"] == 101, "Target 0 must map to live slot 101")
    check(targets[1][1].get("is_planned_create") is True, "Target 1 (user2) must be planned create")
    check(targets[2][1]["save_data_id"] == 103, "Target 2 (Device) must map to live slot 103")

    # Case 4: Restore all - Duplicate target collision detection
    zelda_duplicate = MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT)
    zelda_duplicate.is_game_parent = True
    zelda_duplicate.children = [
        MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT, uid="user1", slot=0, path="/z1.zip"),
        MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT, uid="user1", slot=0, path="/z2.zip"),
    ]
    ok_dup, err_dup, _ = restore_all_for_game_model(zelda_duplicate, live_slots_with_device)
    check(not ok_dup, "Duplicate target selection must be rejected")
    check(err_dup == "Duplicate restore target slot selected.", f"Unexpected duplicate msg: {err_dup}")

    # Case 5: Restore all - Child with no admissible archives blocks entire batch (no silent omission)
    zelda_missing_archive = MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT)
    zelda_missing_archive.is_game_parent = True
    zelda_missing_archive.children = [
        MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT, uid="user1", slot=0, path="/z1.zip"),
        MockBackupEntry(app_id=0x0100000000010000, save_data_type=FS_SAVE_DATA_TYPE_ACCOUNT, uid="user2", slot=0, path=""),  # empty path -> empty backup_members
    ]
    ok_empty, err_empty, targets_empty = restore_all_for_game_model(zelda_missing_archive, live_slots_with_device)
    check(not ok_empty, "Restore all must fail closed when a child has no admissible archive")
    check("Cannot restore: no admissible backup archive found for Account" in err_empty,
          f"Expected missing archive error msg, got: {err_empty}")
    check(len(targets_empty) == 0, "No targets may be resolved when a child has no archives")

    print("  -> Suite 10 (Game Grouping & Safe Batch Admission) PASSED.")
