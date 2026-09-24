#!/usr/bin/env python3
"""
Test Suite: Save Metadata Wire Compatibility Contract (Sphaira v0.13.856)

Comprehensive verification for JKSV / DBI / Sphaira save metadata wire compatibility:
1. Static source contracts:
   - save_paths.hpp:
     - JKSV_SAVE_META_MAGIC (0x56534B4A), JKSV_SAVE_META_REVISION (1).
     - SaveReservedMetaKind enum (None, NxMeta, DbiExtra, DbiInfo).
     - ClassifySaveReservedMetadataRoot, IsSaveReservedMetadataRoot declarations.
     - ArchiveMetaStatus enum (NoMetadata, Valid, Invalid).
     - DecodedSaveMetadata structure with source metadata semantics.
     - ReadArchiveSaveMetadata declaration.
   - save_paths.cpp:
     - Declaration order: CompareCommonSourceFields defined before DecodeJksv86WithAmbiguityCheck.
     - Bounded little-endian decoders: ReadU8, ReadU16LE, ReadU32LE, ReadU64LE, ReadS64LE.
     - Layout decoders: DecodeJksv85, DecodeJksvTail86, DecodeJksvMiddle86,
       DecodeJksv86WithAmbiguityCheck, DecodeSphaira128, DecodeDbiRaw512.
     - ReadArchiveSaveMetadata implementation:
       - Uses ClassifySaveReservedMetadataRoot.
       - Directory attributes check: ((info.external_fa & 0x10) != 0 || ((info.external_fa >> 16) & 0xF000) == 0x4000).
       - Rejection of parent directory conflicts (root_kind != None with slash).
       - Upfront unsupported size checks (85, 86, 128 for NX; 512 for Extra).
       - Byte accounting and CRC error (UNZ_CRCERROR -> 0x8) checks.
       - No 64KiB ceiling on DBI INI file.
       - Checked rewind with unzGoToFirstFile(zfile) == UNZ_OK.
       - Traversal termination check: unzGoToNextFile(zfile) == UNZ_END_OF_LIST_OF_FILE.
     - DbiBackupMatchesEntry and InspectBackupArchive:
       - Explicit checked close: unzClose(zfile) == UNZ_OK && !reader_ctx.HasError().
       - Fail closed on ArchiveMetaStatus::Invalid without fallback.
   - save_menu_ops.cpp:
     - save_filter executable source calls IsSaveReservedMetadataRoot(name.s).
     - Order of gates in RestoreSaveZip:
       unzOpen2_64 -> TransferUnzipPreflight -> ReadArchiveSaveMetadata ->
       Invalid metadata refusal -> destination resolution -> native check ->
       recovery generation/validation -> clear/commit gate -> extraction.
     - Dead dbi_extra variable and branch removed.
     - Preserves existing destination identity, space, sizes when e.save_data_id != 0.
   - Shared callers:
     - DbiBackupMatchesEntry, InspectBackupArchive, RestoreSaveZip (source & recovery)
       all delegate to ReadArchiveSaveMetadata.
   - CMakeLists.txt:
     - sphaira_VERSION bumped to 0.13.856.
2. Connected admission lifecycle model:
   - Event sequence: open -> preflight -> metadata -> selected-live -> recovery -> clear -> extract -> fresh-RO -> source-close.
   - Injected failure modes: short-read, read-error, overrun, overflow, termination, rewind, owner-close, callback-close.
   - Invariant: EVERY pre-mutation refusal stops before clear/mutation.
3. Destination identity preservation & UID remap:
   - Account source UID A -> destination UID B with both halves checked.
   - Selected ID, actual space, and live data+journal sizes unchanged when e.save_data_id != 0.
   - Unbacked targets (save_data_id == 0) rejected before mutation; creation-from-backup is out of scope.
4. Valid embedded index 0 vs actual conflicting filename in discovery model:
   - Precedence 1 archive metadata preserves index 0 over DBI filename hints.
5. Legacy recovery roundtrip and metadata-free leading-slash DBI:
   - Legacy Sphaira 128 roundtrip validation.
   - Metadata-free leading-slash archive returns NoMetadata and falls through cleanly.
6. Synthetic behavioral reference fixtures:
   - JKSV 85, tail-86, middle-86, unconditional divergent 86 fixture.
   - Sphaira 128, DBI raw 512, upfront size checks, boundary checks, CRC error.

NO C++ COMPILATION, NO BINARIES, NO NRO, NO WSL REQUIRED. Pure Python stdlib.
"""

import io
import os
import sys
import struct
import zipfile

def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ==============================================================================
# 1. Static Source Contracts & Gate Order Verifications
# ==============================================================================

def test_source_contracts() -> None:
    print("[1] Running static source contract & gate order checks for v0.13.856...")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # 1.1 save_paths.hpp declarations
    hpp_path = os.path.join(repo_root, "sphaira", "include", "ui", "menus", "save", "save_paths.hpp")
    with open(hpp_path, "r", encoding="utf-8") as f:
        hpp_src = f.read()

    check("constexpr u32 JKSV_SAVE_META_MAGIC = 0x56534B4A;" in hpp_src,
          "save_paths.hpp must define JKSV_SAVE_META_MAGIC as 0x56534B4A")
    check("constexpr u8 JKSV_SAVE_META_REVISION = 1;" in hpp_src,
          "save_paths.hpp must define JKSV_SAVE_META_REVISION as 1")
    check("enum class SaveReservedMetaKind" in hpp_src,
          "save_paths.hpp must declare SaveReservedMetaKind enum")
    check("None," in hpp_src and "NxMeta," in hpp_src and "DbiExtra," in hpp_src and "DbiInfo" in hpp_src,
          "SaveReservedMetaKind must have None, NxMeta, DbiExtra, DbiInfo")
    check("ClassifySaveReservedMetadataRoot(std::string_view name) -> SaveReservedMetaKind" in hpp_src,
          "save_paths.hpp must declare ClassifySaveReservedMetadataRoot")
    check("IsSaveReservedMetadataRoot(std::string_view name) -> bool" in hpp_src,
          "save_paths.hpp must declare IsSaveReservedMetadataRoot")
    check("enum class ArchiveMetaStatus" in hpp_src,
          "save_paths.hpp must declare ArchiveMetaStatus enum")
    check("NoMetadata" in hpp_src and "Valid" in hpp_src and "Invalid" in hpp_src,
          "ArchiveMetaStatus must have NoMetadata, Valid, Invalid values")
    check("struct DecodedSaveMetadata" in hpp_src,
          "save_paths.hpp must declare DecodedSaveMetadata")
    check("ReadArchiveSaveMetadata(" in hpp_src and "-> ArchiveMetaStatus" in hpp_src,
          "save_paths.hpp must declare ReadArchiveSaveMetadata")

    # 1.2 Save metadata and backup inspection implementations & declaration order
    paths_src = ""
    for unit in ["save_metadata_decoders.cpp", "save_archive_metadata.cpp", "save_backup_inspection.cpp"]:
        with open(os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", unit), "r", encoding="utf-8") as f:
            paths_src += f.read() + "\n"

    # BLOCKER 1 check: CompareCommonSourceFields defined before DecodeJksv86WithAmbiguityCheck
    pos_compare = paths_src.find("auto CompareCommonSourceFields(")
    pos_ambiguity = paths_src.find("auto DecodeJksv86WithAmbiguityCheck(")
    check(pos_compare != -1, "CompareCommonSourceFields definition must exist")
    check(pos_ambiguity != -1, "DecodeJksv86WithAmbiguityCheck definition must exist")
    check(pos_compare < pos_ambiguity,
          "CompareCommonSourceFields must be declared/defined BEFORE DecodeJksv86WithAmbiguityCheck")

    check("ReadU16LE" in paths_src and "ReadU32LE" in paths_src and "ReadU64LE" in paths_src and "ReadS64LE" in paths_src,
          "save_paths.cpp must provide bounded little-endian decoders")
    check("DecodeJksv85(" in paths_src, "save_paths.cpp must implement DecodeJksv85")
    check("DecodeJksvTail86(" in paths_src, "save_paths.cpp must implement DecodeJksvTail86")
    check("DecodeJksvMiddle86(" in paths_src, "save_paths.cpp must implement DecodeJksvMiddle86")
    check("DecodeSphaira128(" in paths_src, "save_paths.cpp must implement DecodeSphaira128")
    check("DecodeDbiRaw512(" in paths_src, "save_paths.cpp must implement DecodeDbiRaw512")
    check("ToNXSaveMeta(" in paths_src, "save_paths.cpp must implement ToNXSaveMeta")
    check("ReadArchiveSaveMetadata(" in paths_src,
          "save_paths.cpp must implement ReadArchiveSaveMetadata")

    rasm_pos = paths_src.find("auto ReadArchiveSaveMetadata(")
    check(rasm_pos != -1, "ReadArchiveSaveMetadata definition must exist")
    rasm_body = paths_src[rasm_pos:paths_src.find("auto DbiBackupMatchesEntry(", rasm_pos)]
    check("ClassifySaveReservedMetadataRoot" in rasm_body,
          "ReadArchiveSaveMetadata must use ClassifySaveReservedMetadataRoot")
    check("seen_nx_meta" in rasm_body and "seen_dbi_extra" in rasm_body and "seen_dbi_info" in rasm_body,
          "ReadArchiveSaveMetadata must track seen metadata entries")
    check("FsError_PathAlreadyExists" in rasm_body,
          "ReadArchiveSaveMetadata must reject duplicate metadata entries with FsError_PathAlreadyExists")
    check("FsError_InvalidCharacter" in rasm_body,
          "ReadArchiveSaveMetadata must reject directory-kind or parent-conflict metadata entries with FsError_InvalidCharacter")
    check("unzGoToFirstFile(zfile)" in rasm_body,
          "ReadArchiveSaveMetadata must rewind archive state with unzGoToFirstFile")
    check("UNZ_END_OF_LIST_OF_FILE" in rasm_body,
          "ReadArchiveSaveMetadata must check traversal termination against UNZ_END_OF_LIST_OF_FILE")
    check("(info.external_fa & 0x10) != 0" in rasm_body and "((info.external_fa >> 16) & 0xF000) == 0x4000" in rasm_body,
          "ReadArchiveSaveMetadata must inspect external_fa for directory attributes")
    check("info.uncompressed_size != 85 && info.uncompressed_size != 86 && info.uncompressed_size != 128" in rasm_body,
          "ReadArchiveSaveMetadata must perform upfront unsupported size check on NX metadata")
    check("info.uncompressed_size != 512" in rasm_body,
          "ReadArchiveSaveMetadata must perform upfront unsupported size check on DBI extra metadata")
    check("close_res == UNZ_CRCERROR" in rasm_body,
          "ReadArchiveSaveMetadata must check CRC errors via UNZ_CRCERROR")

    # DbiBackupMatchesEntry uses ReadArchiveSaveMetadata and explicit checked close
    dbm_pos = paths_src.find("auto DbiBackupMatchesEntry(")
    check(dbm_pos != -1, "DbiBackupMatchesEntry definition must exist")
    dbm_end = paths_src.find("auto CollectDbiBackups(", dbm_pos)
    check(dbm_end != -1, "CollectDbiBackups must follow DbiBackupMatchesEntry")
    dbm_body = paths_src[dbm_pos:dbm_end]
    check("ReadArchiveSaveMetadata(zfile, nullptr, archive_meta, &meta_rc)" in dbm_body,
          "DbiBackupMatchesEntry must delegate to ReadArchiveSaveMetadata")
    check("const int close_res = unzClose(zfile);" in dbm_body,
          "DbiBackupMatchesEntry must perform explicit checked unzClose")
    check("if (close_res != UNZ_OK || reader_ctx.HasError())" in dbm_body,
          "DbiBackupMatchesEntry must verify close result and reader error")
    check("meta_status == ArchiveMetaStatus::Invalid" in dbm_body,
          "DbiBackupMatchesEntry must fail closed on ArchiveMetaStatus::Invalid")

    # InspectBackupArchive uses ReadArchiveSaveMetadata and explicit checked close
    iba_pos = paths_src.find("auto InspectBackupArchive(")
    check(iba_pos != -1, "InspectBackupArchive definition must exist")
    iba_end = paths_src.find("auto FormatBackupAccount(", iba_pos)
    check(iba_end != -1, "FormatBackupAccount must follow InspectBackupArchive")
    iba_body = paths_src[iba_pos:iba_end]
    check("ReadArchiveSaveMetadata(zfile, nullptr, archive_meta, &meta_rc)" in iba_body,
          "InspectBackupArchive must delegate to ReadArchiveSaveMetadata")
    check("const int close_res = unzClose(zfile);" in iba_body,
          "InspectBackupArchive must perform explicit checked unzClose")
    check("if (close_res != UNZ_OK || reader_ctx.HasError())" in iba_body,
          "InspectBackupArchive must verify close result and reader error")
    check("meta_status == ArchiveMetaStatus::Invalid" in iba_body,
          "InspectBackupArchive must fail closed on ArchiveMetaStatus::Invalid without fallback")

    # 1.3 save_restore_zip.cpp RestoreSaveZip admission/clear gates order
    ops_cpp_path = os.path.join(repo_root, "sphaira", "source", "ui", "menus", "save", "save_restore_zip.cpp")
    with open(ops_cpp_path, "r", encoding="utf-8") as f:
        ops_src = f.read()

    # BLOCKER 2 check: save_filter uses executable IsSaveReservedMetadataRoot without relying on obsolete comment
    check("IsSaveReservedMetadataRoot(name.s)" in ops_src,
          "save_restore_zip.cpp save_filter must execute IsSaveReservedMetadataRoot")

    rsz_start = ops_src.find("Result RestoreSaveZip(ProgressBox* pbox, const Entry& e, const fs::FsPath& path, fs::FsPath* out_recovery_path, bool* out_mutation_started")
    check(rsz_start != -1, "RestoreSaveZip definition must exist")
    rsi_start = ops_src.find("} // namespace sphaira::ui::menu::save", rsz_start)
    check(rsi_start != -1, "namespace end must follow RestoreSaveZip")
    rsz_body = ops_src[rsz_start:rsi_start]

    # Admission & Clear Gate Order in RestoreSaveZip:
    # 1. unzOpen2_64 (open archive)
    # 2. TransferUnzipPreflight (preflight & filter)
    # 3. ReadArchiveSaveMetadata (source metadata admission)
    # 4. meta_status == ArchiveMetaStatus::Invalid (gate refusal before mutation)
    # 5. if (e.save_data_id == 0) (destination identity resolution)
    # 6. check_save_fs (target existence check)
    # 7. ReadArchiveSaveMetadata(rec_zfile, ...) (recovery candidate admission)
    # 8. rec_meta_status == ArchiveMetaStatus::Valid (recovery admission gate)
    # 9. save_fs.Commit() / TransferUnzipAll (first mutation / clear / extract)
    pos_open = rsz_body.find("unzOpen2_64(path, &file_func)")
    pos_preflight = rsz_body.find("thread::TransferUnzipPreflight(")
    pos_meta = rsz_body.find("ReadArchiveSaveMetadata(zfile, pbox, archive_meta, &meta_rc)")
    pos_invalid_gate = rsz_body.find("if (meta_status == ArchiveMetaStatus::Invalid)")
    pos_dest_res = rsz_body.find("if (target_entry.save_data_id == 0) {")
    if pos_dest_res == -1:
        pos_dest_res = rsz_body.find("if (e.save_data_id == 0) {")
    pos_check_fs = rsz_body.find("fs::FsNativeSave check_save_fs")
    pos_rec_meta = rsz_body.find("ReadArchiveSaveMetadata(rec_zfile, pbox, rec_meta, &rec_meta_rc)")
    pos_rec_gate = rsz_body.find("R_UNLESS(rec_meta_status == ArchiveMetaStatus::Valid")
    pos_clear_commit = rsz_body.find("save_fs.Commit()")

    check(pos_open != -1 and pos_preflight != -1 and pos_meta != -1 and pos_invalid_gate != -1 and
          pos_dest_res != -1 and pos_check_fs != -1 and pos_rec_meta != -1 and pos_rec_gate != -1 and pos_clear_commit != -1,
          "All RestoreSaveZip lifecycle gates must exist in source")

    check(pos_open < pos_preflight < pos_meta < pos_invalid_gate < pos_dest_res < pos_check_fs < pos_rec_meta < pos_rec_gate < pos_clear_commit,
          "Admission gates (metadata read, invalid refusal, recovery validation) must strictly precede clear/mutation commit")

    # Dead dbi_extra variable check
    check("dbi_extra" not in rsz_body,
          "RestoreSaveZip must have dead dbi_extra variable and branch removed")

    # 1.4 sphaira/CMakeLists.txt version check
    cmake_path = os.path.join(repo_root, "sphaira", "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        cmake_src = f.read()
    check(any(f"set(sphaira_VERSION 0.13.{v})" in cmake_src for v in range(856, 879)),
          "sphaira/CMakeLists.txt version must be 0.13.856 or later")

    print("  -> Static source contracts & gate order checks PASSED.")


# ==============================================================================
# 2. Synthetic Reference Decoders & Data Builders
# ==============================================================================

JKSV_MAGIC = 0x56534B4A
JKSV_REVISION = 1
SPHAIRA_MAGIC = 0x4A4B5356
SPHAIRA_VERSION = 1

VALID_SPACES = {0, 1, 2, 3, 4, 100, 101}

from contract_fixtures.save_metadata_wire_models import (
    RES_OK, ERR_CORRUPT_METADATA, ERR_IDENTITY_REFUSED, ERR_TARGET_LOCKED,
    ERR_CANNOT_RESTORE_TO_SYSTEM, ERR_INDEX_MISMATCH, ERR_APP_MISMATCH,
    ERR_NO_VALID_METADATA, ERR_DISA_NOT_SUPPORTED, META_FORMAT_JKSV85,
    META_FORMAT_JKSV86, META_FORMAT_SPHAIRA128, META_FORMAT_DBI512,
    pack_jksv85, pack_jksv_tail86, pack_jksv_middle86, pack_sphaira128,
    pack_dbi_raw512, ReferenceSaveMetadataDecoder, make_zip,
    JKSV_MAGIC, JKSV_REVISION, SPHAIRA_MAGIC, SPHAIRA_VERSION
)
from contract_fixtures.save_metadata_wire_scenarios import (
    test_connected_admission_and_failures, test_account_uid_remap_contract,
    ReferenceBackupDiscoveryModel, test_embedded_index0_vs_filename_discovery,
    test_legacy_recovery_and_leading_slash_dbi
)

def test_behavioral_fixtures() -> None:
    print("[6] Running synthetic behavioral regression fixtures...")

    # Fixture 1: Genuine JKSV 85-byte layout decoding
    raw_85 = pack_jksv85()
    m85 = ReferenceSaveMetadataDecoder.decode_jksv85(raw_85)
    check(m85 is not None, "JKSV 85-byte payload must decode successfully")
    check(m85["application_id"] == 0x0100000000010000, "App ID must match")
    check(m85["save_data_type"] == 1, "Save data type must match")
    check(m85["source_space"] is None, "Source space must be None for 85-byte layout")
    zip_85 = make_zip({".nx_save_meta.bin": raw_85, "save.dat": b"hello"})
    status, meta = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_85)
    check(status == "Valid" and meta is not None, "ZIP with JKSV 85 must return Valid status")

    # Fixture 2: JKSV tail-86 layout decoding with valid source_spaces
    for sp in (0, 1, 2, 3, 4, 100, 101):
        raw_tail86 = pack_jksv_tail86(owner_id=0x0100000000010055, source_space=sp)
        mt = ReferenceSaveMetadataDecoder.decode_jksv_tail86(raw_tail86)
        check(mt is not None and mt["source_space"] == sp, f"JKSV tail-86 with space {sp} must decode")
        zip_t86 = make_zip({".nx_save_meta.bin": raw_tail86})
        st, m = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_t86)
        check(st == "Valid" and m["source_space"] == sp, f"ZIP with tail-86 space {sp} must be Valid")

    # Fixture 3: JKSV middle-86 layout decoding where tail is invalid
    raw_mid86 = pack_jksv_middle86(commit_id=0x5500000000000001, source_space=1)
    mm = ReferenceSaveMetadataDecoder.decode_jksv_middle86(raw_mid86)
    mt = ReferenceSaveMetadataDecoder.decode_jksv_tail86(raw_mid86)
    check(mm is not None and mt is None, "Middle-86 must succeed while tail-86 fails on 0x55 byte 85")
    m_res = ReferenceSaveMetadataDecoder.decode_jksv86_ambiguity(raw_mid86)
    check(m_res is not None and m_res["source_space"] == 1, "Ambiguity check must resolve uniquely to middle-86")
    zip_m86 = make_zip({".nx_save_meta.bin": raw_mid86})
    st_m86, m_m86 = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_m86)
    check(st_m86 == "Valid" and m_m86["source_space"] == 1, "ZIP with middle-86 must be Valid")

    # Fixture 4: Unconditional divergent 86-byte fixture
    raw_divergent = pack_jksv_tail86(owner_id=0x0100000000010001, source_space=0)
    t_div = ReferenceSaveMetadataDecoder.decode_jksv_tail86(raw_divergent)
    m_div = ReferenceSaveMetadataDecoder.decode_jksv_middle86(raw_divergent)
    check(t_div is not None, "Divergent fixture tail-86 must decode successfully")
    check(m_div is not None, "Divergent fixture middle-86 must decode successfully")
    check(t_div["source_space"] == 0, "Divergent fixture tail space must be 0")
    check(m_div["source_space"] == 1, "Divergent fixture middle space must be 1")
    check(t_div["source_space"] != m_div["source_space"], "Tail and middle spaces must diverge")
    amb_div = ReferenceSaveMetadataDecoder.decode_jksv86_ambiguity(raw_divergent)
    check(amb_div is None, "Divergent 86-byte interpretations must fail closed unconditionally")
    zip_div = make_zip({".nx_save_meta.bin": raw_divergent})
    st_div, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_div)
    check(st_div == "Invalid", "Ambiguous 86-byte ZIP must fail closed as Invalid")

    # Fixture 5: Invalid source_space values rejected
    for invalid_sp in (5, 99, 102, 255):
        raw_bad_sp = pack_jksv_tail86(source_space=invalid_sp)
        check(ReferenceSaveMetadataDecoder.decode_jksv_tail86(raw_bad_sp) is None,
              f"Space {invalid_sp} must be rejected")

    # Fixture 6: Sphaira 128-byte layout with uninitialized padding ignored
    raw_128_dirty = pack_sphaira128(pad=b"\xDE\xAD\xBE\xEF" * 7)
    m128 = ReferenceSaveMetadataDecoder.decode_sphaira128(raw_128_dirty)
    check(m128 is not None, "Sphaira 128-byte layout with non-zero padding must decode")
    check(m128["application_id"] == 0x0100000000010000, "App ID must match")
    check(m128["source_space"] is None, "Sphaira 128 source space is None")
    zip_128 = make_zip({".nx_save_meta.bin": raw_128_dirty})
    st128, meta128 = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_128)
    check(st128 == "Valid" and meta128 is not None, "ZIP with dirty padding Sphaira 128 must be Valid")

    # Fixture 7: DBI raw 512-byte layout with uninitialized trailing padding ignored
    raw_512_dirty = pack_dbi_raw512(extra=b"\xCC" * 400)
    m512 = ReferenceSaveMetadataDecoder.decode_dbi_raw512(raw_512_dirty)
    check(m512 is not None, "DBI 512-byte layout with uninitialized extra must decode")
    zip_512 = make_zip({".dbi_save_extra": raw_512_dirty})
    st512, meta512 = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_512)
    check(st512 == "Valid" and meta512 is not None, "ZIP with dirty DBI extra must be Valid")

    # Fixture 8: Upfront size rejections and semantic invariants
    for bad_size in (0, 4, 84, 87, 127, 129, 200, 511, 513):
        zip_bad_size = make_zip({".nx_save_meta.bin": b"\x00" * bad_size})
        st_bad, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_bad_size)
        check(st_bad == "Invalid", f"Size {bad_size} must fail closed as Invalid upfront")
    for bad_extra_size in (85, 128, 511, 513):
        zip_bad_extra = make_zip({".dbi_save_extra": b"\x00" * bad_extra_size})
        st_bad_extra, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_bad_extra)
        check(st_bad_extra == "Invalid", f"DBI extra size {bad_extra_size} must fail closed as Invalid upfront")

    bad_magic = pack_jksv85(magic=0x12345678)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(bad_magic) is None, "Bad magic must be rejected")
    bad_rev = pack_jksv85(rev=2)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(bad_rev) is None, "Bad revision must be rejected")
    bad_dsize = pack_jksv85(data_size=-1)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(bad_dsize) is None, "Negative data size must be rejected")
    bad_jsize = pack_jksv85(journal_size=-1)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(bad_jsize) is None, "Negative journal size must be rejected")
    bad_dsize_min = pack_jksv85(data_size=-9223372036854775808)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(bad_dsize_min) is None, "Sign-bit data size must be rejected")
    ok_zero_size = pack_jksv85(data_size=0, journal_size=0)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(ok_zero_size) is not None, "Zero sizes must be accepted")
    ok_max_s64 = pack_jksv85(data_size=0x7FFFFFFFFFFFFFFF, journal_size=0x7FFFFFFFFFFFFFFF)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(ok_max_s64) is not None, "Max s64 sizes must be accepted")

    bad_uid = pack_jksv85(save_type=1, uid_low=0, uid_high=0)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(bad_uid) is None, "Account save with zero UID must be rejected")
    ok_uid_low = pack_jksv85(save_type=1, uid_low=0x1234, uid_high=0)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(ok_uid_low) is not None, "Low-half UID must be accepted")
    ok_uid_high = pack_jksv85(save_type=1, uid_low=0, uid_high=0x5678)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(ok_uid_high) is not None, "High-half UID must be accepted")

    ok_idx0 = pack_jksv85(index=0)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(ok_idx0) is not None, "Index 0 must be accepted")
    ok_idx_max = pack_jksv85(index=65535)
    check(ReferenceSaveMetadataDecoder.decode_jksv85(ok_idx_max) is not None, "Index 65535 must be accepted")

    # Fixture 9: Opaque DBI INI >64KiB accepted without arbitrary limit
    large_ini_content = b"[SaveInfo]\nTitleId=0100000000010000\n" + (b"Comment=SomeLargeOpaqueBlockOfData\n" * 2000)
    check(len(large_ini_content) > 65536, "Large INI content must be >64KiB")
    zip_large_ini = make_zip({
        ".dbi_save_info.ini": large_ini_content,
        "save.dat": b"test_payload"
    })
    st_lini, m_lini = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_large_ini)
    check(st_lini == "NoMetadata", "Large opaque INI without metadata must return NoMetadata without failing")

    # Fixture 10: Coexistence rules between .nx_save_meta.bin and .dbi_save_extra
    common_app = 0x0100000000010000
    common_owner = 0x0100000000010055
    nx_payload = pack_jksv_tail86(app_id=common_app, owner_id=common_owner, source_space=1)
    dbi_payload = pack_dbi_raw512(app_id=common_app, owner_id=common_owner)
    zip_coexist_match = make_zip({
        ".nx_save_meta.bin": nx_payload,
        ".dbi_save_extra": dbi_payload
    })
    st_match, meta_match = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_coexist_match)
    check(st_match == "Valid", "Coexisting matching NX and DBI metadata must be Valid")
    check(meta_match["source_space"] == 1, "Source space from NX must be retained")

    dbi_mismatch = pack_dbi_raw512(app_id=0x0100000000020000, owner_id=common_owner)
    zip_coexist_mismatch = make_zip({
        ".nx_save_meta.bin": nx_payload,
        ".dbi_save_extra": dbi_mismatch
    })
    st_mismatch, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_coexist_mismatch)
    check(st_mismatch == "Invalid", "Mismatched coexisting metadata must be rejected as Invalid")

    # Fixture 11: Nested .nx_save_meta.bin preserved as payload vs parent directory conflict
    zip_nested_payload = make_zip({
        "saves/nested/.nx_save_meta.bin": b"regular payload content",
        "saves/data.bin": b"123"
    })
    st_nested, m_nested = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_nested_payload)
    check(st_nested == "NoMetadata" and m_nested is None,
          "Nested .nx_save_meta.bin under payload folder must not trigger conflict and must remain payload")

    zip_parent_conflict_nx = make_zip({
        ".nx_save_meta.bin/subfile.bin": b"bad payload",
        "save.dat": b"123"
    })
    st_pcn, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(zip_parent_conflict_nx)
    check(st_pcn == "Invalid", "Parent directory conflict with .nx_save_meta.bin must fail closed as Invalid")

    # Fixture 12: Directory attributes without trailing slash rejected
    info_dir_attr = zipfile.ZipInfo(".nx_save_meta.bin")
    info_dir_attr.external_attr = 0x10  # DOS Directory attribute
    buf_dir_attr = io.BytesIO()
    with zipfile.ZipFile(buf_dir_attr, "w") as zf_da:
        zf_da.writestr(info_dir_attr, b"")
    st_da, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(buf_dir_attr.getvalue())
    check(st_da == "Invalid", "Entry with external_attr 0x10 must fail closed as Invalid")

    info_unix_dir = zipfile.ZipInfo(".nx_save_meta.bin")
    info_unix_dir.external_attr = 0x4000 << 16  # Unix Directory attribute
    buf_unix_dir = io.BytesIO()
    with zipfile.ZipFile(buf_unix_dir, "w") as zf_ud:
        zf_ud.writestr(info_unix_dir, b"")
    st_ud, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(buf_unix_dir.getvalue())
    check(st_ud == "Invalid", "Entry with Unix directory mode 0x4000 must fail closed as Invalid")

    # Fixture 13: Duplicate case/slash aliases rejected
    buf_dup_case = io.BytesIO()
    with zipfile.ZipFile(buf_dup_case, "w") as zf_dc:
        zf_dc.writestr(".nx_save_meta.bin", raw_85)
        zf_dc.writestr(".NX_SAVE_META.BIN", raw_85)
    st_dc, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(buf_dup_case.getvalue())
    check(st_dc == "Invalid", "Duplicate entries with differing case must fail closed as Invalid")

    buf_dup_slash = io.BytesIO()
    with zipfile.ZipFile(buf_dup_slash, "w") as zf_ds:
        zf_ds.writestr(".nx_save_meta.bin", raw_85)
        zf_ds.writestr("/.nx_save_meta.bin", raw_85)
    st_ds, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(buf_dup_slash.getvalue())
    check(st_ds == "Invalid", "Duplicate entries with leading slash must fail closed as Invalid")

    # Fixture 14: Real ZIP CRC corruption rejected
    buf_crc = io.BytesIO()
    with zipfile.ZipFile(buf_crc, "w", compression=zipfile.ZIP_STORED) as zf_crc:
        zf_crc.writestr(".nx_save_meta.bin", raw_85)
    raw_crc_zip = bytearray(buf_crc.getvalue())
    raw_crc_zip[48] ^= 0xFF
    st_crc, _ = ReferenceSaveMetadataDecoder.read_archive_metadata(bytes(raw_crc_zip))
    check(st_crc == "Invalid", "ZIP with CRC corruption must fail closed as Invalid")

    print("  -> Synthetic behavioral reference fixtures PASSED.")


# ==============================================================================
# 8. Main Runner & Honest Limitations Disclosure
# ==============================================================================

def main():
    print("=== Sphaira v0.13.856: Save Metadata Wire Compatibility Test Suite ===")
    test_source_contracts()
    test_connected_admission_and_failures()
    test_account_uid_remap_contract()
    test_embedded_index0_vs_filename_discovery()
    test_legacy_recovery_and_leading_slash_dbi()
    test_behavioral_fixtures()

    print("\n--- HONEST VERIFICATION LIMITATIONS ---")
    print("1. Static source analysis verifies text token order, substring presence, and absence of dead branches.")
    print("2. Behavioral fixtures simulate wire layouts, decoders, and lifecycle gates in pure Python.")
    print("3. These tests do NOT constitute runtime bare-metal execution on Horizon OS / Switch hardware.")
    print("4. Decoder refusal alone is not connected lifecycle admission; true connected admission")
    print("   requires passing metadata admission, preflight, recovery validation, and safe path gates.")
    print("================================================================================")
    print("=== ALL SAVE METADATA WIRE COMPATIBILITY REGRESSION CHECKS PASSED ===")

if __name__ == "__main__":
    main()
