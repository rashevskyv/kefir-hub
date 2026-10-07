# plan.md — work queue

## Поточний delivery: v0.13.971
Попередній: v0.13.958

Source: `docs/dev/AUDIT-2026-10-01.md` (findings F1–F10). Baseline v0.13.922, commit `0c80cdd4`.
Rules: `AGENTS.md`. One task ≈ one session ≈ one commit. Do tasks in order inside a phase; phases in order.
Each task has **Do**, **Done when** (verifiable), **Verify** (command). Tick `[x]` when done; write nothing else here.
`[USER]` tasks are for the human (hardware). Build checkpoint (`test-build` skill) closes every phase.

---

## Phase 0 — Context diet and repo hygiene (no product code, no version bump)

- [x] 0.0 Audit report, new `AGENTS.md`, `CLAUDE.md`, this plan. Old `plan/task/walkthrough/audit.md` moved
      to `docs/dev/history/` (staged as renames). *(done by the audit session; commit it with 0.1)*
- [x] 0.1 **CHANGELOG from history.** *(accepted by senior: 638 versions → one line each; only `head -20` is read per session, so size is fine)* Create `docs/dev/CHANGELOG.md`. For every version in
      `git log --oneline | grep -oE 'v0\.13\.[0-9]+' | sort -uV` (and v0.13.871–922 described in
      `docs/dev/history/walkthrough.md`) write one entry: `## v0.13.X — <title>` + ≤2 lines (what shipped;
      verification state: host tests / nro / switch). Newest first. Then `git rm -r docs/dev/history`.
      **Done when:** every version number from git log appears exactly once; file ≤ 150 lines; history/ deleted.
      **Verify:** `for v in $(git log --oneline | grep -oE 'v0\.13\.[0-9]+' | sort -u); do grep -q "## $v " docs/dev/CHANGELOG.md || echo MISSING $v; done`
      <!-- blocked: git log has 638 distinct versions, so one heading each cannot fit 150 lines; CHANGELOG has all 638 (683 lines), history/ deleted -->
- [x] 0.2 **Untrack junk.** *(done by audit session: untracked, .gitignore updated, sphaira/graphify-out deleted)* `git rm --cached -r remote_log.txt .codex-tmp .grok .agents tools/i18n-translate/*.log`
      (only those that `git ls-files` lists). Append to `.gitignore`: `remote_log.txt`, `.codex-tmp/`, `.grok/`,
      `.agents/`, `tools/i18n-translate/*.log`, `.pytest_cache/`, `sphaira/graphify-out/`, `build/`.
      Delete `sphaira/graphify-out/` from disk (35 MB duplicate; canonical graph is repo-root `graphify-out/`).
      **Done when:** `git ls-files | grep -E 'remote_log|\.codex-tmp|\.grok|\.agents|run[0-9]*\.log'` is empty.
- [x] 0.3 **One test-build skill.** *(done; canonical moved to `.agents/skills/` for Codex/Gemini, `.claude/skills/` is a stub)* `git mv .agents/skills/test-build/SKILL.md .claude/skills/test-build/SKILL.md`
      (the `.grok` copy is identical — drop it). Rewrite its step 5 to the AGENTS.md delivery ritual
      (CHANGELOG + plan.md checkbox; no plan/task/walkthrough/audit quartet). Keep steps 1–4 as is.
      **Done when:** exactly one `SKILL.md` named test-build exists under `.claude/skills/`; it mentions `CHANGELOG.md`.
- [x] 0.4 **Graphify session hook.** *(done by audit session; verify once in Git Bash on Windows)* Add `.claude/hooks/graphify-session.sh`:
      if `graphify-out/GRAPH_REPORT.md` missing → `graphify .`; else if any file under `sphaira/ hbl/ sysmodule/ tests/`
      is newer than it → `graphify update .`; else print `Graphify: current`. Always `exit 0`.
      Register in `.claude/settings.json` as `SessionStart` hook (matcher `startup`, timeout 600).
      **Verify:** run the script by hand from repo root in Git Bash; second run prints `Graphify: current`.
- [x] 0.5 **README diet.** `README.md` (435 lines, 72 KB) → ≤ 150 lines: what it is, screenshots, install, feature
      list as one-liners linking to `docs/wiki/*.md` pages. Move any paragraph not already in the wiki into the matching
      wiki page (do not drop content; do not duplicate). **Done when:** `wc -l README.md` ≤ 150 and every removed
      heading's content is findable in `docs/wiki/` (`grep -ril "<key phrase>" docs/wiki`).
- [x] 0.6 Commit `chore: context diet — agent rules, changelog, untrack junk, README split`. No version bump.

## Phase 1 — Build truth and warning baseline

- [x] 1.1 **Build v0.13.922.** Run the `test-build` skill (WSL, preset `ReleaseWithInstall`). 922 was never built.
      Fix compile errors surgically if any. **Done when:** `[100%] Built target sphaira_nro`.
      *(verified clean build on v0.13.928: [100%] Built target sphaira_nro, host tests passed)*
- [x] 1.2 **Warning inventory.** Rebuild from clean (`rm -rf build/ReleaseWithInstall`), capture
      `cmake --build ... 2>&1 | grep -E 'warning:' | grep -E 'sphaira/|hbl/|sysmodule/' | sort -u > /tmp/warn.txt`.
      Record the count in the CHANGELOG entry. Fix every first-party warning (not `libs/`). Then enable `-Werror`
      for first-party sources only: in `sphaira/CMakeLists.txt` uncomment `-Werror` **inside the first-party
      compile-options block** (line ~376); if it also hits `libs/` targets, scope it with
      `set_source_files_properties(<first-party sources> PROPERTIES COMPILE_OPTIONS -Werror)` instead.
      Keep existing `-Wno-*` lines. **Done when:** clean build passes with `-Werror`; `/tmp/warn.txt` empty for first-party.
- [x] 1.3 **Host suite green in WSL.** `tests/run.sh` must pass end to end (all `tests/test_*.cpp`, dead-symbol
      guard, patch shape checks, Python contracts as they still exist). Fix only what is broken; do not delete tests here.
      **Verify:** `wsl bash -lc 'cd /mnt/d/git/dev/sphaira && tests/run.sh'` exits 0.
- [x] 1.4 `[USER]` **Hardware baseline.** *(done on v0.13.934, 2026-10-01 — results in the checklist; failures became Phase H)* Flash the 1.1 NRO and run `docs/dev/HARDWARE-CHECKLIST.md` sections A–C.
      Record pass/fail per item in the checklist file. This is the baseline for Phase 3.2 — do not start 3.2 before it.
- [x] 1.5 Build checkpoint + commit(s) `v0.13.9XX: warning-free first-party build with -Werror`.

## Phase H — bugs found on hardware (baseline v0.13.934, see `docs/dev/HARDWARE-CHECKLIST.md`)

Priority order. Each H task: investigate with `graphify explain` + the console log, fix surgically, bump + CHANGELOG,
and name the checklist item the user must re-run. Console log: `/config/kefir/log.txt` (Settings → Logging on);
the user drops logs into `scratch/` (git-ignored) as `scratch/<item>.log`. Do not guess when a log is missing — ask.
Evidence already on disk (git-ignored `scratch/`): `scratch/log-saves-session-v934.txt` (console log of the B-section run, 19:15–19:20), `scratch/errors-v934.txt` (append-only error log, all launches), `scratch/kefir-config.ini`, `scratch/dbi.config`. The MTP cancel runs (A3/A4) happened in earlier launches whose `log.txt` was overwritten — those logs are still pending from the user.

- [x] H1 **MTP drops after a Switch-side cancel (A3, A6, A7 note) — critical.** After B → «+» cancel, the console
      disconnects from MTP; the user must remount or re-plug. Expected: stay connected; if the transport really died,
      re-init MTP automatically in device mode (never host mode). This is what v0.13.911–922 tried to fix blind.
      Investigate: who calls `haze::Exit()` / USB reinit after `::haze::CancelTransfer()` (`graphify explain "Exit"`
      scoped to `sphaira/source/haze_helper.cpp`, `app_usb.cpp` `IsRecovering`/`pc_enumerated` path, `app_mtp_settings.cpp`),
      and what `patch_libhaze_cancel.cmake` makes libhaze do on cancel (does it close the USB interface?).
      **Evidence (`scratch/A3.log`, v0.13.934, 19:28:27):** B→«+» cancel → `[LIBHAZE] failed to cancel endpoint 0 for urb 3345: 0x828C` → `local cancel detected` → `transfer aborted by transport error: 0x7A4` → `Transfer Finished` → `CloseFile` → `DeleteFile` (good) → **`[USB] MTP session gone (state=0 recovering=0 unplugged=0); releasing port to host`** → `[MTP] exitied` → USBHSFS host polling.
      Root cause chain: `sphaira/cmake/patch_libhaze_cleanup.cmake:552` makes the local-cancel path call `m_usb_server.SetCancelled(true); m_usb_server.SetBroken(true);` — `SetBroken` tears down the whole USB/PTP session, usb:ds leaves Configured, and `sphaira/source/app_usb.cpp:57-61` (`haze::IsRunning() && (!is_recovering || genuinely_unplugged) && (!usbds_up || Detached)`) treats it as a PC unplug because `IsRecovering()` is false on the local-cancel path. Fix, in this order:
      (1) preferred — on local cancel do NOT `SetBroken(true)`: abort only the object transfer, respond to the host with a PTP cancel/incomplete-transfer response, keep the session open (verify what the responder does after `SetCancelled` without `SetBroken`; the host sends `GetObjectHandles` next and the session must still answer);
      (2) safety net — if the transport must be reset, set the haze recovering flag *before* `SetBroken` so `app_usb.cpp` takes the recovery branch (L44) and re-inits MTP in device mode (v0.13.922 path), never host mode; add a log line on that branch.
      Also `failed to cancel endpoint 0 … 0x828C` happens on every cancel and at every shutdown (`errors-v934.txt`) — check whether `usbDsEndpoint_Cancel` is called on an endpoint with no in-flight URB (harmless) or on the wrong endpoint. **Re-run:** A3, A6, A7, A5, A11.
      <!-- v0.13.935: fix (2) shipped (re-enumeration in device mode); fix (1) not viable without STALL + Get_Device_Status handling in libhaze — see CHANGELOG -->

- [x] H2 **PC-side cancel leaves the ProgressBox decaying to 0 B/s (A4).** Windows closes its dialog; the console box
      stays until speed hits zero. The box loop (`haze_helper.cpp` StartMtpProgressBox) only exits on `is_aborted`/seq
      change; a PC cancel (URB abort 0x748C) evidently never reaches `haze_callback` as an abort. Fix: surface the abort
      from libhaze (callback event) and close the box at once; as a fallback, if `is_active` and no bytes for >1.5 s and
      the USB transaction is gone, treat as aborted. Need `scratch/A4.log`. **Re-run:** A4, A1, A8.
- [x] H3 **Dropping a folder onto microSD via MTP does nothing (A2) — feature the user needs for translation packs.**
      Four files dropped at once work; a folder does not. MTP sends `SendObjectInfo` with format Association (0x3001)
      for the directory, then children. Check the device-root/SD route (`haze_fs_proxy.cpp`, `haze_game_proxy.cpp`,
      v0.13.913 routing) for missing directory creation / parent-handle mapping. **Re-run:** A2 (nested folder, 3+ files).
      <!-- A2 reported PASS by the user after v0.13.960; no separate fix: the folder drop works since the v0.13.913 routing was removed in H8 -->

- [x] H4 **New ZIP backup not listed in «Бекапи» (B2).** Backup written to `/dumps` (user's dump folder, v0.13.905)
      and visible in the file browser, but the Backups tab does not show it. Check the library scanner roots
      (`source/ui/menus/save/save_locations.cpp`, `save_backup_library*`): does it scan the configured dump folder or a
      hard-coded path? Is there a cache that is not invalidated after a backup? Facts: the backup in question is `/dumps/12Switch/20261001/01000320000CC000_D_20261001191222_0.zip` (892 bytes, type `_D_` = Device save; the other backups are `_A_`), `kefir-config.ini` has `[saves] show_backups=0` and `default_backup_location=sd||/dumps`. Check whether Device-type backups are filtered out of the Backups tab and what `show_backups` gates. **Re-run:** B1 → B2.
- [x] H5 **Restore to an uninstalled game fails (B4).** Dialog shows target as «Corrupted (Account: nin10do)
      [idx:0 rk:0 sp:1 …]» — the title-name fallback for a not-installed title should use the archive metadata name,
      not «Corrupted». After «Так»: «Вибраний архів резервної копії змінився або більше не доступний» — the staged-ZIP
      revalidation (v0.13.882 blocker 4) rejects a valid archive; likely compares path/size/mtime after staging or after
      slot creation changed the catalog entry. Find the exact failing comparison and log it. **Re-run:** B4.
- [x] H6 **Restore picker: duplicate users, wrong game name, «дублікат цільового слота» (B5).** «Бекапи» → Real Boxing 2
      → the dialog is titled «Відновити для користувача (Minecraft)» (stale title from another entry), lists «nin10do»
      twice (same uid, not deduplicated — probably accounts + save owners merged), and selecting one yields the
      duplicate-slot error. Fix: dedupe targets by uid, take the title from the selected group. **Re-run:** B5.
- [x] H7 **Grouping by origin not visible (B3) + DBI save path from DBI config.** v0.13.882 claims sections by source
      (Kefir Hub/DBI/JKSV/Checkpoint); the user sees none. Check the condition that shows section labels (maybe only
      when ≥2 sources are found) and the DBI/JKSV/Checkpoint roots being scanned. Then read DBI's own config for its
      saves directory instead of a hard-coded path — DBI config is `/switch/DBI/dbi.config` (INI-like, `;` comments), key `SavesFolder=sdmc:/switch/DBI/saves/` (copy in `scratch/dbi.config`); fall back to `/switch/DBI/saves` when the file or key is missing. The user's DBI saves dir holds both title-ID-named and game-name-named subfolders.
      **Re-run:** B3.
- [x] H8 `[USER decision]` **MTP routing NSP → Install / other → SD (A9) does not work.** The user said this was
      deliberately dropped. Decide: fix it, or delete the routing code (less code, fewer states). Until decided — no work.
- [x] H10 **Log spam + suspicious result in save scan.** `scratch/log-saves-session-v934.txt` shows `[SAVE] fsOpenSaveDataInfoReader failed for space 100: 0x…ee202` ~70 times in 5 minutes (twice per UI action), and the result value **increments by 0x00400000 on every call** (0xc22ee202, 0xc26ee202, …) — a Result should not change like that; check whether the logged value is actually a Result or a leaked handle/session id, and whether something is opened and never closed per call. Space 100 = `FsSaveDataSpaceId_ProperSystem`; it needs permissions the app does not have — skip it (or probe once and remember). Also `[SAVE] account uid is not found: 0x0` appears right before the B4/B5 flows — a uid-0 (Device) entry is being resolved as an account; likely the same root as H5/H6.
- [ ] H9 Build checkpoint after H1–H2, then again after H4–H6; `[USER]` re-runs the listed items; then `[USER]` section C.

## Phase 2 — Tests that test (see audit F2)

Keep as is: `tests/*.cpp`, `tests/check_dead_symbols.py`, `tests/test_patch_libhaze.sh`, `tests/test_patch_ftpsrv.sh`.
Working rule: a test is kept only if it executes project C++ (`#include` of a project header, compiled with g++) or
validates real data files (i18n JSON, cmake patch files). Everything that asserts on C++ *source text* is deleted.

- [x] 2.1 **Delete static-review contracts.** For each `tests/test_*.py`: if it contains `read_text` / `in c_text` /
      `in h_text` / `test_static_review_*` style assertions on `.cpp/.hpp` text, delete those functions. If nothing
      remains but `main()`, delete the file and its `contract_fixtures/` modules that no other test imports.
      Known all-static: `test_live_queue_contract.py`, `test_mtp_routing_and_minibadge_contract.py`,
      `test_sphq_empty_queue_contract.py`, `test_save_backup_destination_contract.py`,
      `test_modal_priority_and_mtp_cancel_contract.py`, `test_dbi_usb_connection_contract.py`, the
      `check_*_contract()` functions of `test_mtp_cancellation_contract.py`.
      Exception: `test_i18n_deployment_contract.py` — keep the parts that read `assets/romfs/i18n/*.json`
      (real data); delete its source-text asserts. Update `tests/run.sh` so it only runs what exists.
      **Done when:** `grep -lE 'read_text\(|in [chs]_text' tests/*.py` returns only `check_dead_symbols.py` (if at all).
- [x] 2.2 **Map Python behavioral models to real C++.** For each remaining `tests/test_*.py` (and its
      `contract_fixtures/*_models.py`) fill the table below (edit this task in place): which C++ function it mirrors
      (`graphify explain "<name>"`), and the verdict: `PURE` (function already in a libnx-free header → write
      `tests/test_<name>.cpp` against it), `SEAM` (logic inside libnx-dependent code → extract the decision logic into
      `sphaira/include/<module>/<name>_logic.hpp` or a libnx-free `.cpp`, call it from the original site, then test),
      `DROP` (mirrors nothing real, or scenario is hardware-only).
      | py test | C++ mirror | verdict |
      |---|---|---|
      | test_mtp_cancellation_models.py + `simulate_*` in test_mtp_cancellation_contract.py | haze_helper.cpp StartMtpProgressBox / haze_callback; libhaze cancel patches | SEAM → Phase 3.2 |
      | test_mtp_cancellation_contract.py `test_patch_application_scenarios` | sphaira/cmake/patch_libhaze*.cmake applied to upstream libhaze (needs cmake + build/_deps) | KEEP (real patch files) |
      | test_dbi_restore_admission_contract.py marker/malformed matrix | path_util.hpp `IsDbiRootMarkerEntry`, `NormalizeSaveArchiveEntry` | PURE (part) |
      | test_dbi_restore_admission_contract.py preflight/unzip/sources | threaded_file_transfer_preflight.cpp `TransferUnzipPreflight`, `ResolveArchiveEntryName`; save_paths `BackupGroupKey` | SEAM |
      | test_safe_restore_target_contract.py | save_menu_target.cpp `ResolveRestoreTarget`; save_paths.hpp `MatchesRestoreDestination` (fs.hpp → libnx) | SEAM |
      | test_save_backup_library_contract.py (+ save_backup_library_* fixtures) | save_backup_inspection.cpp `BackupGroupKey`; save_game_group.cpp `CollectGroupArchives`; `CreateBackupIfNewer` | SEAM |
      | test_save_backup_identity_contract.py | save_backup_pub.cpp `BackupSaveInternal` identity guard, `WriteSaveBackupZip` | SEAM |
      | test_raw_save_restore_contract.py (+ raw_save_restore_* fixtures) | save_backup_inspection.cpp DISF header check; filebrowser_ops.cpp `RestoreSaveFile` refusal | SEAM |
      | test_recursive_install_contract.py | filebrowser_recursive_install.cpp | deleted in 2.1 (static only) |
      | test_ownfoil_contract.py (now `TestI18nCompleteness` only) | assets/romfs/i18n/*.json Ownfoil keys | KEEP (real data) |
      | test_i18n_deployment_contract.py | assets/romfs/i18n/*.json | KEEP (real data) |
      | test_backup_source_section_contract.py | save_backup_inspection.cpp source classification (uses `HasPathDirComponentIC`); save_menu_draw.cpp grid sections; save_folder_discovery.cpp `InspectBackupFolder` | SEAM |
      | test_exact_save_discovery_contract.py | save_discovery.cpp `DiscoverSaveDataInfo` (fsOpenSaveDataInfoReader) | SEAM |
      | test_existing_save_capacity_contract.py | save_restore_zip.cpp `RestoreSaveZip` capacity admission | SEAM |
      | test_game_save_manager_contract.py | game_save_manager.cpp inventory/create/grow; `test_localization_parity` reads en/uk JSON | SEAM (i18n part KEEP) |
      | test_mtp_save_contract.py (+ mtp_save_* fixtures) | haze_save_proxy.cpp / haze_save_proxy_scan.cpp naming and read-only enforcement | SEAM |
      | test_save_backup_publication_contract.py (+ fixtures) | save_backup_writer.cpp / save_backup_pub.cpp checked publication | SEAM |
      | test_save_folder_import_contract.py (+ fixtures) | save_folder_restore.cpp `RestoreSaveFolder`, save_folder_staging.hpp `CheckedJoinPath` | SEAM |
      | test_save_folder_restore_contract.py | save_restore_route.cpp; `MatchesRestoreDestination` | SEAM |
      | test_save_journal_lifecycle_contract.py (+ fixtures) | threaded_file_transfer_zip_io.cpp `TransferUnzipAll` checked commit loop | SEAM |
      | test_save_metadata_wire_contract.py (+ fixtures) | save_metadata_decoders.cpp / save_archive_metadata.cpp `ReadArchiveSaveMetadata` | SEAM |
      | test_save_payload_summary_contract.py (+ fixtures) | threaded_file_transfer_preflight.cpp `TransferUnzipPreflight` accounting and overflow guards | SEAM |
      | test_save_post_restore_verification_contract.py (+ fixtures) | threaded_file_transfer_verify.cpp `VerifyArchiveAgainstNative` | SEAM |
      | test_save_recovery_contract.py (+ fixtures) | save_restore_zip.cpp `RestoreSaveZip` recovery stream | SEAM |
      | test_save_slot_backend_contract.py | save_slot_backend.cpp / save_slot_admission.cpp `CreateSaveDataChecked`, `ExtendSaveDataChecked` | SEAM |
      | test_shutdown_lifecycle_contract.py | app/main/haze_helper/web exit ordering, modelled by MockSystem threads | DROP (hardware-only interleavings; no callable C++ unit) |
      **Done when:** every row has a verdict and the table is in this file.
- [x] 2.3 **Host test harness can link `.cpp` units.** *(runner verified with a scratch unit; first real LINK user arrives with 2.5)* Extend `tests/run.sh`: a test file may declare
      `// LINK: sphaira/source/foo_logic.cpp` lines at the top; the runner adds them to the g++ command.
      Rule: only libnx-free sources may be listed. **Verify:** existing tests still pass; add one test using `LINK:`.
      <!-- blocked: runner supports LINK: (checked with a scratch unit); no libnx-free .cpp exists yet, first LINK test arrives with the first 2.5 seam -->
- [x] 2.4 **Convert PURE rows** (one task per row, one commit each, in table order). Each new `tests/test_<name>.cpp`
      reproduces the scenarios from the Python model (same inputs/expectations), then the Python file + its fixtures
      are deleted. **Done when:** the py file is gone and the cpp test is in `run.sh` and passes.
- [ ] 2.5 **Convert SEAM rows** (one task per row). Extract decision logic into a header/libnx-free unit with
      **no behavior change** (same inputs → same outputs; keep call sites one-line thin), add the cpp test, delete the py.
      Build checkpoint after every 2–3 seams (these touch product code).
      <!-- build checkpoint passed at v0.13.934; next: test_dbi_restore_admission_contract.py preflight/unzip/sources seam (2.2 table order, MTP row waits for 3.2) -->
- [ ] 2.6 **Delete `tests/contract_fixtures/`** once nothing imports it. **Verify:** `grep -rl contract_fixtures tests` empty;
      `find tests -name '*.py' | xargs wc -l | tail -1` < 3000 lines.
- [ ] 2.7 Build checkpoint + commit.
- [ ] 2.8 **C++ tests that test copies.** Executor found 11 `tests/*.cpp` (incl. `test_save_restore_contract.cpp`) that re-implement project logic locally instead of including the project header, plus `test_save_restore_contract.cpp` and `tools/module_catalog/tests/test_catalog.py` asserting on C++ source text (catalog test currently fails 1/9). List them (`grep -L '#include "' tests/test_*.cpp` is a start; then read each), and for each: include the real header (PURE), extract a seam (SEAM, same rules as 2.5), or delete (DROP). Fix or delete the failing catalog test. **Done when:** every `tests/test_*.cpp` includes at least one project header and no test greps C++ source.
      <!-- build checkpoint passed at v0.13.934; done: test_save_restore_contract.cpp deleted (source greps only), test_catalog.py text check removed (8/8 pass); next: 10 copy tests, all SEAM (no real function of that name exists): hbl_nro_reader, header_network_layout, header_service_indicators, list_null_safety, queue_outcome, screensaver_title, tico_assoc, title_scaling (usb3_indicator done in v0.13.959; mtp_progress_calc folded into test_mtp_transfer_state.cpp in v0.13.960); test_transport_install_queue already includes a project header (<...>) -->

## Phase 3 — Stability (audit F3, F6, F7)

- [x] 3.1 **Cross-thread globals.** For each of the 45 plain `bool/u64/int g_*` globals
      (`grep -rnE '^(extern|static)?\s*(bool|u64|u32|int|size_t)\s+g_[a-z_0-9]+' sphaira/source sphaira/include`):
      list every reader/writer with `graphify explain` + grep; decide `single-thread` (leave, add a comment
      `// main thread only`) or `shared` (→ `std::atomic<T>` or move under the module's existing mutex).
      Known shared: `haze/haze_internal.cpp:33 g_is_running` (read by `IsRunning/IsRecovering` from UI thread, written
      in `Init/Exit`), `ftpsrv_helper.cpp:144 g_is_running`, `log.cpp g_thread_running/g_thread_stop`,
      `net.cpp g_cache_valid/g_cache_value/g_request_open`, `account_link.cpp g_daemons_terminated`.
      **Done when:** the grep above returns 0 unannotated non-atomic globals. One commit per module.
- [x] 3.2 **MTP transfer state machine** (after H1/H2 are fixed and re-verified — refactor the behavior that works, not the one that is broken). Create `sphaira/include/haze/mtp_transfer_state.hpp`:
      `struct MtpTransferState { bool active, aborted, ui_alive; u64 seq, handled_seq; }` plus pure transition
      functions, each returning the actions to perform: `OnFileStart(state, seq)`, `OnFileDone(state)`,
      `OnUserCancel(state) -> {cancel_worker, signal}`, `OnUiClosed(state) -> {relaunch}`, `OnExit(state)`.
      Move the logic now spread over `haze_helper.cpp` L54–235 (ProgressBox lambda, cancel callback, completion
      callback), `haze_callback` L237–350, `Init` L356–507, `Exit` L510–540 into those functions; the call sites keep
      the mutex and only apply returned actions. **No behavior change** vs. the 1.4 baseline.
      Write `tests/test_mtp_transfer_state.cpp` from `tests/test_mtp_cancellation_models.py` scenarios
      (Switch-side cancel, PC cancel/URB abort, cancel during idle window, relaunch after late file, exit during
      transfer, double cancel is a no-op). Then delete the py model. `[USER]` re-runs checklist section A.
      <!-- shipped in v0.13.960; the user re-ran checklist section A afterwards: all PASS, A10 remarks fixed in v0.13.961 -->
- [x] 3.3 **Thread lifecycle parity.** For each `threadCreate` (19) confirm a matching `threadWaitForExit` +
      `threadClose` on every exit path (normal, error, `Exit()` while running). Fix leaks. Start with
      `haze_helper.cpp`, `ftpsrv_helper.cpp`, `log.cpp`. **Done when:** a table thread→create/wait/close sites is in
      the CHANGELOG entry and no path lacks a wait.
- [x] 3.4 **Bounded string ops.** `grep -rnE '\b(strcpy|strcat|sprintf)\(' sphaira/source sphaira/include` (45).
      Skip calls whose source is a string literal into a buffer sized ≥ literal. Replace the rest with
      `snprintf`/`strncpy`+terminator/`std::string`. **Done when:** every remaining call has a `// literal, bounded`
      comment or is replaced.
- [x] 3.5 **Graph holes.** `defines.hpp` L255, `net.hpp` L32, `nxlink.h` L47, `ams_su.h` L36, `hbl/source/main.c` L27
      break the tree-sitter parser (macro-heavy). If a trivial rewrite (e.g. a macro used as a type, a missing
      semicolon in a macro) fixes extraction without changing semantics, do it; otherwise note `// graphify: parse stop`
      and move on. **Verify:** `graphify update .` warning count drops.
- [x] 3.7 **Follow-ups found by the executor in batch 2** (product code; one commit each, after the v0.13.933 build checkpoint):
      (a) `FsPath::From(std::string)` and `operator+=(std::string)` copy without a bound — add a bound, truncate with NUL, log on truncation.
      (b) `nro.cpp:61` `strncpy(..., len-4)`: no destination bound and `len < 4` underflows — guard `len >= 4` and bound by `sizeof(dst)-1`.
      (c) `fs.cpp:157` `strncat` without a bound.
      (d) `ProgressBox`, `download`, transfer core: if `threadStart` fails they still `threadWaitForExit` on a never-started thread — check the Result of `threadStart` and skip wait/close on failure.
      (e) `s_install_thread_created` is a plain `static bool` crossing threads — make it atomic or guard it.
      (f) `web.cpp`: restart while an old worker is still in `WebShareStop` races on `g_share_threads`; `g_share_port`/`g_share_offline` cross threads unguarded — serialize stop/start under one mutex or an atomic generation counter (no new abstractions).
      **Done when:** each item has a commit or a one-line `wontfix: reason` here.
- [ ] 3.6 Build checkpoint + `[USER]` checklist sections A–C again. Commit.

## Phase 4 — Documentation for humans and models

- [x] 4.1 **`docs/dev/ARCHITECTURE.md`** (≤ 200 lines, English): directory → responsibility table; the 21 `Menu`
      classes as a table `namespace | header | what it shows`; thread map (every thread: who starts it, which
      globals/mutex it shares); god nodes and the rule for each; build/test commands; where i18n keys live and how
      parity is checked. Generate from `graphify explain` + `GRAPH_REPORT.md` God Nodes, then verify against code.
      Add the path to AGENTS.md routing line.
- [x] 4.2 **Wiki parity.** For every feature heading removed from README in 0.5, confirm the wiki page covers it;
      add a `docs/wiki/Developer-Guide.md` that links `ARCHITECTURE.md`, `CHANGELOG.md`, test commands, test-build skill.
- [x] 4.3 **Tooling docs.** `tools/i18n-translate/README.md` (how to add a language, run parity test) and
      confirm `tools/module_catalog/README.md` is current.
- [x] 4.4 Commit `docs: architecture, developer guide, tooling`.

## Phase 5 — Executor ergonomics (small, do last)

- [x] 5.1 `tests/run.sh --quick`: host C++ tests + dead-symbol guard only (< 60 s). Document in AGENTS.md.
- [x] 5.2 `tools/dev/check.ps1`: PowerShell wrapper that calls the WSL `tests/run.sh --quick` so it works from a
      Windows shell with one command.
- [ ] 5.3 Final build checkpoint; `[USER]` full checklist; the user decides on `git push`.
- [x] 5.4 **Graphify noise.** `docs/dev/CHANGELOG.md` is indexed as a god node (640 edges) and skews the report. Exclude it (and `docs/dev/history`, `graphify-out`) via graphify's ignore mechanism (`graphify --help`, look for ignore/exclude; else a `.graphifyignore` or `.gitignore`-style config) and re-run `graphify update .`. **Done when:** CHANGELOG is absent from `## God Nodes`.

---

## Phase D — decisions from the docs review (2026-10-02)
Context: `docs/dev/AUDIT-2026-10-02-docs.md`. When a task changes behaviour, update the matching `docs/site/{en,uk}` page.

- [x] D.1 🔥 **DBI Backend Qt: ship the Windows USB driver** so "PC Install (USB)" works without a manual Zadig step
      (separate repo). Then replace the TODO in `docs/site/*/install/usb.md#requirements` and the site `inc/zadig.txt`.
      Done in DBI Backend Qt 2.9.0: bundled libusb-1.0, WinUSB installed by the app (UAC), udev rule on Linux.
      Hub unchanged (no MS OS 2.0 descriptors: usb:ds exposes only interface-level control requests). Console test pending.
- [ ] D.2 **Remove the install-enable switch** (Settings → Install → Enable sysMMC/emuMMC and the ban-warning prompt).
      Installing is always allowed. Drop `install/index.md#enable` and the "turn installing on" steps (docs, site, video 02).
- [ ] D.3 **Bring back restoring profile backups.** Backups from Delete user and Console Transfer → Share User Backups
      can be made but not restored (dead `users_restore*`, `users_manage*`). Give them a menu entry.
- [ ] D.4 **After installing a system interface translation, ask before switching Kefir Hub's language.** Today
      `TryAutoSwitchLanguage` (settings_translations.cpp:427-518) switches it silently when a matching UI language
      exists. Offer it in a dialog instead.
- [ ] D.6 **Auto-update check also on network connect**, not only once per launch (main_menu.cpp:90-226).
- [ ] D.7 **Kefir Settings: USB 3.0 shows On when the key is absent** (settings_kefir.cpp:158-162). Every switch must
      read Off by default.
- [ ] D.5 `[USER]` Screenshots (`python docs/site/shotlist.py`) and video recording (`docs/video/*/script.md`).

---

## Phase F — features DBI has and Kefir Hub does not (decided 2026-10-03)
Sources: DBI 810 walked in Eden; DBI 912 strings (`D:\git\dev\dbi_patcher\data\dictionary.xlsx`, column 1); DBI
README (old). DBI 905/912 quit themselves in Eden and Ryujinx (`am Exit` at 3 s), so their screens are not seen yet:
F.0 first. Rule: user-first UI (names, one question, no dead options). Each task: check Hub first, then build only
the gap. Code tasks follow the delivery ritual and update `docs/site`.

- [ ] F.0 `[USER]` DVR capture of DBI 912 on the console: main menu, Tools, Installed games → (+) and game card,
      Saves → each tab (+), Tickets (+), file browser (+), Activity log, Settings. Confirms F.1–F.12 behaviour.
- [ ] F.1 ⏸ *Deferred by the user (needs a focused session).* **Game needs newer firmware (SDK).** (a) Install
      warning also when the Program NCA `SdkAddonVersion` major > firmware major (today only RequiredSystemVersion,
      `yati_metadata.cpp:427-440`). (b) Installed game → "Reset required version". (c) Experimental "Use SDK from
      another installed game" (DBI 828+ Export/Replace SDK): donor = installed game with the highest SDK ≤ firmware
      major; copy its exefs `sdk` to `/atmosphere/contents/<ProgramID>/exefs/sdk`; show it as active with one-tap
      removal; offer removal after a firmware or game update. Never download or bundle sdk files; never swap silently.
- [ ] F.2 **Game updates.** Hub finds installed games with a newer update/DLC (titledb versions; Hub reads titledb in
      `ownfoil_api.cpp`), the user picks which app fetches it — TorrentShopNX or pipensx (confirmed 2026-10-04,
      `D:\git\dev\_kefir\kefir\switch`) — that app downloads, Hub installs. Better still (user): Hub downloads just
      the update itself. Research how both apps get a request and where they fetch updates from first.
      Research (2026-10-04): neither app takes argv yet; both install what they download. pipensx has an
      update-only download (`game_update_install.hpp`) and an index with `latestVersion` per torrent
      (i3sey/pipensx-metadata `game_metadata_index.json`). Hub: compare installed patch version, then
      `nro_launch(app, "--update <tid>")` once the authors add it (ask: `--update/--dlc <tid>`); native download
      only via a user's TorrServer (`/stream?...&index=N&play` + `yati::source::Http`).
- [x] F.4 **System cleanup** (Tools → "Clean system junk", today "Coming soon"). DBI 905 screen (user screenshot
      2026-10-04): one list of checkboxes, all on, then "Run selected": delete old game updates; orphaned content on
      SD; orphaned content on NAND; placeholders on SD; placeholders on NAND; unused tickets; fix tickets with dump
      errors; downloaded system update; clear erpt_reports; clear ticket cache; clean /atmosphere/contents (folders of
      titles no longer installed — keep sysmodules); saves of deleted users. Show what each step freed.
- [ ] F.5 **Mods.** Big area, split into tasks when started: (a) installing mods automatically when a game folder
      with mods is dropped over MTP together with the game (put them in `/atmosphere/contents/<TitleID>/`; agreed
      2026-10-04: folders accepted, files under `…/atmosphere/contents/<tid>/…` or `…/<tid>/…` go to the SD, mod files
      without a tid in the path are skipped without stopping the copy); (b) mods
      size in the game card, "Delete mods" (also offered when deleting the game) — done; (c) "Pack mods into romfs.bin".
- [x] F.6 **Saves.** "Check backups" (validate backup archives), browse a backup's files read-only, automatic backup
      before deleting a save (DBI FoolproofSaveDelete).
- [x] F.8 **Language bound to a translation pack.** Ukrainian translations ship as a repacked update + translation +
      metadata saying which language the game must start with (some must be forced to English). Hub reads that
      metadata on install and forces the language for that title; plus a manual "Force language" per installed game
      (DBI Force language). Find the mechanism first (how DBI forces it). The metadata format is mine to define, then
      agree it with the shop app `D:\git\dev\swuk_shop_nx` (user, 2026-10-04).
      Research (2026-10-04): mechanism = Atmosphère `/atmosphere/contents/<app_id>/config.ini`
      `[override_config] override_language=<en-US|en-GB|ja|fr|de|es-419|es|it|nl|fr-CA|pt|ru|ko|zh-Hant|zh-Hans|pt-BR>`
      (what DBI writes; Kefirosphere keeps it; merge with existing keys). Format: `kefir_lang.json` in the PFS0
      root of the repacked NSP: `{"format":1,"title_id":"<base tid>","language":"en-US"}`; swuk_shop_nx (PC
      repacker) adds it per title.
- [x] F.9 **Hex view** in the file browser. (Second panel exists. RAR/7z do not: only zip extracts; 7z only for RetroArch in the App Store.)
- [x] F.9b **Archive extraction** in the file browser: RAR, 7z, xz, tar, gz (zip already works). Approved 2026-10-04.
- [x] F.10 ~~**Fill free NAND space with zeros**~~ Removed on the user's request (2026-10-07, v0.14.018). The SD
      zero-fill stays (restored in v0.14.022 under Maintenance).
- [x] F.16 **Tools → Tools grouped** into Diagnostics / Settings / Maintenance captions (user, 2026-10-07; v0.14.018).
- [x] F.11 **Game patches, switchable at install and afterwards** (need sigpatches): remove the linked-account
      requirement, allow screenshots, allow video capture (DBI PatchUAC/PatchScreenshot/PatchVideoRec). Install
      options plus the same switches on an installed game (DBI "Edit parental controls"), on and off.
      Research (2026-10-04): NACP `startup_user_account`(0x3025)=1, `required_network_service_license_on_launch`
      (0x3213)=0, `screenshot`(0x3034)=0, `video_capture`(0x3035)=2 (+screenshots on). Patch the Control NCA after
      it is registered: read via ncm, decrypt section, patch, rehash IVFC (verify old hashes first), fs_header_hash,
      re-encrypt header, placeholder + Register same id, invalidate ns control cache. Base and update controls.
- [x] F.12 **MTP "Installed games": add a mods folder** per game (`atmosphere/contents/<TitleID>`; DBI "Mods &
      cheats"). NSP and combined NSP already exist.
- [x] F.14 **System information** (Tools, today "Coming soon"). DBI 905 has one scrolling page (user screenshots
      2026-10-04): firmware (version, hash, display name, DRAM id, burnt fuses, SoC, hardware type, purpose, device id,
      HiZ/kiosk, serial read/guessed, language, region, console nickname, parental PIN set?); Atmosphère (version, key
      generation, target firmware, git hash, RCM bug patched, exosphere CAL0 flags, emuMMC, USB 3.0 forced, supported
      HOS); SD card (CID, manufacturer, OEM, product, revision, serial, date); power source and battery charging
      (charge %, raw, age, voltage, current, limits, PD source); saved battery controller params and MAX17050
      registers (full capacity vs design, cycles); hardware (BT/WLAN MAC, config id, serial, battery lot, screen
      panel); play activity totals. User-first: group into sections, names not raw registers where possible.
- [x] F.15 **Save folder restore without metadata** (JKSV/Checkpoint folders): when the game is installed, take the
      save size from its NACP instead of refusing with "Backup folder metadata is missing…" (only for a user with
      no save of that game yet). Found by the other session 2026-10-04; user agreed.
- [x] F.14b **System information as grouped tables** (user, 2026-10-07; v0.14.019): groups open with A or a tap, rows are
      parameter → value; every DBI field; real serial from PRODINFO / a backup with the source named, never a computed one.
- [x] F.13 **Game transfer between two consoles** over the existing console-link mode, by cable and over the air.
      Entry: Console Transfer menu only (user, 2026-10-07: all console-to-console options in one place for now; other
      entry points, e.g. the installed game menu, to be decided after a walk through the menus). Base, updates and DLC.
      Wi-Fi shipped in v0.14.020 (web server /games + yati HTTP install). Cable decided against (2026-10-07): MTP and
      the USB install protocols are both device-side; a console-to-console cable needs a usb:hs host implementation
      plus a hardware spike on USB-C role negotiation between two Switches. Reopen only with that spike.

Not taken from DBI (decided 2026-10-03): tickets screen (users delete tickets by mistake), activity log, current
firmware dump, fake app records, DLC unlocker, "Convert to fake", clear error flag.

---

## Phase S — DocsDemo build: screenshots of every screen, any language (decided 2026-10-03)
Goal: every `<!-- shot -->` marker can be taken in Eden, in any UI language, by `tools/docs/shoot.ps1`, without a
console. A `DOCS_DEMO` build is the normal app plus fictional content: six meme games with covers, their saves,
network replies and frozen demo scenes. Data: `docs/site/fixtures/sdmc/config/kefir/demo/` (`titles.json`, `icons/`;
read at run time from `sdmc:/config/kefir/demo/`). Workflow: `.agents/skills/update-docs/SKILL.md` → Screenshots.
Rules for this phase:
- Release presets never contain demo code: `option(DOCS_DEMO OFF)`; demo sources are added to the target only when ON.
- Demo code lives in `sphaira/source/demo/` (+ `include/demo/`). In other modules only `#if DOCS_DEMO` hooks of a few
  lines that call into `demo::`. No behaviour change when OFF.
- Prefer link-time wrapping (`-Wl,--wrap=<libnx fn>`, `__wrap_`/`__real_` in `demo/demo_wrap.cpp`) over hooks: one
  wrapper covers every caller. Wrappers return real results plus the demo ones (`__real_` first), so Eden content stays.
- Demo data is fictional only: no real people, no real accounts, no third-party art. App Store / Themezer / Ownfoil /
  updater replies use the meme titles and simple generated images, not copies of real catalogues.
- Demo code never installs, deletes, writes saves, links accounts or touches NAND; actions on demo items may fail
  with the normal error. It only has to *look* right for the screenshot.
- Tasks that touch `sphaira/` follow the delivery ritual (version bump, CHANGELOG). Build checkpoint closes the phase.
- Shot status: `python docs/site/shotlist.py`. Each task lists the shot ids it unlocks; it is done when those shots
  are recorded (`Rec`) in English and their PNGs checked.

- [x] S.0 **Spike: build switch + one wrapper.** Add `option(DOCS_DEMO "docs screenshot build" OFF)` to
      `sphaira/CMakeLists.txt` (`target_compile_definitions(... DOCS_DEMO=$<BOOL:...>)`, demo sources and
      `target_link_options(sphaira PRIVATE -Wl,--wrap=nsListApplicationRecord)` only when ON) and a `DocsDemo`
      configure/build preset in `CMakePresets.json` (inherits ReleaseWithInstall, `DOCS_DEMO=ON`, output
      `build/DocsDemo/`, nro named `kefir-hub.nro`). `demo/demo_data.cpp`: parse `titles.json` (yyjson) once, cache.
      `demo/demo_wrap.cpp`: `__wrap_nsListApplicationRecord` = real records, then demo ids appended (offset-aware).
      Also check whether the normal build reaches the network in Eden (App Store loads?) and write the answer in
      the CHANGELOG line: it decides S.3. **Done when:** Games shows the six titles (no icons yet is fine);
      `strings build/ReleaseWithInstall/kefir-hub.nro | grep -c __wrap_` → 0.
- [x] S.0b **Focus-free input + every language on one screen** (decided 2026-10-03, after S.0). Eden ignores
      PostMessage keys while its window is not active, and Windows will not let a script activate it, so
      `eden.ps1`/`shoot.ps1` cannot drive the Hub unattended. (1) Input from a file: one `#if DOCS_DEMO` hook where
      `app_frame.cpp` polls the pad (`padGetButtonsDown`): each frame `demo::` reads queued commands from
      `sdmc:/config/kefir/demo/input.txt` (button names as in shots.json, `wait <s>`) and ORs the buttons into
      kdown/kheld; consumed lines are removed. `eden.ps1` `B`/`W` write there instead of PostMessage (`Rec` unchanged).
      (2) Language loop in one Eden launch: command `lang <code>` = `App::SetLanguage` without the restart prompt,
      rebuild the menu stack (labels are cached at construction), replay the current recipe's steps, then write
      `sdmc:/config/kefir/demo/ready` → the PC side takes `Shot` and sends the next `lang`. `shoot.ps1 -Lang uk,en`:
      per shot, open it once, loop all languages, go to the next shot (no Eden restart per language).
      Also: skip the old-forwarder notice in DOCS_DEMO (`NotifyUi`, it shows on every Eden launch, nothing is removed).
      **Done when:** with Eden in the background (user's window in front) `shoot.ps1 -Lang en,uk -Only <one shot>`
      produces both PNGs with correct labels in each language.
- [x] S.1 **Games: names, icons, contents.** Wrap what `title_info.cpp` and the game menu read for a demo id:
      `nsGetApplicationControlData` (NACP built from titles.json: name per language — fill every NACP language slot,
      `uk` name for Ukrainian, `en` for the rest; publisher, display version; JPEG from `icons/`),
      `nsListApplicationContentMetaStatus` (base + updates + `dlc` add-ons, storage from `storage`),
      and the size/occupied-size call used by Game Details. Make `LoadControlManual` skip demo ids (hook) so it does not
      walk ncm. Graph first: `graphify explain "ForEachApplicationRecord"`, `"GetMetaEntries"`, `"ThreadData::Get"`.
      Shots: `games-list`, `games-details`, `games-move-summary`, `cheats-select`, `cheats-files` (cheat .txt files go
      into fixtures `atmosphere/contents/<id>/cheats/`; Build ID: read how cheats_ops gets it and fake it the same way).
- [x] S.2 **Saves and users.** Wrap `fsOpenSaveDataInfoReader` / `fsSaveDataInfoReaderRead` / `...Close`: for
      `FsSaveDataSpaceId_User` append one `FsSaveDataInfo` per titles.json save (uid = Eden profile at `user` index via
      `accountListAllUsers`). Saved backups: write `tools/docs/make_demo_backups.py` that creates backup archives in
      the exact format and folder the Backups tab reads (`save_backup_pub.cpp`, `save_archive_metadata.cpp`) into
      fixtures, three dated backups for one game, two owners for another. `[USER]` once: three Eden profiles with
      fictional names (e.g. Pixel, Kotyk, Guest). Linked / Not linked on the Users grid: find its source
      (`graphify explain` on the users grid draw); wrap if it is one call, else mark `users-list` as `user`.
      Shots: `saves-list`, `saves-backup-options`, `saves-select-backup`, `saves-restore-confirm`, `saves-backup-group`,
      `users-list`, `users-delete-hold`.
- [x] S.3 **Network replies from fixtures.** (Skip parts S.0 found working in Eden.) Online state: wrap the
      `nifm` calls `net.cpp` uses so the header shows Wi-Fi connected with an IP. Download layer: in `download*.cpp`
      one `#if DOCS_DEMO` hook before curl: if `sdmc:/config/kefir/demo/http/<host>/<path>` (POST: `<path>.<fnv1a of
      body>`) exists, return it as the response (status 200, same callbacks), else fail like offline. Fixtures, all
      fictional, minimal JSON shaped after each parser: App Store repo (the meme apps + icons), Themezer list (a few
      generated previews), Kefir/firmware releases newer than current (Updater, changelog), Hub self-update, Ownfoil
      server (shop of the six meme games), translations list, About release notes. Saved Wi-Fi list for `system-tools-wifi`:
      wrap the nifm profile listing `wifi_menu.cpp` uses. Shots: `software-appstore-grid`, `software-appstore-entry`,
      `software-extract-options`, `themes-themezer-grid`, `updater-main`, `updater-changelog`, `updater-firmware-confirm`,
      `updater-downgrade-warning`, `updater-hub-update-prompt`, `cheats-select` (CheatSlips reply; Build ID from S.1), `network-ownfoil-servers`, `network-ownfoil-catalog`,
      `network-ownfoil-install-panel`, `kefir-settings-translate`, `settings-about`, `system-tools-wifi`.
      Never let a demo action download or install for real: Install buttons on demo items may stop at the first prompt.
- [x] S.4 **Demo scenes.** `[demo] scene=<name>` in config.ini (written by `shoot.ps1` from the recipe's `"scene"`).
      After the main menu is up, `demo::StartScene()` pushes the screen in a frozen state; no worker thread, no I/O.
      Install session: a `DemoSession` deriving `InstallSession` (`dbi_menu.hpp`) that fills `m_queue`, `m_log`,
      stats and state from a per-scene table (meme game file names) and never starts a transfer. Mode badges
      (USB 2.0, MTP, FTP): wrap `usbDsGetState` / the speed query where the header reads them. Scenes and shots:
      `install-sd-card-queue-review` (4 packages, one Analysis failed), `install-sd-card-queue-progress` (2 of 4,
      speed graph, log), `install-sd-card-minimized-badge`, `install-sd-card-screensaver`, `install-sd-card-summary`
      (3 installed, 1 failed), `install-mtp-progress`, `network-ftp-progress`, `install-usb-waiting`, `install-usb-queue`,
      `console-transfer-remote-list`, `console-transfer-te-confirm`, `software-extract-options` (ZipExtractBox on a fixture zip), `updater-firmware-confirm` (install prompt), `console-transfer-ip-entry` (keyboard: try Eden's
      software keyboard first). Game card row: wrap `fsDeviceOperatorIsGameCardInserted` and serve the
      `storage: gamecard` title as the card → `install-gamecard-games-row`. Second storage for `file-browser-split`,
      `file-browser-sources`, `settings-sources`: a demo mount named like a USB drive over `sdmc:/config/kefir/demo/usb/`.
- [x] S.5 **Validate fixtures** (host test, real data): `tests/test_demo_fixtures.py` — titles.json parses, ids are
      base ids (`...000`, unique), every icon exists and is a 256x256 JPEG, every `user` index < 3, every http fixture
      parses as JSON. Runs in `tests/run.sh`.
- [x] S.6 Build checkpoint (`ReleaseWithInstall` and `DocsDemo`), then record the recipes for all remaining markers in
      English, mark PC-side shots (`install-mtp-explorer`, `network-ftp-client`, `sharing-mtp-pc`) `user`, retake the
      49 old English shots from the DocsDemo build for one consistent look, then `shoot.ps1 -Lang uk,en`, check every
      PNG, `python tools/docs/sync_site_shots.py`, docs build, commit (docs: no bump). Report `[USER]` leftovers.

---

## Not to do (from audit)
- No refactor of `App`; no new abstractions, registries, interfaces with one implementation.
- No file splits for their own sake; the 600-line cap is already met.
- No line cap on i18n JSON, tables, embedded HTML.
- No `git push`, no `--force`, no worktrees. Deleting junk is fine.
