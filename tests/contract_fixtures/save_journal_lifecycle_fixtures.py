# Behavioral fixtures for save journal lifecycle contract.
import os
import io
import zipfile
from contract_fixtures.save_journal_lifecycle_models import (
    RES_OK, ERR_TARGET_LOCKED, ERR_VERIFICATION_FAILED, ERR_CANCELLED,
    ERR_INVALID_SIZE, ERR_INVALID_CHAR, ERR_UNZ_READ, ERR_CRC_MISMATCH,
    ERR_PREFLIGHT_FAIL, ERR_JOURNAL_SIZE_MISMATCH, ERR_PREV_RESTORE_DIRTY,
    ERR_REWIND_FAILED, RESERVED_NAMES, INVALID_CHARS, MAX_S64, SMALL_BUFFER_SIZE,
    ModelNativeFile, ModelNativeFs, MockProgressBox, simulate_checked_restore,
    build_fixture_zip
)

def check(condition: bool, msg: str) -> None:
    if not condition:
        raise AssertionError(msg)

def test_behavioral_fixtures() -> None:
    print("[2] Running synthetic behavioral regression fixtures...")

    # Fixture 1: Negative journal rejected before extraction mutation
    zip_basic = build_fixture_zip({"save.bin": b"hello"})
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=-1)
    check(not ok and "reject_negative_journal" in ev and "clear_collections" not in ev,
          "Negative journal size must be rejected before clear/mutation")
    print("  -> Fixture 1 (Negative journal rejection before mutation) PASSED.")

    # Fixture 2: Zero journal keeps per-read commits; no divide by zero; request cap is SMALL_BUFFER_SIZE
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=0)
    check(ok and f"request_cap:{SMALL_BUFFER_SIZE}" in ev and "commit:chunk:/save.bin:0" in ev,
          "Zero journal must default request cap to SMALL_BUFFER_SIZE and perform per-read commits")
    print("  -> Fixture 2 (Zero journal default cap & per-read commits) PASSED.")

    # Fixture 3: Positive journal boundary caps (1, below SMALL_BUFFER_SIZE, equal, above, s64 max)
    # Journal = 1 byte
    zip_multi = build_fixture_zip({"data.bin": b"ABCDE"})
    ok, fs, ev, pbox = simulate_checked_restore(zip_multi, declared_journal_size=1)
    check(ok and "request_cap:1" in ev, "Journal=1 must cap requests to 1 byte")
    chunk_commits = [e for e in ev if e.startswith("commit:chunk:/data.bin:")]
    check(len(chunk_commits) == 5, f"Journal=1 must result in 5 commits for 5 bytes (got {len(chunk_commits)})")
    check(fs.files["/data.bin"].data == b"ABCDE", "File data must match exact offset writes")

    # Journal = 1024 bytes (below SMALL_BUFFER_SIZE)
    large_payload = b"X" * 3000
    zip_large = build_fixture_zip({"large.bin": large_payload})
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024)
    check(ok and "request_cap:1024" in ev, "Journal=1024 must cap request to 1024")
    commits_1024 = [e for e in ev if e.startswith("commit:chunk:/large.bin:")]
    check(len(commits_1024) == 3, f"3000 bytes with cap 1024 must produce 3 commits (1024, 1024, 952), got {len(commits_1024)}")
    check(fs.files["/large.bin"].data == large_payload, "Large payload bytes must match exactly")

    # Journal = SMALL_BUFFER_SIZE
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=SMALL_BUFFER_SIZE)
    check(ok and f"request_cap:{SMALL_BUFFER_SIZE}" in ev, "Journal equal to SMALL_BUFFER_SIZE must cap to SMALL_BUFFER_SIZE")

    # Journal = 10 MiB (above SMALL_BUFFER_SIZE) -> capped at SMALL_BUFFER_SIZE
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=10 * 1024 * 1024)
    check(ok and f"request_cap:{SMALL_BUFFER_SIZE}" in ev, "Journal > SMALL_BUFFER_SIZE must cap to SMALL_BUFFER_SIZE")

    # Journal = s64 max -> capped at SMALL_BUFFER_SIZE without overflow
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=MAX_S64)
    check(ok and f"request_cap:{SMALL_BUFFER_SIZE}" in ev, "Journal s64 max must cap to SMALL_BUFFER_SIZE without overflow")
    print("  -> Fixture 3 (Journal boundary caps: 1, below, equal, above, s64 max) PASSED.")

    # Fixture 4: Positive deliberate short reads
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, short_read_step=500)
    check(ok and fs.files["/large.bin"].data == large_payload,
          "Short reads must accumulate correctly without byte loss")
    print("  -> Fixture 4 (Short reads accumulation & integrity) PASSED.")

    # Fixture 5: Empty file lifecycle (open, flush, close, commit, no data reads)
    zip_empty = build_fixture_zip({"empty.bin": b""})
    ok, fs, ev, pbox = simulate_checked_restore(zip_empty, declared_journal_size=1000)
    check(ok and "commit:empty_file:/empty.bin" in ev and not any("chunk:" in e for e in ev),
          "Empty file must flush/close/commit without data payload writes")
    print("  -> Fixture 5 (Empty file lifecycle) PASSED.")

    # Fixture 6: Metadata component-by-component creation & collision handling
    zip_nested = build_fixture_zip({"sub/nested/file.bin": b"nested content"}, dirs=["empty_dir"])
    ok, fs, ev, pbox = simulate_checked_restore(zip_nested, declared_journal_size=1000)
    check(ok and "create_dir:/sub" in ev and "commit:dir:/sub" in ev and "create_dir:/sub/nested" in ev,
          "Implicit and explicit dirs must be created and committed component by component")
    print("  -> Fixture 6 (Component-by-component metadata cadence) PASSED.")

    # Fixture 7: Metadata commit failure MUST NOT be retried or swallowed
    ok, fs, ev, pbox = simulate_checked_restore(zip_nested, declared_journal_size=1000, fail_at="commit:dir:/sub")
    check(not ok and "commit:dir:/sub" in ev and "create_dir:/sub/nested" not in ev,
          "Metadata commit failure must propagate immediately and not proceed to next component")
    print("  -> Fixture 7 (Metadata commit failure refusal) PASSED.")

    # Fixture 8: File create commit failure before payload open
    ok, fs, ev, pbox = simulate_checked_restore(zip_basic, declared_journal_size=1000, fail_at="commit:file_create:/save.bin")
    check(not ok and "commit_file_create_error:/save.bin" in ev and "open_file:/save.bin:2" not in ev,
          "File creation commit failure must halt before opening write handle")
    print("  -> Fixture 8 (File creation commit failure before open) PASSED.")

    # Fixture 9: Chunk write / flush / commit failures and handle invalidation
    # Write failure
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="write_fail:0")
    check(not ok and "write_error:0" in ev and fs.open_write_handles == 0,
          "Write failure must close file handle and not leave active write handles")

    # Flush failure
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="flush_fail:0")
    check(not ok and "flush_error:0" in ev and fs.open_write_handles == 0,
          "Flush failure must close file handle and not leave active write handles")

    # Chunk commit failure: progress NOT advanced, handle closed, no auto-commit
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="commit:chunk:/large.bin:0")
    check(not ok and "commit_chunk_error:0" in ev and fs.open_write_handles == 0,
          "Chunk commit failure must propagate and leave zero open handles")
    print("  -> Fixture 9 (Write, flush, commit failures & zero open handles) PASSED.")

    # Fixture 10: Cancellation before write vs during/after commit
    # Cancel before write: closes without commit
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, cancel_at="cancel_before_write:0")
    check(not ok and "cancelled_before_write:0" in ev and "commit:chunk:/large.bin:0" not in ev,
          "Cancellation before write must close without commit")

    # Cancel after commit: observed immediately before reopen/next read
    ok, fs, ev, pbox = simulate_checked_restore(zip_large, declared_journal_size=1024, cancel_at="cancel_after_commit:1024")
    check(not ok and "cancelled_after_commit:1024" in ev and "commit:chunk:/large.bin:0" in ev,
          "Cancellation after commit must be observed before next reopen")
    print("  -> Fixture 10 (Cancellation before write vs after commit) PASSED.")

    # Fixture 11: Fake single-operation allocation/journal refusal despite valid request cap
    class ExhaustionModelFs(ModelNativeFs):
        def commit(self, context: str = ""):
            if "chunk" in context:
                raise RuntimeError("OS_ERROR_JOURNAL_EXHAUSTED: single allocation exceeded platform journal quota")
            super().commit(context)

    exhaustion_fs = ExhaustionModelFs()
    try:
        exhaustion_fs.create_file("/save.bin", 5)
        exhaustion_fs.commit("file_create:/save.bin")
        f = exhaustion_fs.open_file("/save.bin", 2)
        f.write(0, b"hello")
        f.flush()
        exhaustion_fs.close_file(f)
        exhaustion_fs.commit("chunk:/save.bin:0")
        check(False, "Exhaustion model must raise journal exhaustion")
    except RuntimeError as e:
        check("OS_ERROR_JOURNAL_EXHAUSTED" in str(e), "Exhaustion correctly raised on single operation")
    print("  -> Fixture 11 (Simulated single-operation journal exhaustion ceiling illustration) PASSED.")

    # Fixture 12: Premature zero read, negative read, and over-request read independently verified
    # 12.1 Negative read (< 0)
    ok_neg, fs_neg, ev_neg, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="read_fail_neg:0")
    check(not ok_neg and "read_error_neg:0" in ev_neg and fs_neg.open_write_handles == 0,
          "Negative read must abort extraction and close write handles")

    # 12.2 Premature zero read (== 0)
    ok_zero, fs_zero, ev_zero, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="read_fail_zero:0")
    check(not ok_zero and "read_error_zero:0" in ev_zero and fs_zero.open_write_handles == 0,
          "Premature zero read must abort extraction and close write handles")

    # 12.3 Over-request read (> to_read)
    ok_over, fs_over, ev_over, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="read_fail_over:0")
    check(not ok_over and "read_error_over:0" in ev_over and fs_over.open_write_handles == 0,
          "Over-request read must abort extraction and close write handles")
    print("  -> Fixture 12 (Premature zero, negative, over-request read rejection) PASSED.")

    # Fixture 13: Remaining and offset arithmetic at s64 boundary without huge loops/buffers
    req_cap = SMALL_BUFFER_SIZE
    rem = MAX_S64
    off = 0
    to_read = min(req_cap, rem)
    check(off + to_read <= MAX_S64, "Initial chunk must not overflow s64")
    check(rem - to_read >= 0, "Initial remaining must not underflow")

    off = MAX_S64 - req_cap
    rem = req_cap
    to_read = min(req_cap, rem)
    check(off + to_read == MAX_S64, "Final chunk must reach MAX_S64 exactly without overflow")
    check(rem - to_read == 0, "Final remaining must reach 0 exactly")
    print("  -> Fixture 13 (s64 boundary arithmetic validation) PASSED.")

    # Fixture 14: Unexpected existing destination file rejection tested through actual restore engine
    fs_existing = ModelNativeFs()
    fs_existing.create_file("/save.bin", 100)
    fs_existing.after_clear = lambda: fs_existing.create_file("/save.bin", 100)
    ok_ex, fs_ex_out, ev_ex, _ = simulate_checked_restore(zip_basic, declared_journal_size=1024, fs=fs_existing)
    check(not ok_ex, "Creating over existing destination file must fail restore")
    check("create_file_error:/save.bin" in ev_ex, "Must record create_file_error event")
    check(fs_ex_out.open_write_handles == 0, "Zero open handles on existing file collision")
    check(not any("chunk:" in e for e in ev_ex), "Zero chunk commits on existing file collision")
    print("  -> Fixture 14 (Unexpected existing destination file rejection) PASSED.")

    # Fixture 15: Existing non-directory parent collision during directory creation through actual engine
    fs_coll = ModelNativeFs()
    fs_coll.create_file("/sub", 50)  # Non-dir entry where directory is expected
    fs_coll.after_clear = lambda: fs_coll.create_file("/sub", 50)
    ok_coll, fs_coll_out, ev_coll, _ = simulate_checked_restore(zip_nested, declared_journal_size=1000, fs=fs_coll)
    check(not ok_coll, "Existing non-directory parent collision must fail restore")
    check("parent_conflict_not_dir:/sub" in ev_coll or "parent_conflict_exists_file:/sub" in ev_coll,
          "Must record parent non-directory conflict event")
    check(fs_coll_out.open_write_handles == 0, "Zero open handles on non-directory parent collision")
    check(not any("chunk:" in e for e in ev_coll), "Zero payload chunk commits on parent collision")
    print("  -> Fixture 15 (Existing non-directory parent collision check) PASSED.")

    # Fixture 16: Reopen failure and final commit failure handling
    ok_ro, fs_ro, ev_ro, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, fail_at="reopen_fail:1024")
    check(not ok_ro and fs_ro.open_write_handles == 0 and "reopen_error:1024" in ev_ro,
          "Reopen failure must propagate and leave zero open handles")

    ok_fc, fs_fc, ev_fc, _ = simulate_checked_restore(zip_basic, declared_journal_size=1024, fail_at="commit:final_extraction")
    check(not ok_fc and "final_commit_error" in ev_fc and "extraction_succeeded" not in ev_fc,
          "Final commit failure must prevent extraction success")
    print("  -> Fixture 16 (Reopen and final commit failure handling) PASSED.")

    # Fixture 17: Cancellation points: metadata, before create file, after create commit, loop start, between chunks, and after final payload
    # 17.1 Cancel at metadata directory creation
    ok_c1, _, ev_c1, _ = simulate_checked_restore(zip_nested, declared_journal_size=1000, cancel_at="dir_cancel:/empty_dir")
    check(not ok_c1 and "cancelled_at_dir:/empty_dir" in ev_c1,
          "Cancellation during metadata component creation must halt immediately")

    # 17.2 Cancel before file creation
    ok_c2, _, ev_c2, _ = simulate_checked_restore(zip_basic, declared_journal_size=1000, cancel_at="cancel_before_create_file:/save.bin")
    check(not ok_c2 and "cancelled_before_create_file:/save.bin" in ev_c2 and not any(e.startswith("create_file:") for e in ev_c2),
          "Cancellation before file creation must halt before fsFsCreateFile")

    # 17.3 Cancel after file create commit (before payload open)
    ok_c3, _, ev_c3, _ = simulate_checked_restore(zip_basic, declared_journal_size=1000, cancel_at="cancel_after_commit_create:/save.bin")
    check(not ok_c3 and "cancelled_after_commit_create:/save.bin" in ev_c3 and not any(e.startswith("open_file:") for e in ev_c3),
          "Cancellation after file create commit must halt before opening payload handle")

    # 17.4 Cancel at start of payload loop
    ok_c4, fs_c4, ev_c4, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, cancel_at="cancel_start_loop:0")
    check(not ok_c4 and "cancelled_start_loop:0" in ev_c4 and fs_c4.open_write_handles == 0,
          "Cancellation at loop start must close open payload handle and halt")

    # 17.5 Cancel after final payload chunk
    ok_c5, _, ev_c5, _ = simulate_checked_restore(zip_large, declared_journal_size=1024, cancel_at="cancel_after_commit:3000")
    check(not ok_c5 and "cancelled_after_commit:3000" in ev_c5 and "extraction_succeeded" not in ev_c5,
          "Cancellation after final payload chunk must return cancellation before overall success")
    print("  -> Fixture 17 (Comprehensive cancellation checkpoints) PASSED.")

    # Fixture 18: Progress invariant: commit refusal maintains accurate progress
    # 18.1 First chunk commit failure maintains progress == 0
    pbox_prog1 = MockProgressBox()
    ok_p1, fs_p1, ev_p1, pbox_out1 = simulate_checked_restore(
        zip_large, declared_journal_size=1024,
        fail_at="commit:chunk:/large.bin:0",
        pbox=pbox_prog1
    )
    check(not ok_p1, "First chunk commit failure must fail restore")
    check(pbox_out1.updated_progress == 0, f"Progress must remain 0 on first chunk commit failure (got {pbox_out1.updated_progress})")
    check(pbox_out1.progress_history == [], "Progress history must be empty on first chunk commit failure")

    # 18.2 Second chunk commit failure maintains progress == 1024 (only earlier committed chunk)
    pbox_prog2 = MockProgressBox()
    ok_p2, fs_p2, ev_p2, pbox_out2 = simulate_checked_restore(
        zip_large, declared_journal_size=1024,
        fail_at="commit:chunk:/large.bin:1024",
        pbox=pbox_prog2
    )
    check(not ok_p2, "Second chunk commit failure must fail restore")
    check(pbox_out2.updated_progress == 1024, f"Progress must strictly reflect only earlier committed chunk (got {pbox_out2.updated_progress})")
    check(pbox_out2.progress_history == [1024], f"Progress history must only contain first chunk (got {pbox_out2.progress_history})")
    print("  -> Fixture 18 (Progress invariant: commit refusal maintains accurate progress) PASSED.")

    # Fixture 19: Real ZIP entry CRC corruption detection refusing final commit
    # 19.1 Corrupted payload byte detected during ZIP streaming read
    buf_crc = io.BytesIO()
    with zipfile.ZipFile(buf_crc, "w", compression=zipfile.ZIP_STORED) as zf_crc:
        zf_crc.writestr("corrupt.bin", b"valid_payload")
    raw_crc = bytearray(buf_crc.getvalue())
    idx = raw_crc.find(b"valid_payload")
    raw_crc[idx] ^= 0xFF
    bad_crc_zip = bytes(raw_crc)

    ok_crc, fs_crc, ev_crc, _ = simulate_checked_restore(bad_crc_zip, declared_journal_size=1024)
    check(not ok_crc and "crc_read_error:BadZipFile" in ev_crc and "extraction_succeeded" not in ev_crc,
          "CRC read error must halt extraction and prevent final extraction commit")
    check(fs_crc.open_write_handles == 0, "Zero open write handles on CRC read error")

    # 19.2 Explicit post-loop CRC mismatch detection
    ok_crc2, fs_crc2, ev_crc2, _ = simulate_checked_restore(zip_basic, declared_journal_size=1024, fail_at="crc_mismatch")
    check(not ok_crc2 and any(e.startswith("crc_mismatch:") for e in ev_crc2) and "extraction_succeeded" not in ev_crc2,
          "Post-loop CRC mismatch must halt extraction before final commit")
    check(fs_crc2.open_write_handles == 0, "Zero open write handles on post-loop CRC mismatch")
    print("  -> Fixture 19 (CRC error detection refusing final commit) PASSED.")

    # Recovery belongs to the exercised restore, and remains published on success too.
    for failure in ("commit:chunk:/large.bin:0", "source_entry_close", "ro_mount", "verify", "source_reader_close"):
        ok, fs, ev, _ = simulate_checked_restore(zip_large, 1024, fail_at=failure)
        check(not ok and "restore_succeeded" not in ev and fs.recovery_path,
              f"Restore gate {failure} must refuse success and retain recovery")
        check(fs.open_write_handles == 0, "Failed restore leaves no write handles")
    ok, fs, ev, _ = simulate_checked_restore(zip_large, 1024)
    check(ok and fs.recovery_path, "Successful restore retains published recovery")
    check(ev.index("rw_exit") < ev.index("ro_mount") < ev.index("verify") < ev.index("ro_exit")
          < ev.index("source_reader_close") < ev.index("restore_succeeded"), "Final verification lifecycle order")
    for failure in ("create_dir:/sub", "create_file:/save.bin", "commit:post_clear"):
        payload = zip_nested if "sub" in failure else zip_basic
        ok, fs, ev, _ = simulate_checked_restore(payload, 1024, fail_at=failure)
        check(not ok and fs.recovery_path and "restore_succeeded" not in ev, f"Primitive fault {failure}")
    # Cancellation is delivered inside the operation, then observed at its next gate.
    for operation in ("create_commit", "chunk_commit", "reopen"):
        fs, pb = ModelNativeFs(), MockProgressBox()
        if operation == "create_commit":
            fs.on_commit = lambda context: setattr(pb, "cancelled", True) if context.startswith("file_create:") else None
        elif operation == "chunk_commit":
            fs.on_commit = lambda context: setattr(pb, "cancelled", True) if context.startswith("chunk:") else None
        else:
            opens = []
            def cancel_reopen(path):
                opens.append(path)
                if len(opens) == 2:
                    pb.cancelled = True
            fs.on_open = cancel_reopen
        ok, fs, ev, pb = simulate_checked_restore(zip_large, 1024, fs=fs, pbox=pb)
        check(not ok and fs.open_write_handles == 0 and fs.recovery_path, f"Cancel during {operation}")
        reads = [e for e in ev if e.startswith("read:")]
        check(len(reads) == (0 if operation == "create_commit" else 1), "No extra ZIP read after cancellation")
    ok, fs, ev, _ = simulate_checked_restore(zip_empty, 1024, cancel_at="cancel_after_empty_commit:/empty.bin")
    check(not ok and fs.recovery_path and fs.open_write_handles == 0, "Empty-file commit cancellation")
    # Clear really removes old data; conflicts are injected by after_clear above.
    fs = ModelNativeFs()
    fs.create_file("/obsolete.bin", 1)
    ok, fs, ev, _ = simulate_checked_restore(zip_basic, 1024, fs=fs)
    check(ok and "/obsolete.bin" not in fs.files, "Old files removed by clear")
    # Primitive event sequence and no cleanup commit after failed chunk commit.
    ok, fs, ev, pb = simulate_checked_restore(zip_large, 1024, fail_at="commit:chunk:/large.bin:1024")
    first = ["read:/large.bin:0:1024", "write:/large.bin:0:1024", "flush:/large.bin",
             "close_file:/large.bin", "commit:chunk:/large.bin:0", "progress:1024"]
    cursor = -1
    for event in first:
        cursor = ev.index(event, cursor + 1)
    failed = ev.index("commit:chunk:/large.bin:1024")
    check(not any(e.startswith(("commit:", "open_file:", "progress:")) for e in ev[failed + 1:]),
          "Failure has no retry, reopen, progress or auto-commit")
    print("  -> Fixture 20 (Connected lifecycle/fault/cancel/recovery checks) PASSED.")

    print("=== ALL SYNTHETIC BEHAVIORAL FIXTURES PASSED SUCCESSFULLY ===")


# ==============================================================================
# Main Runner
# ==============================================================================
