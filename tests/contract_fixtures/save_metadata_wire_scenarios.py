# Connected scenario tests for save metadata wire contract.
import struct
import io
import zipfile
from contract_fixtures.save_metadata_wire_models import (
    RES_OK, ERR_CORRUPT_METADATA, ERR_IDENTITY_REFUSED, ERR_TARGET_LOCKED,
    ERR_CANNOT_RESTORE_TO_SYSTEM, ERR_INDEX_MISMATCH, ERR_APP_MISMATCH,
    ERR_NO_VALID_METADATA, ERR_DISA_NOT_SUPPORTED, META_FORMAT_JKSV85,
    META_FORMAT_JKSV86, META_FORMAT_SPHAIRA128, META_FORMAT_DBI512,
    pack_jksv85, pack_jksv_tail86, pack_jksv_middle86, pack_sphaira128,
    pack_dbi_raw512, ReferenceSaveMetadataDecoder, make_zip,
    ConnectedAdmissionLifecycleModel
)

def check(condition: bool, msg: str) -> None:
    if not condition:
        raise AssertionError(msg)

def test_connected_admission_and_failures() -> None:
    print("[2] Running connected admission model & injected failure regressions...")

    valid_payload = make_zip({
        ".nx_save_meta.bin": pack_jksv85(),
        "save.dat": b"live_data_chunk"
    })
    target_live = {
        "save_data_id": 0x12345678,
        "application_id": 0x0100000000010000,
        "save_data_space_id": 1,
        "save_data_type": 1,
        "save_data_index": 0,
        "uid": (0x1111222233334444, 0x5555666677778888),
        "live_data_size": 0x200000,
        "live_journal_size": 0x200000
    }

    # Baseline success
    ok, ev, _ = ConnectedAdmissionLifecycleModel.execute_restore(valid_payload, target_live)
    check(ok, "Baseline valid restore must succeed")
    expected_ev = ["open", "preflight", "metadata", "selected-live", "recovery", "clear", "extract", "fresh-RO", "source-close"]
    check(ev == expected_ev, f"Events must strictly match full connected sequence: {ev}")

    # Overflow preflight failure: must fail before metadata
    ok_ov, ev_ov, _ = ConnectedAdmissionLifecycleModel.execute_restore(
        valid_payload, target_live, injected_failure="overflow"
    )
    check(not ok_ov, "Overflow preflight must reject restore")
    check("metadata" not in ev_ov, f"Overflow preflight must fail before metadata (got {ev_ov})")
    check("clear" not in ev_ov, f"Overflow preflight MUST NOT reach 'clear' (got {ev_ov})")

    # Injected metadata failures: must occur after successful preflight
    injected_meta_cases = [
        "short-read", "read-error", "overrun", "termination", "rewind"
    ]
    for failure in injected_meta_cases:
        res_ok, res_ev, _ = ConnectedAdmissionLifecycleModel.execute_restore(
            valid_payload, target_live, injected_failure=failure
        )
        check(not res_ok, f"Injected failure '{failure}' must reject admission")
        check("preflight" in res_ev, f"Metadata failure '{failure}' must happen after successful preflight (got {res_ev})")
        check("metadata" in res_ev, f"Metadata failure '{failure}' must reach metadata gate (got {res_ev})")
        check("selected-live" not in res_ev, f"Metadata failure '{failure}' must not reach selected-live (got {res_ev})")
        check("clear" not in res_ev, f"Injected failure '{failure}' MUST NOT reach 'clear' (events={res_ev})")
        check("extract" not in res_ev, f"Injected failure '{failure}' MUST NOT reach 'extract'")
        check("fresh-RO" not in res_ev, f"Injected failure '{failure}' MUST NOT reach 'fresh-RO'")

    # Open error: fails before preflight and metadata
    ok_open, ev_open, _ = ConnectedAdmissionLifecycleModel.execute_restore(
        valid_payload, target_live, injected_failure="open-error"
    )
    check(not ok_open, "Open error must reject restore")
    check("preflight" not in ev_open and "metadata" not in ev_open and "clear" not in ev_open,
          "Open error must fail before preflight/metadata/clear")

    # Zero save_data_id target: rejected at selected-live before recovery and clear
    bad_target = dict(target_live)
    bad_target["save_data_id"] = 0
    ok_bad_target, ev_bad_target, _ = ConnectedAdmissionLifecycleModel.execute_restore(
        valid_payload, bad_target
    )
    check(not ok_bad_target, "Zero save_data_id target must be rejected")
    check("selected-live" in ev_bad_target, "Zero save_data_id target reaches selected-live check")
    check("recovery" not in ev_bad_target, "Zero save_data_id target must not reach recovery")
    check("clear" not in ev_bad_target, "Zero save_data_id target MUST NOT reach clear")

    # Recovery candidate invalid: MUST NOT reach 'clear'
    rec_ok, rec_ev, _ = ConnectedAdmissionLifecycleModel.execute_restore(
        valid_payload, target_live, recovery_candidate_valid=False
    )
    check(not rec_ok, "Invalid recovery candidate must reject restore")
    check("recovery" in rec_ev, "Invalid recovery candidate reaches recovery gate")
    check("clear" not in rec_ev, "Invalid recovery candidate MUST NOT reach 'clear'")

    # Close failures after mutation
    for close_fail in ("owner-close", "callback-close"):
        c_ok, c_ev, _ = ConnectedAdmissionLifecycleModel.execute_restore(
            valid_payload, target_live, injected_failure=close_fail
        )
        check(not c_ok, f"Close failure '{close_fail}' must report error")
        check("clear" in c_ev, f"Close failure occurs after clear")

    print("  -> Connected admission model & injected failures PASSED (all pre-mutation refusals blocked before clear).")


# ==============================================================================
# 4. Destination Identity Preservation & Account UID Remap Contract
# ==============================================================================

def test_account_uid_remap_contract() -> None:
    print("[3] Running Account UID remap & destination identity preservation tests...")

    # Source metadata has User A attributes
    uid_a_low = 0x1111222233334444
    uid_a_high = 0x5555666677778888
    source_app_id = 0x0100000000010000
    source_raw = pack_jksv_tail86(
        app_id=source_app_id,
        uid_low=uid_a_low,
        uid_high=uid_a_high,
        owner_id=0x0100000000010055,
        save_type=1,
        index=3,
        source_space=1,
        data_size=0x100000,
        journal_size=0x100000
    )
    source_zip = make_zip({".nx_save_meta.bin": source_raw, "save.dat": b"user_a_save"})

    # Destination is an existing live save on User B
    uid_b_low = 0xAAAABBBBCCCCDDDD
    uid_b_high = 0xEEEEFFFF00001111
    selected_dest_app_id = 0x0100000000099999
    selected_live_target = {
        "save_data_id": 0x9999888877776666,  # Existing live save!
        "application_id": selected_dest_app_id,
        "save_data_space_id": 4,              # ProperSystem space
        "save_data_type": 1,                  # Account
        "save_data_index": 0,
        "uid": (uid_b_low, uid_b_high),       # User B
        "live_data_size": 0x500000,           # Live size
        "live_journal_size": 0x500000         # Live journal size
    }

    # Execute restore
    ok, events, resolved = ConnectedAdmissionLifecycleModel.execute_restore(source_zip, selected_live_target)
    check(ok, "Restore into live target must succeed admission")
    check("clear" in events, "Valid restore reaches clear")

    # Invariant checks: User B identity, actual space, and live sizes MUST BE PRESERVED
    check(resolved["uid"][0] == uid_b_low, f"Destination UID low half must be UID B (got {hex(resolved['uid'][0])})")
    check(resolved["uid"][1] == uid_b_high, f"Destination UID high half must be UID B (got {hex(resolved['uid'][1])})")
    check(resolved["uid"] != (uid_a_low, uid_a_high), "Source UID A must NOT overwrite destination UID B")
    check(resolved["application_id"] == selected_dest_app_id, "Selected app ID must be preserved")
    check(resolved["save_data_space_id"] == 4, "Selected destination space (4) must be preserved")
    check(resolved["save_data_index"] == 0, "Selected destination index (0) must be preserved")
    check(resolved["data_size"] == 0x500000, "Live data size must be preserved")
    check(resolved["journal_size"] == 0x500000, "Live journal size must be preserved")

    # Unbacked target (save_data_id == 0) must be rejected; creation-from-backup is out of scope
    unbacked_target = {
        "save_data_id": 0,
        "application_id": selected_dest_app_id,
        "save_data_space_id": 4,
        "save_data_type": 1,
        "save_data_index": 0,
        "uid": (uid_b_low, uid_b_high),
        "live_data_size": 0x500000,
        "live_journal_size": 0x500000
    }
    ok_unbacked, ev_unbacked, _ = ConnectedAdmissionLifecycleModel.execute_restore(source_zip, unbacked_target)
    check(not ok_unbacked, "Target with save_data_id == 0 must be rejected")
    check("clear" not in ev_unbacked, "Rejected zero-ID target must not reach clear")

    print("  -> Account UID remap & destination identity preservation PASSED.")


# ==============================================================================
# 5. Embedded Index 0 vs Conflicting Filename in Discovery Model
# ==============================================================================

class ReferenceBackupDiscoveryModel:
    """Models InspectBackupArchive and DbiBackupMatchesEntry precedence."""

    @classmethod
    def inspect_backup_archive(cls, filename: str, zip_bytes: bytes) -> dict | None:
        out = {
            "application_id": 0, "save_data_type": 0xFF,
            "save_data_index": 0, "loaded_from_meta": False
        }

        # Precedence 1: ReadArchiveSaveMetadata
        meta_status, meta = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_bytes)
        if meta_status == "Invalid":
            # Present invalid metadata: fail closed, NO fallback!
            return None

        if meta_status == "Valid":
            out["application_id"] = meta["application_id"]
            out["save_data_type"] = meta["save_data_type"]
            out["save_data_index"] = meta["save_data_index"]
            out["loaded_from_meta"] = True
            return out

        # Precedence 2: Filename parsing if not loaded from meta
        # Parse DBI filename: e.g. 0100000000010000_1_00000000000000000000000000000000_15.zip
        parts = filename.split("_")
        if len(parts) >= 4:
            try:
                out["application_id"] = int(parts[0], 16)
                out["save_data_type"] = int(parts[1])
                idx_str = parts[3].split(".")[0]
                out["save_data_index"] = int(idx_str)
            except Exception:
                pass
        return out

    @classmethod
    def dbi_backup_matches_entry(cls, zip_bytes: bytes, entry: dict) -> bool:
        meta_status, meta = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_bytes)
        if meta_status == "Invalid":
            return False
        if meta_status == "Valid":
            if meta["save_data_type"] != entry["save_data_type"]:
                return False
            if meta["application_id"] != entry["application_id"]:
                return False
            if meta["save_data_index"] != entry["save_data_index"]:
                return False
            return True
        return False


def test_embedded_index0_vs_filename_discovery() -> None:
    print("[4] Running embedded index 0 vs conflicting filename discovery tests...")

    # Embedded metadata has index 0
    raw_meta_idx0 = pack_jksv85(app_id=0x0100000000010000, index=0)
    zip_with_idx0 = make_zip({".nx_save_meta.bin": raw_meta_idx0})

    # Filename suggests index 15
    conflicting_filename = "0100000000010000_1_00000000000000000000000000000000_15.zip"

    disc = ReferenceBackupDiscoveryModel.inspect_backup_archive(conflicting_filename, zip_with_idx0)
    check(disc is not None, "Discovery inspection must succeed")
    check(disc["loaded_from_meta"], "Discovery must load from embedded archive metadata")
    check(disc["save_data_index"] == 0,
          f"Embedded metadata index 0 must override filename hint 15 (got index {disc['save_data_index']})")

    # Matcher check
    entry_idx0 = {"application_id": 0x0100000000010000, "save_data_type": 1, "save_data_index": 0}
    entry_idx15 = {"application_id": 0x0100000000010000, "save_data_type": 1, "save_data_index": 15}
    check(ReferenceBackupDiscoveryModel.dbi_backup_matches_entry(zip_with_idx0, entry_idx0),
          "Archive with embedded index 0 must match entry with index 0")
    check(not ReferenceBackupDiscoveryModel.dbi_backup_matches_entry(zip_with_idx0, entry_idx15),
          "Archive with embedded index 0 must NOT match entry with index 15")

    # If metadata is present but corrupt: fail closed, NO fallback to filename
    corrupt_meta_zip = make_zip({".nx_save_meta.bin": b"\x00" * 50})
    disc_bad = ReferenceBackupDiscoveryModel.inspect_backup_archive(conflicting_filename, corrupt_meta_zip)
    check(disc_bad is None, "Corrupt embedded metadata must fail closed without fallback to filename fields")
    check(not ReferenceBackupDiscoveryModel.dbi_backup_matches_entry(corrupt_meta_zip, entry_idx0),
          "Corrupt embedded metadata must fail matcher closed")

    print("  -> Embedded index 0 vs conflicting filename discovery PASSED.")


# ==============================================================================
# 6. Legacy Recovery Roundtrip & Metadata-Free Leading-Slash DBI
# ==============================================================================

def test_legacy_recovery_and_leading_slash_dbi() -> None:
    print("[5] Running legacy recovery roundtrip & metadata-free leading-slash DBI tests...")

    # 1. Legacy recovery candidate validation
    legacy_rec_zip = make_zip({
        ".nx_save_meta.bin": pack_sphaira128(app_id=0x0100000000010000),
        "save.dat": b"recovery_snapshot"
    })
    st_rec, m_rec = ReferenceSaveMetadataDecoder.read_archive_metadata(legacy_rec_zip)
    check(st_rec == "Valid" and m_rec is not None,
          "Legacy Sphaira 128 recovery archive must decode as Valid")

    # 2. Metadata-free leading-slash DBI archive
    leading_slash_zip = make_zip({
        "/.dbi_save_info.ini": b"[SaveInfo]\nTitleId=0100000000010000\n",
        "/save.dat": b"pure_payload_data"
    })
    st_ls, m_ls = ReferenceSaveMetadataDecoder.read_archive_metadata(leading_slash_zip)
    check(st_ls == "NoMetadata" and m_ls is None,
          "Archive with leading slash and only .dbi_save_info.ini must return NoMetadata")

    # Discovery falls back to filename
    filename_dbi = "0100000000010000_1_00000000000000000000000000000000_00.zip"
    disc_ls = ReferenceBackupDiscoveryModel.inspect_backup_archive(filename_dbi, leading_slash_zip)
    check(disc_ls is not None and not disc_ls["loaded_from_meta"],
          "Metadata-free archive must fall back cleanly to filename discovery")
    check(disc_ls["application_id"] == 0x0100000000010000 and disc_ls["save_data_type"] == 1,
          "Filename fields must be parsed on NoMetadata fallback")

    # save_filter with leading-slash and nested payload
    def sim_save_filter(entry_name: str) -> bool:
        norm = entry_name.lstrip("/")
        return not ReferenceSaveMetadataDecoder.is_reserved_root(norm)

    check(not sim_save_filter("/.dbi_save_info.ini"), "Leading-slash root INI must be filtered")
    check(not sim_save_filter("/.NX_SAVE_META.BIN"), "Leading-slash uppercase NX meta must be filtered")
    check(sim_save_filter("/save.dat"), "Leading-slash regular payload must be kept")
    check(sim_save_filter("/nested/.nx_save_meta.bin"), "Nested metadata file must remain payload")

    print("  -> Legacy recovery roundtrip & metadata-free leading-slash DBI PASSED.")


# ==============================================================================
# 7. Synthetic Behavioral Reference Fixtures
# ==============================================================================
