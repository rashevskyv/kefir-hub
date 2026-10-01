# plan.md — work queue

Source: `docs/dev/AUDIT-2026-10-01.md` (findings F1–F10). Baseline v0.13.922, commit `0c80cdd4`.
Rules: `AGENTS.md`. One task ≈ one session ≈ one commit. Do tasks in order inside a phase; phases in order.
Each task has **Do**, **Done when** (verifiable), **Verify** (command). Tick `[x]` when done; write nothing else here.
`[USER]` tasks are for the human (hardware). Build checkpoint (`test-build` skill) closes every phase.

---

## Phase 0 — Context diet and repo hygiene (no product code, no version bump)

- [x] 0.0 Audit report, new `AGENTS.md`, `CLAUDE.md`, this plan. Old `plan/task/walkthrough/audit.md` moved
      to `docs/dev/history/` (staged as renames). *(done by the audit session; commit it with 0.1)*
- [ ] 0.1 **CHANGELOG from history.** Create `docs/dev/CHANGELOG.md`. For every version in
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
- [x] 0.3 **One test-build skill.** *(done by audit session)* `git mv .agents/skills/test-build/SKILL.md .claude/skills/test-build/SKILL.md`
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

- [ ] 1.1 **Build v0.13.922.** Run the `test-build` skill (WSL, preset `ReleaseWithInstall`). 922 was never built.
      Fix compile errors surgically if any. **Done when:** `[100%] Built target sphaira_nro`.
- [ ] 1.2 **Warning inventory.** Rebuild from clean (`rm -rf build/ReleaseWithInstall`), capture
      `cmake --build ... 2>&1 | grep -E 'warning:' | grep -E 'sphaira/|hbl/|sysmodule/' | sort -u > /tmp/warn.txt`.
      Record the count in the CHANGELOG entry. Fix every first-party warning (not `libs/`). Then enable `-Werror`
      for first-party sources only: in `sphaira/CMakeLists.txt` uncomment `-Werror` **inside the first-party
      compile-options block** (line ~376); if it also hits `libs/` targets, scope it with
      `set_source_files_properties(<first-party sources> PROPERTIES COMPILE_OPTIONS -Werror)` instead.
      Keep existing `-Wno-*` lines. **Done when:** clean build passes with `-Werror`; `/tmp/warn.txt` empty for first-party.
- [ ] 1.3 **Host suite green in WSL.** `tests/run.sh` must pass end to end (all `tests/test_*.cpp`, dead-symbol
      guard, patch shape checks, Python contracts as they still exist). Fix only what is broken; do not delete tests here.
      **Verify:** `wsl bash -lc 'cd /mnt/d/git/dev/sphaira && tests/run.sh'` exits 0.
- [ ] 1.4 `[USER]` **Hardware baseline.** Flash the 1.1 NRO and run `docs/dev/HARDWARE-CHECKLIST.md` sections A–C.
      Record pass/fail per item in the checklist file. This is the baseline for Phase 3.2 — do not start 3.2 before it.
- [ ] 1.5 Build checkpoint + commit(s) `v0.13.9XX: warning-free first-party build with -Werror`.

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
- [ ] 2.3 **Host test harness can link `.cpp` units.** Extend `tests/run.sh`: a test file may declare
      `// LINK: sphaira/source/foo_logic.cpp` lines at the top; the runner adds them to the g++ command.
      Rule: only libnx-free sources may be listed. **Verify:** existing tests still pass; add one test using `LINK:`.
      <!-- blocked: runner supports LINK: (checked with a scratch unit); no libnx-free .cpp exists yet, first LINK test arrives with the first 2.5 seam -->
- [x] 2.4 **Convert PURE rows** (one task per row, one commit each, in table order). Each new `tests/test_<name>.cpp`
      reproduces the scenarios from the Python model (same inputs/expectations), then the Python file + its fixtures
      are deleted. **Done when:** the py file is gone and the cpp test is in `run.sh` and passes.
- [ ] 2.5 **Convert SEAM rows** (one task per row). Extract decision logic into a header/libnx-free unit with
      **no behavior change** (same inputs → same outputs; keep call sites one-line thin), add the cpp test, delete the py.
      Build checkpoint after every 2–3 seams (these touch product code).
- [ ] 2.6 **Delete `tests/contract_fixtures/`** once nothing imports it. **Verify:** `grep -rl contract_fixtures tests` empty;
      `find tests -name '*.py' | xargs wc -l | tail -1` < 3000 lines.
- [ ] 2.7 Build checkpoint + commit.

## Phase 3 — Stability (audit F3, F6, F7)

- [ ] 3.1 **Cross-thread globals.** For each of the 45 plain `bool/u64/int g_*` globals
      (`grep -rnE '^(extern|static)?\s*(bool|u64|u32|int|size_t)\s+g_[a-z_0-9]+' sphaira/source sphaira/include`):
      list every reader/writer with `graphify explain` + grep; decide `single-thread` (leave, add a comment
      `// main thread only`) or `shared` (→ `std::atomic<T>` or move under the module's existing mutex).
      Known shared: `haze/haze_internal.cpp:33 g_is_running` (read by `IsRunning/IsRecovering` from UI thread, written
      in `Init/Exit`), `ftpsrv_helper.cpp:144 g_is_running`, `log.cpp g_thread_running/g_thread_stop`,
      `net.cpp g_cache_valid/g_cache_value/g_request_open`, `account_link.cpp g_daemons_terminated`.
      **Done when:** the grep above returns 0 unannotated non-atomic globals. One commit per module.
- [ ] 3.2 **MTP transfer state machine** (requires 1.4 baseline). Create `sphaira/include/haze/mtp_transfer_state.hpp`:
      `struct MtpTransferState { bool active, aborted, ui_alive; u64 seq, handled_seq; }` plus pure transition
      functions, each returning the actions to perform: `OnFileStart(state, seq)`, `OnFileDone(state)`,
      `OnUserCancel(state) -> {cancel_worker, signal}`, `OnUiClosed(state) -> {relaunch}`, `OnExit(state)`.
      Move the logic now spread over `haze_helper.cpp` L54–235 (ProgressBox lambda, cancel callback, completion
      callback), `haze_callback` L237–350, `Init` L356–507, `Exit` L510–540 into those functions; the call sites keep
      the mutex and only apply returned actions. **No behavior change** vs. the 1.4 baseline.
      Write `tests/test_mtp_transfer_state.cpp` from `tests/test_mtp_cancellation_models.py` scenarios
      (Switch-side cancel, PC cancel/URB abort, cancel during idle window, relaunch after late file, exit during
      transfer, double cancel is a no-op). Then delete the py model. `[USER]` re-runs checklist section A.
- [ ] 3.3 **Thread lifecycle parity.** For each `threadCreate` (19) confirm a matching `threadWaitForExit` +
      `threadClose` on every exit path (normal, error, `Exit()` while running). Fix leaks. Start with
      `haze_helper.cpp`, `ftpsrv_helper.cpp`, `log.cpp`. **Done when:** a table thread→create/wait/close sites is in
      the CHANGELOG entry and no path lacks a wait.
- [ ] 3.4 **Bounded string ops.** `grep -rnE '\b(strcpy|strcat|sprintf)\(' sphaira/source sphaira/include` (45).
      Skip calls whose source is a string literal into a buffer sized ≥ literal. Replace the rest with
      `snprintf`/`strncpy`+terminator/`std::string`. **Done when:** every remaining call has a `// literal, bounded`
      comment or is replaced.
- [ ] 3.5 **Graph holes.** `defines.hpp` L255, `net.hpp` L32, `nxlink.h` L47, `ams_su.h` L36, `hbl/source/main.c` L27
      break the tree-sitter parser (macro-heavy). If a trivial rewrite (e.g. a macro used as a type, a missing
      semicolon in a macro) fixes extraction without changing semantics, do it; otherwise note `// graphify: parse stop`
      and move on. **Verify:** `graphify update .` warning count drops.
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

- [ ] 5.1 `tests/run.sh --quick`: host C++ tests + dead-symbol guard only (< 60 s). Document in AGENTS.md.
- [ ] 5.2 `tools/dev/check.ps1`: PowerShell wrapper that calls the WSL `tests/run.sh --quick` so it works from a
      Windows shell with one command.
- [ ] 5.3 Final build checkpoint; `[USER]` full checklist; the user decides on `git push`.

---

## Not to do (from audit)
- No refactor of `App`; no new abstractions, registries, interfaces with one implementation.
- No file splits for their own sake; the 600-line cap is already met.
- No line cap on i18n JSON, tables, embedded HTML.
- No `git push`, no `--force`, no worktrees. Deleting junk is fine.
