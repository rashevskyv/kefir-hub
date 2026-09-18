Поточний delivery — **v0.13.857 accepted**: безпечний import явно вибраної SD backup-папки через owned staged ZIP. Попередній baseline **v0.13.856**, HEAD 0d2c9df4e0c6ab3df6d6ae343303687c0caee24e.

## Поточний delivery: v0.13.857 — безпечний import папки backup

Accepted 2026-09-18 after Gemini13:51 final test rework. Senior reviewed complete product diff, declarations/callers, owned stage/native reservation/checked finalization and destructive boundary; final connected model reopens actual ZIP at shared boundary and checks selected versus actual live identity/sizes. Gemini reports 11 compiler-free Python suites, en/uk JSON/new-key parity and diff check PASS. Senior ran diff check, did not rerun suites or compile. Tests use real folders/ZIP/artifacts plus simplified models; do not execute C++/libnx/IPC/hardware and are not an exhaustive runtime proof. Existing wire suite covers other supported layouts; new admission helper is85-only.

Shipped: one highlighted native SD directory action; explicit existing target picker; bounded checked EOF traversal/path joins/reads/empty files+dirs; private owned streamed ZIP with checked entry/final close/flush/sync/SD commits; full stage CRC/inventory/shared metadata admission then existing shared restore/recovery. Full-empty staged source uses explicit folder-only opt-in; ordinary ZIP defaults remain strict. Published destination recovery retained, stage cleanup exact/nonrecursive, source/foreign untouched. No source identity or sizing overrides selected destination. User-authorized background TegraExplorer ROMFS update and AGENTS preference included without binary validation. App .856 -> .857. No configure/compile/WSL/NRO/compiled tests/tests/run.sh/push. Separate user compile and disposable Switch checks required. No snapshot/atomic restore/rollback/capacity-fit/journal-fit claims; growth/create/RAW/global scanner and unrelated audit queue remain open.

Review2 Gemini11:55: canonical component validation/checked full joins/all-file checked EOF and known finalize Results corrected. NOT ACCEPTED: reservation fsFsCreateDirectory(sd_fs.Get(), ...) calls nonexistent FsNativeSd::Get (use existing m_fs native member). Empty acceptance unconditionally enabled in existing RestoreSaveZip routes and generic TransferUnzipAll zero-entry default changed; requires folder-only opt-in with old defaults preserved. Real tree/ZIP fixtures now exist, but fault model remains booleans disconnected from artifact operations, misses required fault gates/selected-remap/full admission events. User confirmed background assets/romfs/tegra/TegraExplorer.bin update is authorized ROMFS maintenance, never an out-of-scope stop; preference persisted in AGENTS.md. No senior product edits/build/Python rerun/commit.

Review1 2026-09-18 Gemini10:55: NOT ACCEPTED. Product changes route folder to shared ZIP restore, but source path only checks absolute/colon/control/backslash, not dot components; ancestry can be bypassed with /other/../dumps/save-import. AppendPath truncates silently and adapter checks only relative length, not complete source+relative length. Trailing read error ignored and absent for zero-size file. Result_ZipClose has no project definition. Empty ZIP rejected by existing shared preflight, contrary to planned full-empty tree support. New suite contains no real folders/ZIPs, filters invented save_meta.json/sphaira_meta.json/title.txt and disconnected cleanup booleans; no lifecycle/fault/recovery evidence. No senior Python rerun/compile; git diff --check PASS. Targeted rework back to NEW Gemini; no product edits/commit by senior. Full-empty support needs explicit bounded shared empty-archive handling preserving default admission, or concrete blocker returned before acceptance; no synthetic marker metadata.

Senior preparation 2026-09-18: primary D:/git/dev/sphaira, master, empty porcelain, exact HEAD and baseline ancestry verified before planning. Canonical root Graphify local AST update completed: 12893 nodes / 25689 edges / 645 communities; 8 partial-parser warnings. Query then exact source/caller review. Target chat: Sphaira v0.13.857: безпечний import папки backup. Product code only Gemini manually through user; no other coding agents, no senior product edits. Both roles: NO configure/compile/cmake build/WSL ReleaseWithInstall/NRO/g++/compiled tests/tests/run.sh/binaries/push.

Evidence: filebrowser_ops.cpp FsView::RestoreSaveFile already enumerates exact existing slots and confirms destination, then calls shared RestoreSaveZip. No folder path accepted. Generic ZipFiles omits empty directories, ignores get_collections and scope-exit ZIP close errors; not suitable. get_collections uses native Dir::ReadAll single read; adapter must use bounded Dir::Read until checked EOF, validating counts/kinds/names before joining paths. Do not change generic CRUD/ZIP/traversal. Upstream JKSV rewrite source/tasks/backup.cpp create_new_backup_local writes root .nx_save_meta.bin then copies DEFAULT_SAVE_ROOT contents into selected path; include/fs/SaveMetaData.hpp confirms name. Checkpoint master switch/source/io.cpp backup copies save:/ directly into dstPath, no producer metadata; title.cpp refreshDirectories lists immediate backup subfolders/custom roots. Names are presentation, never destination authority.

Fixed architecture: one File Browser action for one highlighted directory on FsType::Sd, no multiselect/scanner. Folder-aware branch reuses current non-RAW target enumeration/confirmation/results, always explicit existing slot picker with nonzero save_data_id, no folder-name inference or create. Worker adapter in save_menu_ops.cpp owns independent FsNativeSd and canonical absolute source path; only native SD namespace, never stdio/network/UMS/archive/save mounts. Strict bounded component/path validation before every join, exact source-relative paths unchanged, no sanitize/remap loss, invalid mappings/kind/duplicate/parent conflicts fail. Full checked recursive inventory/readable payload, empty files and explicit dirs, including fully empty tree; cancel during scan/read/finalization/admission. Inventory checked against finished stage; same-size concurrent source changes are not snapshot isolation. Preserve hidden payload and nested metadata names. Copy existing reserved root metadata verbatim, no synthesized destination/source metadata; v856 ReadArchiveSaveMetadata remains the decoder/filter authority.

Stage in collision-reserved owned directory under /dumps/save-import; reject source equal/ancestor/descendant of staging parent before creating anything, including root, with case-insensitive component boundaries. Never include generated stage or source==live mount. Reuse checked RecoveryStreamContext callbacks in same translation unit for private folder writer; do not feed arbitrary folder into WriteSaveBackupZip with fake Entry/extra. Bounded exact-size native reads including short-read handling/zero progress/overread, checked current GetSize and trailing EOF, checked ZIP writes/entry close/archive finalize/fflush/fsync/fclose/SD commits. Reopen with SaveReaderContext; full CRC/path/payload inventory preflight + shared metadata admission + compare staged payload inventory to captured source inventory (only existing reserved-root filter), checked reader close. Only then stage usable; call existing RestoreSaveZip for complete admission, mandatory destination recovery, clear/copy/commit/fresh-RO/source-close verification. No second destructive backend. Private stage cleanup deletes only exact owned files/nonrecursive empty owned directory, never foreign/source/recovery. Cleanup errors surface honestly while preserving recovery path/mutation status. Published recovery retained on success/failure/cancel. No automatic rollback/atomic restore/capacity fit/snapshot guarantee.

Gemini scope: sphaira/include/ui/menus/filebrowser.hpp, sphaira/source/ui/menus/filebrowser/filebrowser_ops.cpp and filebrowser_options.cpp; sphaira/include/ui/menus/save_menu.hpp and sphaira/source/ui/menus/save/save_menu_ops.cpp; assets/romfs/i18n/en.json + uk.json minimum parity; tests/test_save_folder_import_contract.py and only necessary old anchors; sphaira/CMakeLists.txt .856 -> .857. Existing save_paths decoder/transfer primitives reused unchanged unless senior-reviewed blocker. No plan/task/walkthrough/audit edits or commit by Gemini.

Acceptance: real temporary folder trees -> real ZIP fixtures, nested/empty dirs/files/full-empty, metadata-free Checkpoint, supported independently encoded JKSV metadata/remap, hidden/nested same-basename payload; hostile components/too-long paths/kinds/collisions/non-SD/staging ancestry; source read/size/truncation/ZIP write/entry-final-close/flush/sync/commit/admission/no-space/cancel faults all before live mutation; connected stage-ready -> shared admission -> exact selected live -> destination recovery -> clear -> serial copy -> fresh RO -> source close -> owned cleanup model, retained recovery and foreign artifacts. Preserve all nine prior Python suites. Models/text anchors are not C++/IPC proof. Gemini runs compiler-free Python/JSON/diff checks; senior reviews complete diff/callers/lifetime/mutation/claims, sends product corrections back to Gemini. After acceptance senior updates all four docs and focused v0.13.857 commit, clean+ancestry/no push. Separate user compilation and disposable Switch folder/remap/no-space/cancel/recovery checks required.

## Попередній delivery: v0.13.856 — JKSV ZIP metadata wire compatibility

Accepted v0.13.856, 2026-09-18: JKSV ZIP save metadata compatibility. Gemini reports all nine compiler-free Python suites PASS; senior reviewed source, callers, working-tree diff, declaration order, shared filtering and final connected-model corrections, and ran git diff --check. Senior did not rerun Python suites. Text/token assertions and Python wire/ZIP/lifecycle models do not execute C++/libnx/IPC or prove hardware behavior. No configure/compile/WSL build/NRO/compiled tests/tests/run.sh/binaries/push.

Shared bounded little-endian decoder/archive reader supports proven JKSV85, both revision-1 JKSV86 layouts with divergent-valid ambiguity refusal, unchanged Sphaira legacy128 and DBI extra512. Discovery, DBI matcher, all-target ZIP restore and recovery admission reuse it. Invalid present metadata, duplicate reserved roots, directory/path aliases, source-field conflicts and read/count/CRC/entry-close/traversal/rewind failures reject; owner/callback close errors are checked. Reserved root filtering is case-insensitive; nested same-basename files remain payload; DBI INI remains opaque and drained without an arbitrary size cap. Valid embedded index0 is not replaced by filename hints.

Selected existing destination identity, both Account UID halves, actual space and live sizes remain authoritative, including explicit remap. Preserve full payload preflight, recovery-before-clear/retention, MTP refusal, v855 closed-handle serial cadence and final fresh-RO/source-close verification. Own legacy writer unchanged. Validated create-from-backup, growth/capacity/journal budget, snapshot/atomic restore/rollback and generic ZIP/UMS/RAW remain outside scope; existing zero-ID legacy fallback is not redesigned or advertised.

Final test corrections align connected events open -> preflight -> metadata -> selected-live -> recovery -> clear -> extract -> fresh-RO -> source-close. Pre-mutation faults never reach clear; final close faults report failure after mutation. Existing-target model removes invented new-creation behavior. Separate user compilation and disposable Switch checks on both restore routes remain required: JKSV85/unique86, ambiguous/corrupt/duplicate/conflicting refusal before clear, Account remap, metadata-free DBI and legacy manual recovery.

Senior handoff preparation 2026-09-17: exact primary D:/git/dev/sphaira, master, empty porcelain, HEAD bb5ff82fc75e8cb69004f57d02020e50a5346b8b і ancestry verified before edits. Parent refresh no longer running; installed Graphify update root completed, no topology changes, 8 partial-parser warnings; query reports 12800 nodes. No product edits/builds/coding agents. Target chat: **JKSV ZIP: сумісне читання save metadata**. Gemini writes product code manually through user; senior owns plan/task and later review/four-doc acceptance/commit.

Evidence: upstream JKSV rewrite d39fd80a208b7c802d12edb12f362506a0addb47 include/fs/SaveMetaData.hpp + source/fs/SaveMetaData.cpp + source/tasks/backup.cpp writes packed revision 1, LE magic 0x56534B4A, 86 bytes with trailing space at85. Released 51da0fd/629a0b7/073d8a5 use 85 bytes without space. Historical 9d542b431417cdde5d545945fee58fbfb9981a13 uses 86 bytes with space at41 and subsequent fields shifted +1; cdc69372491951698dd8fddd4a585e46c352f81b moves space to85 without revision change. Issue264 comment2618962807 is a 32-byte pseudo-code/global-comment draft, not legacy128 wire evidence. Old master c094030c52b6ac4cc831d692c514369954c00efd src/fs.cpp does not establish that metadata layout. Support proven named-entry layouts only; no draft/global-comment or early .jksv_save_meta.bin adapter.

Fixed policy: bounded explicit LE decoder -> existing NXSaveMeta canonical source context; retain own legacy128 writer/magic/version and recovery readability. JKSV85/86 common offsets: magic0/u32, revision4/u8, app5/u64, UID13+21/u64, system29/u64, type37/u8, rank38/u8, index39/u16. JKSV85/tail86 owner41/u64 timestamp49/u64 flags57/u32 data61/s64 journal69/s64 commit77/u64, tail86 space85/u8. Middle86 space41/u8 owner42 timestamp50 flags58 data62 journal70 commit78. Legacy128 magic0/u32=0x4A4B5356 version4/u32=1 app8 UID16+24 system32 type40 rank41 index42 owner72 timestamp80 flags88 unk92 data96 journal104 commit112 raw120; attribute padding44..47/unknown48..71 has no proven blanket zero contract and must not drive identity. Exact lengths only; revision1; type0..6/rank0..1/index full u16; data/journal nonnegative signed s64 (zero source hint allowed); Account app nonzero/systemID0/full UID nonzero, System/SystemBcat systemID nonzero, other types app nonzero. Preserve both UID halves, no local-account existence test or arbitrary numeric ID mask/index==0 rule; non-Account UID remains inert source context. Source space (86 only) allow 0,1,2,3,4,100,101, never All/255, never destination routing. Flags/owner/timestamp/commit/raw unknown bits are source-only; do not invent reserved-zero rules. For86 validate both proven layouts, accept unique interpretation or identical decoded semantics, reject different valid interpretations; never choose by plausibility/date/sizes. Validation is admission policy, not forensic producer attribution.

One minimal shared archive metadata reader + decoder in save_paths.hpp/cpp reused by InspectBackupArchive, DbiBackupMatchesEntry and RestoreSaveZip for ALL targets. Iterate normalized root entries (existing save-only DBI slash policy), exact bounded filenames/no embedded NUL/path aliases, consistent case-insensitive reserved-root recognition, reject reserved metadata directory/kind aliases and duplicates even identical. NX metadata + DBI raw extra may coexist only if all common source fields agree; invalid/unknown/truncated/oversized/read/CRC/close/traversal/rewind errors fail closed, no filename fallback for present invalid metadata. Valid embedded identity is complete: a legitimate zero index/system ID must not be filled from conflicting filename/path hints; filename timestamp presentation may retain existing precedence. Metadata absent permits existing filename/path discovery and metadata-free DBI restore. DBI raw extra remains own 512-byte format; semantic validation/common-field comparison only, uninitialized padding ignored. .dbi_save_info.ini stays excluded opaque content with CRC, duplicate reserved names rejected, no new INI parser. Metadata reads loop through short reads/EOF, bounded stack buffer, full drain/count/CRC/checked entry close, checked iteration termination/rewind; borrowed archive ownership retained, errors propagated. Discovery checks metadata CRC; it does not claim full payload validation. Restore still full payload preflight before mutation. Recovery admission also validates legacy metadata through shared reader before publication/clear; filter shared across preflight/extract/verifier.

All existing selected-target identity/actual space/byte lower-bound/no-growth/recovery retention/MTP/cadence/closed-handle/fresh-RO/source-close guarantees stay. Source metadata never overrides existing selected target, including explicit Account remap. No expansion of zero-ID create support, folder restore, generic ZIP/UMS/RAW/global CRUD/MTP/shutdown/binaries/dependencies/cleanup. Existing zero-ID legacy fallback is not redesigned or advertised.

Gemini scope: save_paths.hpp/cpp, save_menu_ops.cpp metadata consumers/filter; minimal consumer adjustment only if needed; one tests/test_save_metadata_wire_contract.py plus narrowly necessary old text-anchor changes; sphaira/CMakeLists.txt .855 -> .856. No plan/task/walkthrough/audit edits or commit/push by Gemini. Hard ban for both roles: no configure/compile/cmake build/NRO/WSL ReleaseWithInstall/g++/compiled tests/tests/run.sh/test-build. Gemini runs focused Python/source/real-ZIP model + relevant eight existing Python checks and git diff --check; source/model is not C++/IPC proof. Fixtures independently encoded upstream85/tail86/middle86/ambiguous86 + legacy128, corrupt/truncated/oversized/unknown header/type/rank/space/signed/UID/index boundaries, identical/conflicting duplicates, NX+DBI disagreement, metadata-free DBI, selected remap, recovery legacy roundtrip, both shared restore routes. Senior reviews all callers/borrowed lifetimes/preflight/mutation boundaries and evidence; substantive fixes back to Gemini. After accept update four docs, focused v0.13.856 commit, ancestor+clean verification; no push. Separate user compile and disposable Switch both routes/remap/manual recovery/corrupt metadata checks required.

## Попередній delivery: v0.13.855 — serial ZIP commit lifecycle

Accepted v0.13.855: serial ZIP commit lifecycle. Gemini implementation/follow-up reviewed; user explicitly authorized senior completion of connected regression fixtures and unconditional native ownership invalidation. Baseline e5fe804ed59718a48abaf4e4dbdcfca26de3129e, primary D:/git/dev/sphaira/master; clean verified before planning.

Checked native save copy uses serial bounded ZIP read, exact-offset write, checked flush, explicit native close/invalidation, checked commit for every successful chunk, progress after commit and Write-only reopen. Cancellation gates before create/after metadata commit/before read/before write/after commit; empty files supported. Positive selected actual-space live declared journal caps requests at min(512 KiB, journal, remaining); zero retains cadence, negative fails before extraction metadata. No source hints for existing target.

Directory primitives and metadata commits are separate, component-wise, with existing-kind validation and no recursive retry masking. Full-size native file create option 0 commits before payload; unexpected file fails without SetSize. Clear-boundary/final commits checked. Generic unzip/UMS/RAW/global CRUD unchanged. All v854 preflight/CRC/inventory/selected identity/lower-bound/recovery-before-clear/MTP admission/mutation UI/final fresh RO exact inventory-size-bytes/source-close guarantees preserved. Published recovery retained on success/failure/cancel.

Senior actually ran eight compiler-free Python suites PASS plus git diff --check PASS. Connected regression now clears old data, injects post-clear conflicts, tests shared read-result bounds and actual progress, models final mount/verify/source-close refusals and operation-hook cancellation during commit/reopen. Python/text models do not execute C++/IPC/hardware. Expected duplicate ZIP warnings are fixture inputs. No compile/configure/WSL build/NRO/compiled tests/tests/run.sh/binaries/push.

Ceiling: declared-size payload cap is not actual free journal. Allocation/metadata/block overhead unmeasured; even one primitive may exhaust journal; tiny caps may be slow. Quantitative budget/sizing/growth/isolation remain queued. No no-exhaustion/capacity/atomic/rollback/snapshot/power-loss guarantee. Separate test-build and disposable Switch both routes/>journal file/zero-tiny/empty/implicit dirs/fault/cancel/manual recovery/fresh RO checks required.

## Попередній delivery: v0.13.854 — fresh-remount ZIP verification

База ac6675c3b0b7704def169d574f050c3b5a7c40a3 (v0.13.853). Primary D:/git/dev/sphaira, master, clean/base ancestry перевірено до delivery; expected dirty paths належать цьому delivery. Gemini implementation reviewed; після explicit user authorization senior завершив bounded corrections напряму. Graphify root refreshed: 12766 nodes / 25401 edges / 642 communities, 8 partial-parser warnings.

1. Opt-in preflight inventory використовує shared resolved/sanitized/mapped destination pipeline. Exact files/sizes і explicit+implicit directories; duplicate/alias/kind/parent conflicts відхиляються до mutation. Inventory публікується після full CRC та checked rewind; summary semantics/default callers збережено.
2. Shared recovery/post-restore verifier порівнює exact native inventory та streaming bytes у bounded buffers. Empty files/directories, implicit parents і filtered root metadata враховано; усі source entries drain/CRC-check, iteration termination/rewind/reader callback errors checked. Published recovery retention і v853 admission збережено.
3. Shared restore opt-in checked native copy: transfer join, checked flush навіть empty file, explicit close+handle invalidation, checked per-file commit/ZIP entry close та final commit. Writable lexical scope завершується до selected actual-space identity reread і нового full-attr RO mount. Success лише після exact inventory/size/bytes verification, RO scope exit та checked source close. Global File::Close/CRUD semantics незмінні; native void close не дає durability Result.
4. Both UI routes/picked/batch повідомляють до-mutation failure або possible changed/unverified target; published recovery paths retained. Mixed ZIP/RAW batch failure має neutral retained-path prefix; RAW не отримує ZIP mutation semantics.
5. Senior фактично виконав сім compiler-free Python checks PASS, en/uk JSON/key parity PASS і git diff --check PASS. New regression: real ZIP policy cases, 16 injected fault model gates та source lifecycle gates. Python/source/model не доказ C++ threads/IPC/runtime. Legacy C++ contract text-adjusted, не compiled. App 0.13.853 -> 0.13.854, four docs updated для focused primary-master commit; no push.

Remaining: separate test-build і disposable Switch checks обома routes/remap/empty/implicit dirs/large transfers/failure/cancel/manual recovery. Journal allocation/metadata/zero-small/mid-file budget, proven sizing/growth та isolation залишаються queued. No automatic rollback/atomic batch/snapshot exclusion/power-loss durability guarantee. No compiler/configure/build/WSL/NRO/compiled tests/tests/run.sh/binaries run.

## Попередній delivery: v0.13.853 — verified recovery admission

База e7942e899bdef25bd886c03d5489ee1ce6a1afcb. Primary/master/clean/base ancestry перевірено до planning, root/master/ancestry повторено перед acceptance. Graphify root update: 12690 nodes / 25254 edges / 652 communities, 7 parser warnings. App 0.13.852 -> 0.13.853; four docs updated для focused commit.

Shared RestoreSaveZip existing selected target після full source ZIP/live identity/byte admission створює mandatory local SD NX recovery саме destination, включно explicit remap. Reuse shared backup writer, streaming SD, checked entry/final ZIP close і scoped fflush/fsync/fclose/commits; invalid fd fail-closed. Empty saves/files/directories підтримано.
Reopen/full CRC preflight, exact inventory/streaming bytes comparison та sized re-enumeration перед clear; checked sums/counts. Owned collision-safe /dumps/recovery/<timestamp>_<save-id>_<counter>/, native rename окремо від commit, three ownership states; foreign final не видаляється. Published archive/path retained after failure/cancel і both UI routes повідомляють manual recovery File Browser -> recovery.zip -> confirmed existing slot. Cleanup best-effort при failing SD; incomplete artifact не advertised.
Shared ZIP MTP refusal/upfront game-close/MTP-off notice; toggle лише optional RAW backup. Batch sequential per-target, earlier completed restores не rollback. Held mount не snapshot isolation; no atomic restore/batch/automatic rollback/power-loss/capacity-fit guarantee. RAW/growth/create/adapters/MTP redesign/shutdown/binaries поза scope.

Gemini 09-17 16:18 прийнято senior source/caller/diff review. Senior за explicit user authorization додав test-only malformed inventory/close-failure/retention fixtures та фактично виконав шість compiler-free Python checks PASS, en/uk JSON PASS і git diff --check PASS. Model/text не C++/IPC/runtime proof; compile/link/NRO/compiled tests/hardware НЕ запускались. Recovery 17 groups із expanded malformed inventories/failure/retention cases; duplicate-name warnings очікувані fixtures. Legacy C++ contract лише text-adjusted, не compiled.

Next: separate test-build, Switch fsync support і disposable-save checks обома routes/remap/empty/no-space/flush refusal/collision/manual recovery. Journal queued окремо: actual budget/metadata/zero-small handling, checked write-flush-close-commit-reopen cadence, all CRUD/unzip callers/device verification.

## Попередній delivery: v0.13.852 — existing-save capacity admission

Статус: реалізовано Gemini, follow-up 09-16 19:35 прийнято senior full source/caller/diff review. App 0.13.852; база cda36d782973de5ea344738b55f2ebcb711fe561, exact primary/master/clean/base ancestry перевірено перед planning edits. Graphify update: 12670 nodes / 25221 edges / 637 communities, 7 partial-parser warnings. Gemini повідомив PASS п'яти Python checks: capacity (12 model groups), summary (5 source/15 archive/3 arithmetic), selected-target, discovery, MTP. Senior тести не запускав; git diff --check пройшов. Compile/link/NRO/runtime/hardware не перевірено. Text anchors/model не C++ control-flow/IPC/mount-lifetime proof.
1. Shared RestoreSaveZip existing selected slot only: зберегти full ZIP payload/CRC preflight, checked actual-space extra-data read за selected nonzero ID; порівняти application/system ID, full UID, type/index/rank з selected attr. Read failure/mismatch/invalid live sizes fail до clear; data_size > 0, journal_size >= 0 (zero не є divisor).
2. Видалити обидва existing extend guesses та existing-slot metadata size authority. No growth/shrink/rounding/conversion/sum; no other-space fallback. Zero-ID legacy create branch не redesign, caller guards збережено.
3. Перед writable restore mount/clear відмовити, якщо checked summary.file_bytes > live.data_size. Це лише rejection lower bound, не capacity oracle; equality/less не гарантують allocation/implicit-dir/metadata/journal fit. Source metadata hints не використовуються для existing sizing.
4. Завершено: compiler-free Python source/model regression, targeted legacy C++ text-contract adjustment без compilation та app bump 0.13.851 -> 0.13.852. Історичні exact-version assertions прибрано з capacity/summary checks; substantive assertions збережено. Senior acceptance і чотири delivery docs завершено для focused commit. No compiler/build/NRO/WSL/compiled tests/tests/run.sh/binaries.
5. Remaining queue: proven required capacity/alignment або explicit validated sizing policy; growth-only checked actual-space extend з closed probes та mandatory readback, journal-aware copy/remount/readback. No rollback/transaction guarantee. HOS >=3 IPC57/permission support не гарантується, read failure fail-closed.

## Попередній delivery: v0.13.851 — shutdown lifecycle safety

Статус: реалізовано Gemini; follow-up 09-16 16:48 прийнято senior source/diff review. App 0.13.851. Shared install-session mutex серіалізує admission closure/snapshot і publication; cancellation поза mutex до producer joins. Gemini повідомив PASS 7 source/model groups і dead-symbol gate; senior тести не запускав, git diff --check пройшов. Primary master clean на b4770f00 перед task-doc edits. SD config/kefir/log.txt відсутній, errors.txt містить тільки успішний ACC_DIAG; crash/fatal report не знайдено. Причину конкретного збою та traceback не підтверджено. Compile/NRO/runtime/hardware не перевірено; source anchors і sequential Python model не є C++ thread/IPC proof.
1. Gemini: розділити MTP stop для runtime port handback і final shutdown; final App destructor не запускає usbHsFsInitialize після haze stop. Зберегти чинні runtime callers, callbacks suppression, transfer cancellation і join-before-proxy destruction.
2. Gemini: зупинити Web producer threads до звільнення widgets/install state/i18n/GPU. Спершу простежити всі synchronous request/install wait loops та чинний cancel flow; не переносити join перед необхідним cancellation. userAppExit fallback має лишитися idempotent. Не додавати detached threads чи timeout з подальшим знищенням live owners.
3. Gemini: bounded synchronous shutdown begin/end breadcrumbs через існуючий error log, незалежно від normal logging, перед blocking phases, включно з userAppExit. Тільки phase/version/timing/Result, без paths/identities/secrets. Не заявляти відновлений stack trace без crash addresses.
4. Один compiler-free regression для final-vs-runtime MTP behavior, producer-before-owner teardown та begin/end diagnostics; model/source checks не доводять C++ runtime. No build/compiler/tests/run.sh; користувач компілює окремо.
5. Gemini app bump 0.13.850 -> 0.13.851 завершено; senior review, acceptance і усі чотири delivery docs завершено для focused commit. Saves sizing/growth та інші queued tasks поза scope. Наступне: користувач компілює та перевіряє SELECT/HOME, MTP idle/transfer/disconnect, Web idle/upload/direct install і USB flash; errors.txt має begin/end фаз.

## Попередній delivery: v0.13.850 — validated ZIP payload accounting

Статус: реалізовано Gemini та прийнято після test-only follow-up і senior source/diff review; app 0.13.850. Gemini повідомив PASS summary (6 source groups, 15 model fixture groups, 3 defensive arithmetic groups), попередні P2-B/P2-A/MTP Python checks і git diff --check. Senior перевірив source/diff та git diff --check, тести не запускав. Compile/NRO/runtime/hardware не перевірено. Summary — checked payload accounting, не capacity estimator. Text anchors не AST/control-flow proof; path model спрощений і не виконує C++/IPC. Host-native path tests у цьому delivery не запускались.
1. Додати optional output summary до обох TransferUnzipPreflight overloads. Локальний summary публікувати тільки після повної перевірки всіх entries і успішного rewind; failure/cancel лишає caller output незмінним. Payload classification відповідає extraction після normalization/sanitizer/filter/mapping. Metadata drain/CRC збережено, але skipped entries не входять у payload totals.
2. Checked s64 conversion/addition/count increments та bytes_drained accumulation; overflow fail до mutation навіть без requested output. Directory bytes не є file payload; zero-byte files рахуються. Summary explicit directories не рахує implicit parent directories, allocation blocks або filesystem metadata.
3. RestoreSaveZip отримує summary в існуючому preflight call; тільки counts/bytes diagnostic без names/UID/secrets. Не використовувати summary як required data size, journal, alignment або automatic growth authority. Save Menu/File Browser shared owner та generic unzip semantics збережено.
4. Full sizing/growth delivery відкладено: fsExtendSaveDataFileSystem і actual-space extra read — libnx HOS >=3.0.0 IPC 32/57; extra.data_size usable, FsSaveDataInfo.size raw image. Wrapper не визначає filesystem overhead/alignment; JKSV NACP journal policy і Checkpoint cluster/headroom heuristic не доводять гарантовану capacity. Джерела: https://github.com/switchbrew/libnx/blob/master/nx/source/services/fs.c ; https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/fs.h ; https://github.com/J-D-K/JKSV/blob/master/src/fs.cpp ; https://github.com/FlagBrew/Checkpoint/blob/master/switch/source/io.cpp .
5. Відомі unsafe existing extend paths (source metadata authority, unchecked fallback sum, total + remainder rounding, unchecked live read/no readback) не виправлено цим prerequisite. Перед наступним sizing handoff потрібні checked live sizes/identity, підтверджені allocation rules або окремий prompted/fail-closed sizing flow, growth-only data/journal, closed probes, checked extend і reread/open actual space; no other-space fallback. Python model не замінює C++ IPC. Transactional rollback/journal-aware copy/remount verification окремо.
6. Gemini narrow source/header/RestoreSaveZip preflight wiring, Python source/model check та app bump 0.13.849 -> 0.13.850 завершено. Senior acceptance і чотири delivery docs завершено для focused commit. Compiler/cmake/build/WSL ReleaseWithInstall/NRO/g++/compiled tests/tests/run.sh не запускались. Create-from-backup, adapters/format writer, RAW, MTP, scanner, binaries/refactor/deps поза scope. Наступна verification — окремий test-build, потім disposable-save console checks.

## Попередній delivery: v0.13.849 — P2-B selected restore target safety

Статус: реалізовано Gemini та прийнято після follow-up senior source/diff review; app 0.13.849. Premature move-capture виправлено shared ownership seeds/accounts; batch count і всі live/nonzero targets перевіряються до worker/auto-backup. Gemini повідомив PASS P2-B/P2-A/MTP Python checks; senior тести не запускав. Compile/NRO/runtime/hardware не перевірено; binaries не змінено.
1. Найменший slice: backup restore вибирає підтверджений existing live slot через чинний PopupList; no-match/cancel/ambiguous без явного вибору не мутують save і не переходять до guessed create.
2. Source backup context відокремити від selected target FsSaveDataInfo. Full UID/type/index/rank/space target походять виключно від discovery; metadata/path hints лише shortlist/context, не overwrite. Account remap лише через явний user/slot вибір.
3. Preserve direct nonbackup live-seed exact path; batch спершу resolve/select всі targets, потім restore. Unknown backup fields не використовувати як zero/default exact constraints. Shared RestoreSaveZip owner/P0/P2-A збережено.
4. Create-from-backup, checked growth/alignment sizing, DBI/NX precedence reconciliation, legacy 128-byte і packed JKSV wire adapters залишаються queued P2-B/P3. Compatibility не заявляти за filename; цей slice не приймає metadata як нову create authority.
5. RAW format/coverage, MTP coverage/permissions/CloseFile, Game Details і bundled binaries поза scope. No compiler/build/NRO/compiled tests/tests/run.sh; Gemini static contract/synthetic checks і bump 0.13.849 виконано, senior review/docs завершено для focused commit. Python lifetime fixture є моделлю sequential callbacks, не C++ asynchronous execution.

## Попередній delivery: v0.13.848 — P2-A exact discovery / actual space

Статус: реалізовано Gemini та прийнято після повторного senior source/diff review; версія 0.13.848. Empty SetIndex guard повертається до indexed entry access. Gemini повідомив passing compiler-free P2-A/MTP/dead-symbol checks; senior перевірив source і git diff --check, тести не запускав. UI empty-state не має окремого anchored regression у повернутому check; runtime/build/hardware не перевірено. Оновлений bundled TegraExplorer.bin включено за прямою вказівкою користувача, без binary/runtime validation.
1. Один shared unfiltered per-space reader у save_paths.cpp/.hpp; explicit libnx spaces System/User/SdSystem/Temporary/SdUser/ProperSystem/SafeMode — probes, не гарантія доступності. Filter returned records за type/full UID; retain returned space/rank; reuse SaveEntryKey. Open/read errors явно log без identity/secrets; failed reads не публікують частковий space.
2. Menu::ReadSaveEntries і ListAccountSaves та File Browser ZIP target discovery reuse helper; non-account scan не залежить від accounts. Preserve title-grouped tiles, але actual action slots дедуплікуються тільки SaveEntryKey.
3. FsNativeSave system-ID RW open передає caller actual space. Existing live operations не hardcode System і не трактують space=0 як unknown. Existing selected ZIP target з nonzero save ID відкривається exact-space і fail-closed; new-save/create policy та metadata redesign не змінюються.
4. File Browser filename ID auto-target лише за одним exact matching slot, ambiguous → existing picker з slot labels; RAW target coverage не розширюється. ResolveRestoreTarget backup/new-target redesign окремо; live nonbackup seed не переобирає інший rank/space.
5. MTP scanner/coverage/layout/pinned export без змін; Game Details discovery лишається окремим caller follow-up. P2-B/P3/P4, RAW writes, void CloseFile та stale RO settings поза scope.
6. Gemini static-only checks, bump 0.13.848; жодного compiler/build/tests/run.sh. Senior після diff review оновлює всі delivery docs і commit. Firmware/permission/runtime/hardware guarantees не заявляти.

## Попередній delivery: v0.13.847 — MTP сейви: гра → користувач

Статус: реалізовано Gemini та прийнято після повторного senior diff review; app version 0.13.847. Records сортуються до name allocation; save-only game formatter резервує prefix/suffix space. Gemini повідомив passing Python contract/model (7 source groups, 5 behavioral groups) і dead-symbol gate; senior перевірив source/diff та `git diff --check`, але тести не запускав. Python model не виконує C++ і не повністю відтворює shared sanitizer. Compile/runtime/hardware verification відкладено.
1. Reuse `FsSaveProxy`: game display name зі stable Title-ID suffix → account nickname без `Account` → безпосередні live save root contents. Не додавати backend, ZIP або backup layer.
2. Reuse UTF-8 sanitizer і trimming; локально закрити reserved Windows names, byte limits і case-insensitive collisions. Повний UID лишається внутрішнім; visible collision/fallback suffix використовує non-secret save ID, не UID prefix. Не втрачати записи через unchecked `emplace`.
3. Зберегти Account/User, BCAT/User, Device/User і Cache/SdUser scan та mount semantics. Non-account buckets лишаються BCAT/Device/Cache + index; account nickname, що збігається з bucket, disambiguate. System/SystemBcat/Temporary не додавати.
4. Зберегти RW-open → RO fallback, CRUD Result/Commit propagation, shared handle lifetime, LRU=4 та single-thread transfer. Known risks: `CloseFile` не повертає commit Result; root space fallback синтетичний; settings описує Saves як read-only. Цей delivery їх не виправляє.
5. Preserve working pinned MTP export SD/content/archive (`MountCurrentOverMtp` → `MountFs` → `Init` → `MakeFsProxy`). Save-specific restoration не включати; P2 exact-space/create/metadata, P3 adapters і P4 RAW поза scope.
6. Gemini bump 0.13.846 → 0.13.847 виконано. Senior acceptance і delivery docs завершено для focused commit; builds/compiled tests не запускались. Switch/Windows MTP verification — окремий наступний workflow.

## Попередній delivery: v0.13.846 — Host test blockers

Статус: реалізовано; compile verification відкладено за workspace policy.
1. `TraverseGrid` відкидає елементи з `x < min_x` до наявної right-bound перевірки, зберігаючи повне горизонтальне входження й не повертаючи clipped focused item наприкінці.
2. Додати один вузький host regression для першої clipped колонки та наступної видимої колонки.
3. `check_dead_symbols.py` розпізнає багаторядкові function definitions лише до `;`, `{` або `}` і лише з `{` після `)`; line detector лишається fallback.
4. Вбудований Python self-check покриває справжнє multiline definition і phantom declaration; `python tests/check_dead_symbols.py` та `git diff --check` проходять.
5. Не змінювати save restore product files. `test_list_draw_order` і повний `tests/run.sh` виконати окремим test-build workflow.

## Попередній delivery: v0.13.845 — P1 shared ZIP save restore

Статус: реалізовано та успішно зібрано.
1. Винести одну namespace-level реалізацію ZIP save restore з поточного `Menu::RestoreSaveInternal`: metadata, target create/extend, P0 preflight, clear, extraction і final commit.
2. Save Menu wrapper зберігає title/image та RAW DISA branch; ZIP делегує shared function. File Browser зберігає picker/confirmation/notifications та RAW branch; дубльований ZIP lifecycle замінює одним shared call.
3. Не додавати backend class/interface/new file без потреби; наявний `save_menu_ops.cpp` може бути єдиним owner, а `save_menu.hpp` — декларацією.
4. Зберегти P0 fail-closed, DBI save-only compatibility, Result propagation та ordinary/UMS unzip semantics без змін.
5. Оновити мінімальний contract test так, щоб обидва UI routes викликали shared function, а критичні lifecycle operations існували лише в одному owner.
6. `BackupSaveInternal` не перенесено: File Browser не має другого save-aware backup backend, лише generic `ZipFiles`; решту P1 backup scope залишено в черзі. Focused contract (38), path regression (364) і post-edit WSL `ReleaseWithInstall` пройшли до `[100%] Built target sphaira_nro`.

## Попередній delivery: v0.13.844 — Emergency P0 safe save restore

Статус: реалізовано та успішно зібрано.
1. Залишити глобальний `path::IsSafeArchiveEntry` суворим; дозволити рівно один leading `/` лише в save-import compatibility layer, після чого повторно перевірити нормалізований relative path і destination mapping.
2. До першої мутації target save повністю пройти ZIP: metadata/name/size, нормалізація, mapping, open/read/CRC/close кожного payload entry; пропуск metadata не скасовує перевірку читабельності.
3. Лише після успішного preflight виконувати create/extend/delete/extract; усі `Result` від extend, CRUD, write/resize та фінального `Commit()` мають доходити до UI.
4. Save Menu і дубльований File Browser ZIP restore повинні використовувати однакові save-only normalization/preflight guarantees; File Browser зобов'язаний завершуватися успішним `save_fs.Commit()`.
5. Додати один host regression/contract check для DBI leading slash, traversal/absolute rejection і порядку preflight-before-mutation; не змінювати RAW restore та не починати P1 shared-backend refactor.
6. Gemini підняв patch до `0.13.844`: path/save contract checks пройшли, WSL `ReleaseWithInstall` завершився `[100%] Built target sphaira_nro`; повний `tests/run.sh` дійшов до двох попередніх unrelated failures (`min_x`, dead `PromptBatchRestoreAccountTargets`). Senior перевірив diff/callers і UMS follow-up.

## Попередній delivery: v0.13.843 — Restore-first actions and account-filtered backups

Статус: реалізовано та успішно зібрано.
1. Backup actions починаються з Restore і завжди завершуються Delete.
2. Вимкнений All Accounts допускає лише account-bound backups із UID enabled local accounts; non-account saves не змінено.
3. Multi-select foreign/unknown backups збирає local target UID послідовними popup-діалогами; cancel зупиняє batch до restore.
4. Повторно використано ResolveRestoreTarget і наявний RestoreSaves без нового restore backend.
5. Gemini WSL ReleaseWithInstall завершився ціллю [100%] Built target sphaira_nro; JSON validation і git diff --check пройшли.

## Попередній delivery: v0.13.842 — Align backup metadata columns

Статус: реалізовано та успішно зібрано.
1. Secondary metadata backup-рядка розділено на Title ID, account, timestamp і archive count.
2. Фактична NanoVG-ширина кожного поля вимірюється для всіх backup entries; наступні колонки малюються за спільними pixel tab stops.
3. Кастомний рядок перевикористовує точний list text clip width і не перекриває праву info-колонку; live saves та інші layouts не змінено.
4. Gemini WSL `ReleaseWithInstall` завершився до `[100%] Built target sphaira_nro`; senior review і `git diff --check` пройшли. Апаратна перевірка залишається.

## Попередній delivery: v0.13.841 — Polish save metadata and restore

Статус: реалізовано та успішно зібрано.
1. Використати доступну backup metadata як fallback назви для live saves видалених ігор; без зовнішнього icon lookup.
2. У save UI показувати сталий англійський тип `Account`, а unknown backup account — лише коротким hex ID без префікса.
3. Зменшити жовтий backup border у List, не змінюючи великі tile layouts.
4. Закривати probe `FsNativeSave` до повторного RW-open, усуваючи `FsError_TargetLocked` під час restore.
5. WSL `ReleaseWithInstall` успішно завершився ціллю `[100%] Built target sphaira_nro`; `git diff --check` пройшов.

## Попередній delivery: v0.13.840 — Verify save actions ReleaseWithInstall build

Статус: реалізовано та успішно зібрано.
1. Integrity worker перевіряє `.zip` через `std::string_view::ends_with`, не покладаючись на недоступний у translation unit namespace `path`.
2. WSL `ReleaseWithInstall` повторено після виправлення; compile, LTO link, RomFS packing і `sphaira_nro` завершилися успішно до 100%.
3. Зібрано `build/ReleaseWithInstall/kefir-hub.nro`; апаратна перевірка залишається окремим кроком.

## Попередній delivery: v0.13.839 — Finish save actions compilation fixes

Статус: реалізовано; очікується повторна збірка.
1. Локальну змінну `path` у integrity worker перейменовано, щоб вона не затіняла namespace `path`.
2. Delete-backups worker захоплює `this` для виклику `CollectGroupArchives`.
3. Усі решта `save_data_space_id` conditionals приведені до одного enum-типу без `-Wextra` warning; `git diff --check` пройшов.

## Попередній delivery: v0.13.838 — Fix save actions GCC build

Статус: реалізовано; очікується повторна збірка.
1. Тернарний вираз `save_data_space_id` тепер має однаковий enum-тип в усіх гілках і не створює `-Wextra` warning.
2. Локальний `FsEntry` у generic lambda більше не є `constexpr`, що обходить ICE GCC у `tsubst_expr` без зміни поведінки.
3. `git diff --check` пройшов; повторну збірку в цьому delivery не запускали.

## Попередній delivery: v0.13.837 — Save backup metadata and actions

Статус: реалізовано; очікується збірка та апаратна перевірка.
1. Backups об'єднуються за application/system ID, типом, повним UID та index; рядок показує назву гри разом із Title ID, автора/UID, тип, час і кількість архівів.
2. ZIP, DBI та сирі `.disa`/`.bin` знаходяться у стандартних і custom roots; metadata/filename/folder/path мають визначений пріоритет, а parent ID не перекривається timestamp-іменем.
3. `A` відкриває контекстні дії для live saves і backups: backup-if-newer, restore із вибором локального користувача, integrity check, prune до найновішого, delete та груповий selection.
4. Відновлення спершу знаходить реальний локальний save target для auto-backup; ZIP може створити відсутній сейв, а raw DISA без наявного target ID безпечно відхиляється.
5. Gemini та senior виконали static flow/diff review; JSON і `git diff --check` перевірено, compile/tests/NRO не запускалися за workspace policy.

## Попередній delivery: v0.13.836 — Keep web sharing awake and fix cancel dialog freeze

Статус: реалізовано; очікується збірка та апаратна перевірка.
1. Активний HTTP web-sharing server блокує внутрішній inactivity blank mode в install session; після зупинки сервера звичайний timeout відновлюється без persistent state.
2. З `App::Update()` прибрано другий виклик `ShowCancelConfirmation()`; detached `ProgressBox` залишається єдиним власником `B` та touch-cancel.
3. Gemini та senior виконали static caller/diff review; compile/tests/NRO не запускалися за workspace policy.

## Попередній delivery: v0.13.835 — Fix transliterated Russian strings and uninstalled save titles

Статус: реалізовано; очікується збірка та апаратна перевірка.
1. У `ru.json` виправлено транслітерацію на кнопці `(+)` («Remove and reboot» -> «Удалить и перезагрузить») та в інших 8 рядках інтерфейсу (чити, теми, дампи, збереження).
2. Виправлено латинські гомогліфи в ключах читів та додано відсутні ключі з v0.13.813 для видалення перекладів з відкладеним ребутом.
3. У `save_menu.cpp` та `save_menu_detail.cpp` забезпечено форматування назв неінстальованих ігор через Title ID та захист від перезапису валідних назв плейсхолдерами.
4. `git diff --check` і валідація JSON пройшли успішно; compile/tests/NRO не запускалися за workspace policy.

## Попередній delivery: v0.13.834 — Clean Backups tab and contextual save actions

## Попередній delivery: v0.13.833 — High-contrast selection marks

Статус: реалізовано; очікується збірка та апаратна перевірка.
1. Tile-layouts використовують збільшений 28 px checkbox замість спільного 20 px list-size.
2. Вибрана плитка отримує легкий `ThemeEntryID_FOCUS` overlay з 25% alpha; checkbox малюється поверх нього.
3. Спільний `gfx::drawCheckbox` використовує контрастний `ThemeEntryID_TEXT` і 3 px border в усіх grid/list callers.
4. `git diff --check` і static review пройшли; compile/tests/NRO не запускалися.

## Попередній delivery: v0.13.832 — Separate Saves popup from tabs

Статус: реалізовано; очікується апаратна перевірка.
1. Перший ряд звичайного Grid у standalone Saves зсунуто на 16 px вниз (`y=186` → `y=202`).
2. Плашка назви вибраної гри тепер починається одразу під панеллю вкладок, не змінюючи колір або спільний `drawAppLable()`.
3. Інші layout-режими та single-game saves не змінені; `git diff --check` пройшов, compile/tests/NRO не запускалися.

## Попередній delivery: v0.13.831 — Fix save layout compilation and verify parallel build

Статус: реалізовано; успішно зібрано через WSL ReleaseWithInstall.
1. У `sphaira/source/ui/menus/save_menu.cpp` кваліфіковано значення переліку `LayoutType` простором імен `grid::` (`grid::LayoutType_List`, `grid::LayoutType_Grid`, `grid::LayoutType_GridDetail`, `grid::LayoutType_HbMenu`) у методі `OnLayoutChange()`.
2. Успішно виконано паралельну багатопотокову збірку (16 потоків, WSL `ReleaseWithInstall`) з успішною лінковкою `kefir-hub.nro`.
3. `git diff --check` чистий, усі компіляційні помилки усунено.

## Попередній delivery: v0.13.830 — Corner multi-select checkboxes

Статус: реалізовано; очікується збірка та апаратна перевірка.
1. У спільному `DrawSelectionMark` для tile-layouts прибрано затемнення всієї іконки та велику центральну галочку.
2. Після початку мультивибору кожна видима плитка показує стандартний checkbox у верхньому лівому куті: порожній для звичайного елемента, з галочкою — для вибраного.
3. Спільна зміна покриває Saves, Games, Homebrew і Users; наявна list-gutter поведінка, зокрема Wi-Fi, не змінена.
4. Gemini та senior перевірили мінімальний diff і всіх callers; `git diff --check` пройшов. Compile/tests/NRO не запускалися.

## Попередній delivery: v0.13.829 — Saves category tabs

Статус: реалізовано; очікується збірка та апаратна перевірка.
1. `Tools -> Game Tools -> Saves` одразу відкриває загальне меню saves на вкладці `Installed Games`, без проміжних трьох іконок.
2. Видима панель містить `Installed Games`, `Deleted Games` і `Backups`; `L/R` та touch перемикають наявну категорію циклічно.
3. Вкладки залишаються видимими для порожньої категорії і не перекривають список у чотирьох layout-режимах.
4. Single-game saves з `app_id_filter` зберігають попередній `Category::All`, геометрію та поведінку без глобальних вкладок.
5. `git diff --check` пройшов; compile/tests/NRO не запускалися за workspace policy.

## Попередній delivery: v0.13.828 — SysNAND forwarder safety and authoritative NAND state

Статус: реалізовано; очікується збірка та апаратна перевірка.
1. `App::IsEmummc()` знову базується на `splGetConfig(65007)` — тому самому сигналі, що раніше формував `|E` / `|S` біля версії Atmosphere.
2. Невдача `splInitialize` або читання 65007 залишає стан `false`, тобто fail-closed SysNAND/unknown.
3. SMC `smcAmsGetEmunandConfig` залишено лише для шляхів і типу emuMMC; він більше не визначає активний NAND.
4. Автоінсталяція Kefir Hub forwarder блокується на будь-якому непідтвердженому EmuNAND і в плані, і безпосередньо перед `InstallKefirHubForwarder`; наявність EmuNAND на SD не дає дозволу на SysNAND.
5. Додано мінімальний host-test для EmuNAND allow, SysNAND deny та збереження safe cleanup; `git diff --check` і `check_dead_symbols.py` пройшли, компіляцію не запускали за policy.

## Попередній delivery: v0.13.827 — Wi-Fi connection reliability fixes

Статус: реалізовано; очікується апаратна перевірка.
1. Життєвий цикл `NifmRequest` централізовано: попередній запит скасовується, terminal result опитується, а handles закриваються після success, failure, timeout чи виходу з меню.
2. Реальні NIFM-помилки підключення виводяться через `PushErrorBox`; success/timeout не перезаписуються `Refresh()`.
3. Зміна SSID зберігає всі 32 байти завдяки окремому `ssid_len`, без хибного резерву під NUL.
4. Toggle Wi-Fi та single/batch profile mutations перевіряють `Result`; batch delete показує точні success/failure counts і перший реальний код помилки.
5. Перший Gemini-раунд успішно виконав WSL `ReleaseWithInstall`; після фінальної зміни лише i18n-тексту повторну збірку не запускали; JSON і `git diff --check` пройшли.

## Попередній delivery: v0.13.826 — Wi-Fi management menu under Tools

Статус: реалізовано; очікується користувацька збірка та апаратна перевірка.
1. У `Tools -> Tools -> Wi-Fi` замінено заглушку `ComingSoon` на повноцінне меню керування бездротовими мережами `ui::menu::wifi::Menu`.
2. Реалізовано бекенд `wifi_manager` на основі системного сервісу `nifm` (`nifm:a` / `IGeneralService`): перерахування збережених мереж (`nifmEnumerateNetworkProfiles`), отримання параметрів (`nifmGetNetworkProfile`), оновлення профілів (`nifmSetNetworkProfile`), видалення профілю (IPC cmd 10 на `IGeneralService`), та підключення (`nifmRequestSetNetworkProfileId`).
3. Меню підтримує мультиселект: кнопка `X` перемикає вибір поточного елемента, `Y` виконує інверсію виділення, `B` скидає виділення або виходить із меню.
4. Контекстне меню (`Sidebar` на кнопку `+` / `START`):
   - При кількох виділених мережах: масове видалення (`Delete selected`) з діалогом підтвердження кількості, а також швидкий вибір усіх / зняття виділення, перемикання стану Wi-Fi та оновлення списку.
   - При виборі 1 мережі (або поточної виділеної): підключення (`Connect`), перейменування (`Rename`), зміна пароля через клавіатуру `swkbd` без використання системних налаштувань Horizon (`Change password`), зміна SSID (`Edit SSID`), перегляд пароля у відкритому вигляді та деталей мережі (`View password & details`), а також індивідуальне видалення (`Delete network`).
5. Натискання кнопки `A` на будь-якій мережі у списку викликає діалог підтвердження підключення.
6. Поточна активна мережа автоматично відображається вгорі списку із зеленим індикатором та статусом `Connected`.
7. Додано векторну іконку сигналу Wi-Fi на базі NanoVG та прапорці виділення рядків.
8. Додано ключі локалізації для англійської (`en.json`), української (`uk.json`) та російської (`ru.json`) мов.

## Попередній delivery: v0.13.825 — Reliable firmware cleanup result

Статус: реалізовано; очікується користувацька збірка та апаратна перевірка.
1. `CleanThemesAndTranslations` більше не зберігає проміжну delete-помилку, якщо шлях фактично зник до фінального readback.
2. Failure виставляється лише коли file/directory реально залишається; точний path записується в лог.

## Попередній delivery: v0.13.824 — Informative error dialogs and network gate in updater

Статус: реалізовано; очікується користувацька збірка та апаратна перевірка.
1. Діалог помилок `ErrorBox`: заголовок завжди показує зрозумілий та дружній напис `"An error occurred"_i18n` замість внутрішніх C++ ідентифікаторів енумів (наприклад, `SphairaError_AppstoreFailedZipDownload`).
2. Технічний код помилки (`Code: 0x... (SphairaError_...)`) перенесено під текст повідомлення у ролі другорядного діагностичного напису з кольором `ThemeEntryID_TEXT_INFO`.
3. У `GetErrorDescription(rc)` додано пораду перевірити інтернет-з'єднання (`"Please check your internet connection and try again."_i18n`) для збоїв завантаження.
4. Додано функцію `ShouldShowIssue(rc)`, яка приховує заклик відкривати issue та посилання на Telegram `t.me/xhrxhrxhr` для мережевих збоїв, нестачі місця, блокування файлів та скасувань.
5. У `kefir_menu.cpp` завантаження прошивки та пакетів Kefir захищено викликом `net::RequireConnection` (запобігає запуску завантаження при відсутності мережі).
6. Усі помилки завантаження/інсталяції в `kefir_menu.cpp` та `cheat_download_menu.cpp` переведено на `App::PushErrorBox`, що дозволяє при відсутності зв'язку показувати спокійне інформативне вікно налаштування Wi-Fi.
7. Додано ключ локалізації `"Please check your internet connection and try again."` для всіх 14 мов.

## Попередній delivery: v0.13.823 — Localized firmware update and reboot notifications

Статус: реалізовано; очікується користувацька збірка та апаратна перевірка.
1. Усі тексти діалогів після оновлення та даунгрейду прошивки (`prompt_reboot` у `kefir_menu.cpp`) переведено на систему локалізації `_i18n`: повідомлення про успішне оновлення, повідомлення про видалення тем/перекладів, попередження про неможливість видалення тем/перекладів (з інформацією про помилку Atmosphere 2162-0002), нотатки завершення через TegraExplorer та фінальний запит перезавантаження.
2. Локалізовано повідомлення `DescribeDowngradeFix` у `kefir_firmware.cpp`, запити ручного застосування фіксу даунгрейду, підтвердження встановлення пакету Kefir, стан валідації вмісту прошивки та діалоги помилок перевірки/оновлення.
3. Додано нові ключі локалізації з повними перекладами в `en.json`, `uk.json` та `ru.json`; оновлено `README.md`.

## Попередній delivery: v0.13.822 — Silence error on user creation cancellation

Статус: реалізовано; очікується користувацька збірка та апаратна перевірка.
1. У `ConfirmCreate` (`users_profile.cpp`) додано перевірку кодів скасування аплету створення користувача (`AccountError_Cancelled` `0x7C`, `AccountError_CancelledByUser` `0x27C`). При скасуванні створення користувачем помилка не виводиться, екран повертається до меню без помилкового `ErrorBox`.
2. У `defines.hpp` та `error_box.cpp` зафіксовано модуль `Module_Account` (124) та коди помилок скасування для коректної діагностики та обробки.

## Попередній delivery: v0.13.821 — Readable downgrade warning

Статус: реалізовано; очікується користувацька збірка та апаратна перевірка.
1. Текст про очищення нейтрально повідомляє, що system save, теми та переклади буде видалено, без згадки виконавця.
2. `DowngradeWarningBox` використовує більші шрифти та виміряну висоту wrapped text, щоб контент заповнював діалог без накладання на QR чи кнопки.

## Попередній delivery: v0.13.820 — Downgrade script contract and compact translation layout

Статус: реалізовано; очікується користувацька збірка та апаратна перевірка.
1. Канонічний `DowngradeFix.te` перенесено до TegraExplorer і синхронізовано в Sphaira RomFS; Hub mode бере сувору ціль `emu`/`sys` з `/config/kefir/downgrade_nand`, працює без підтверджень і повертається в Hekate, manual mode зберігає вибір та підтвердження й не чіпає сторонній startup workflow.
2. `ReadRomfsTe` монтує RomFS на вимогу; downgrade preflight і надалі блокує `amssuApplyPreparedUpdate`, якщо скрипт не читається або staging не завершено.
3. Із Translate Interface прибрано firmware/target stats, а повний 66 px spacer замінено на 33 px section gap із синхронною геометрією draw, touch, scroll bounds, visibility та scrollbar.

## Попередній delivery: v0.13.819 — Clean translation UI

Статус: реалізовано; очікується WSL-збірка та апаратна перевірка.
1. З Translate Interface прибрано діагностичний firmware/release item та технічні URL, release/metadata tags, ZIP-імена й внутрішні шляхи з описів, підтверджень і progress-повідомлень.
2. Користувацький flow залишає назву мови, firmware та зрозумілі стани завантаження/розпакування/встановлення; мережева й файлова логіка не змінена.

## Попередній delivery: v0.13.818 — Interface translation separator

Статус: реалізовано; очікується WSL-збірка та апаратна перевірка.
1. Коли кеш мов непорожній, між службовими діями Translate Interface та списком мов додається наявний неінтерактивний `MakeSeparator()` — той самий патерн, що в DBI.

## Попередній delivery: v0.13.817 — Restore translation removal contract

Статус: реалізовано; очікується WSL-збірка та апаратна перевірка.
1. Відновлено перевірений контракт v0.13.230–v0.13.812: `FsError_TargetLocked` під час best-effort видалення системного перекладу не показується як failure.
2. Після підтвердження видалення викликається `RemoveInterfaceTranslationAndReboot`, тому reboot знову є обов'язковою частиною операції; відкладений reboot прибрано.
3. Видалено зайвий TegraExplorer flow v0.13.816, його RomFS-скрипт та i18n. Безпечний download/extract/source validation перед заміною перекладу з v0.13.815 збережено.

## Попередній delivery: v0.13.816 — Offline removal for locked translations (superseded by v0.13.817)

Статус: реалізовано; очікується WSL-збірка та апаратна перевірка.
1. `FsError_TargetLocked` під час видалення системного перекладу тепер розпізнається як штатне блокування файлів процесами Horizon, а не як безвихідна помилка.
2. Після підтвердження Hub перевіряє відсутність іншого `/startup.te` і запускає одноразовий `remove_translation.te` у TegraExplorer.
3. Offline-скрипт видаляє лише 18 системних translation paths, перевіряє readback, не зачіпає теми, DBI чи system saves і повертає консоль у Hekate.

## Попередній delivery: v0.13.815 — Post-806 regression fixes

Статус: реалізовано; WSL ReleaseWithInstall успішно зібрано Gemini, очікується апаратна перевірка.
1. Заміна системного перекладу тепер спершу завантажує, розпаковує й перевіряє новий пакет, а лише потім видаляє старі файли; помилки видалення, включно з `FsError_TargetLocked`, більше не маскуються.
2. Автоматичний downgrade fix проходить staging і перевірку TegraExplorer до `amssuApplyPreparedUpdate()`, але `/startup.te` активується лише після успішного застосування прошивки. Наявний startup workflow не перезаписується; failure paths прибирають лише власні staged artifacts.
3. Очищення несумісних тем/перекладів повертає фактичний статус, а UI показує окреме попередження при failure замість неправдивого success. DBI-переклади залишаються недоторканими.
4. `downgrade_fix.te` підтверджує видалення save `8000000000000073` перед зеленим success; failure повертає користувача в Hekate з червоним попередженням.

## Попередній delivery: v0.13.814 — Preserve DBI translation on firmware updates & downgrades

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. Вилучено видалення файлів перекладу DBI (`/switch/DBI/translation.bin`, `/switch/DBI/translation_new.bin`) зі списку очищення `FIRMWARE_CLEANUP_PATHS` у `kefir_firmware.cpp`. Переклади для DBI є автономними файлами хоумбрю-додатку і не конфліктують із системною прошивкою Horizon OS чи завантаженням Atmosphere.
2. Вилучено команди видалення файлів перекладу DBI зі скрипта TegraExplorer `assets/romfs/tegra/downgrade_fix.te`.
3. Оновлено документацію у `README.md` з уточненням, що системні теми та переклади інтерфейсу Horizon OS видаляються при оновленнях/даунгрейдах для уникнення фатальних помилок, а файли перекладу окремих додатків (зокрема DBI) надійно зберігаються.

## Попередній delivery: v0.13.812 — Remove themes & translations on all firmware updates

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. Перевірено логіку оновлення системної прошивки: раніше очищення тем та перекладів виконувалося виключно при даунгрейді у блоці `apply_downgrade_fix`, через що при звичайному оновленні (upgrade) старі теми та переклади залишалися й призводили до падіння Atmosphere (фатальна помилка `2162-0002`).
2. У `InstallValidatedFirmware` (`kefir_firmware.cpp`) одразу після успішного виконання `amssuApplyPreparedUpdate()` додано безумовний виклик `CleanThemesAndTranslations()` з індикатором прогресу `Removing themes and translations...` та фіксацією змін на SD (`sdmc`). Тепер будь-яка інсталяція прошивки (як оновлення/upgrade, так і даунгрейд) надійно очищує встановлені кастомні теми (`0100000000001000`, `0100000000001013`, `0100000000001007`, `00FF007468656D65`) та переклади інтерфейсу (`0100000000000803`...`0100000000001015`, DBI) перед перезавантаженням консолі.
3. Оголошено перевантаження `CleanThemesAndTranslations(sd)` та `CleanThemesAndTranslations()` у `detail` (`kefir_firmware.hpp`/`kefir_firmware.cpp`) та перейменовано константу на `FIRMWARE_CLEANUP_PATHS`.
4. У діалозі успішного встановлення оновлення `prompt_reboot` (`kefir_menu.cpp`) додано явне повідомлення для користувача про те, що кастомні теми та переклади були видалені для запобігання фатальним помилкам сумісності на новій версії прошивки.
5. Оновлено документацію `README.md`.

## Попередній delivery: v0.13.811 — Forced removal of themes & translations on downgrade, Nintendo folder note

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. Реалізовано примусове видалення кастомних тем інтерфейсу (`0100000000001000`, `0100000000001013`, `0100000000001007`, `00FF007468656D65`) та системних перекладів (`0100000000000803`...`0100000000001015`, переклади DBI) після даунгрейду прошивки.
2. Оновлено RomFS-скрипт `assets/romfs/tegra/downgrade_fix.te`: на етапі виконання в TegraExplorer без блокувань файлів з боку Horizon OS викликається `deldir(...)` для всіх папок тем та перекладів на карті пам'яті.
3. У `kefir_firmware.cpp` додано попереднє очищення `CleanThemesAndTranslations` перед перезавантаженням консолі під час стейджингу фіксу даунгрейду.
4. Оновлено діалог `DowngradeWarningBox` у `kefir_menu.cpp`: розміри скориговано до 900x580px, додано чітке роз'яснення щодо недійсності папки `Nintendo` на карті пам'яті після скидання консолі в Maintenance Mode та підтвердження, що погодження на її видалення консоллю не зачіпає збереження ігор.
5. Оновлено тексти в `en.json` та `uk.json`, оновлено документацію `README.md`.

## Попередній delivery: v0.13.810 — Automated post-downgrade fix via TegraExplorer & Maintenance Mode warning

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. Додано новий RomFS-скрипт `assets/romfs/tegra/downgrade_fix.te` для автоматичного фіксу даунгрейду (видалення системного сейву `bis:/save/8000000000000073`). Скрипт відповідає суворим вимогам TE-парсера (без операторів `&&`/`||`), має раннє роззброєння (early disarm), визначає цільовий NAND (EmuNAND або SysNAND через прапорець `sd:/config/kefir/downgrade_nand`, макропідстановку та `emu()`), монтує відповідний SYSTEM (`mountemu` або `mountsys`), видаляє заблокований сейв 0073, відображає кольоровий статус і через 3 секунди повертається в Hekate (`goHekate()`) без очікування дій від користувача.
2. Впроваджено `StageDowngradeFix` та `StageAndLaunchDowngradeFix` у `kefir_firmware.cpp`/`kefir_firmware.hpp`: автоматичне розгортання `/startup.te`, копіювання до `/TegraExplorer/scripts/downgrade_fix.te`, запис цільового NAND (`App::IsEmummc()`) та верифікація наявності TegraExplorer через `utils::ensureTegraExplorerPayload`. `IsDowngradeFixAvailable()` тепер повертає `true`.
3. Оновлено `DowngradeWarningBox` у `kefir_menu.cpp`: розміри розширено до 880x560px, додано покрокову інструкцію входу в Maintenance Mode при помилках завантаження (запуск прошивки -> очікування бутлого Switch/Kefir -> затискання обох кнопок гучності Vol+/Vol- -> скидання «Initialize Console Without Deleting Save Data» зі збереженням сейвів та попередженням про видалення ігор), оновлено QR-код і посилання на ручний гайд `https://switch.customfw.xyz/downgrade_fw`.
4. Значення `m_downgrade_fix_mode` за замовчуванням змінено на `DowngradeFixMode_Automatic`. Після підтвердження вікна попередження даунгрейд фікс активується автоматично без повторного запиту. Після встановлення даунгрейду вибір «Reboot» у діалозі перезавантажує консоль безпосередньо в TegraExplorer через `utils::rebootToPayload`.
5. Додано переклади нових повідомлень та інструкцій в `en.json` та `uk.json`. Оновлено `README.md`.

## Попередній delivery: v0.13.809 — Fix duplicate HR and separator navigation in DBI menu

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. Прибрано дублюючу горизонтальну лінію посередині елемента-розділювача у `DrawActionListItem` та `Menu::DrawItemRow`: тепер під «Скинути налаштування DBI» залишається рівно один роздільник і чистий відступ до списку мов.
2. Виправлено перехід курсора через розділювач у `DbiMenu::Update`: перевірку `SettingsItemKind::Header` обмежено лише подіями тачскрину (`touch && ...`), завдяки чому при навігації кнопками контролера вниз/вгору `SetIndex` викликається належним чином, а `ResolveItemIndex` безперешкодно переводить фокус на перший елемент списку мов («Belarusian») та назад.

## Попередній delivery: v0.13.808 — Pin top DBI items, separator and alphabetical sorting

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. У меню «DBI» три основні дії («Завантажити/Оновити список перекладів DBI», «Остання російська версія DBI» та «Скинути налаштування DBI») тепер завжди зафіксовані зверху списку.
2. Кнопка списку динамічно перемикається між «Download DBI translations list» (якщо переклади ще не завантажені) та «Update DBI translations list» (після завантаження пакета). Додано локалізацію нового рядка для всіх 14 мов.
3. Додано `MakeSeparator()` та підтримку малювання горизонтальної розділювальної лінії для порожніх заголовків у `DrawActionListItem` та `Menu::DrawItemRow`. Розділювач відображається між основними діями та списком перекладів.
4. Навігація в меню DBI (`ResolveItemIndex`, `EnsureVisible`, тач) тепер пропускає розділювач і захищає від вибору неінтерактивної лінії.
5. Список завантажених фанатських перекладів DBI тепер автоматично сортується в алфавітному порядку за назвою без урахування регістру (`strcasecmp`).
6. Оновлено `README.md` з описом функціоналу менеджера DBI та фанатських перекладів.

## Попередній delivery: v0.13.807 — Move fan curve to Tools

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. Пункт «Fan curve» (крива кулера) перенесено з меню «Kefir Settings» до списку системних інструментів «Tools» (`SystemToolsMenu`).
2. Оновлено опис «Kefir Settings» у меню інструментів на «Console-specific Kefir switches.» із відповідною локалізацією всіма 14 мовами.
3. Очищено невикористовувані заголовки в `settings_kefir.cpp`.

## Попередній delivery: v0.13.806 — Alphabetical interface translation languages

Статус: реалізовано; очікується апаратна перевірка.
1. `LoadTranslationsCache()` стабільно сортує отримані мови за відображуваною назвою `entry.name`.
2. Єдине сортування на межі завантаження кешу однаково діє для вже збережених даних і після оновлення списку.

## Попередній delivery: v0.13.805 — Smart default HTTP port

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. HTTP listener спочатку пробує стандартний порт 80, а якщо він недоступний — послідовно 8080–8090.
2. UI і QR не додають `:80` до `kefir.local` або числового IPv4; будь-який fallback-порт показується явно.
3. Console Transfer також спочатку перевіряє неявний port 80, а потім 8080–8090; явні scheme/port не змінюються.
4. README і API comment синхронізовано з новим port/fallback contract.

## Попередній delivery: v0.13.804 — Automated firmware translation mapping and FW 22.5.0+ support

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. Додано новий реліз перекладу `FW22.5.0-TR2.01` до таблиці відомих релізів `KNOWN_TRANSLATIONS` із власним тегом метаданих `FW22.5.0-TR2.01`.
2. Реалізовано автоматизовану систему співвідношення версій прошивки у `ResolveFirmwareCompatibility`:
   - Точне співпадіння версії прошивки консолі з будь-яким відомим релізом (`16.1.0`, `17.0.0`, `17.0.1`, `18.1.0`, `19.0.0`, `20.4.0`, `22.5.0`) призначає його без попереджень (`warning_required = false`).
   - Версії, новіші за останній реліз перекладу (`> 22.5.0`, наприклад 22.5.1, 23.0.0 тощо), автоматично обирають останній доступний реліз (`FW22.5.0-TR2.01`) із попередженням сумісності (`warning_required = true`), що усуває штучні блокування перекладу на нових прошивках.
   - Збережено надійні діапазони конкретних співставлень для проміжних версій (16.0.0–16.1.0, 18.0.0–18.1.0, 19.0.0–19.0.2, 20.0.0–20.5.0, 21.0.0–<22.5.0).
3. Додано помічник `ExtractFirmwareFromTag(target_tag)` для вилучення версії системного ПЗ з тегу релізу; у діалозі попередження версія динамічно замінює "20.4.0" у локалізованому рядку для всіх 14 мов.
4. Оновлено хост-тести `tests/test_translation_policy.cpp` з перевіркою точного співпадіння 22.5.0, автоматичного вибору найновішого релізу для 22.5.0+, проміжних фолбеків 21.0.0–22.4.0, вилучення версій з тегів та граничних випадків.
5. Оновлено `README.md` з описом нової політики автоматичного підбору релізів та підтримки FW 22.5.0+.

## Попередній delivery: v0.13.803 — Global list focus draw order

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. Спільний `List` тепер малює всі видимі нефокусовані елементи першими, а видимий фокусований елемент — останнім, тому сусідні фони не перекривають рамку.
2. Усі `List::Draw` callers передають явний focus index або `List::NO_FOCUS`; спеціальні мапінги та багаторядкове виділення текстового редактора збережено.
3. Дубльовані ручні draw passes прибрано; production traversal винесено у тестований helper без нових залежностей.
4. Додано host regression test для HOME/GRID, visibility, invalid focus, геометрії та single-callback contract.

## Попередній delivery: v0.13.802 — `kefir.local` mDNS discovery

Статус: реалізовано; очікується компіляція та апаратна перевірка.
1. До життєвого циклу вебсервера додано легкий IPv4 mDNS-responder для `kefir.local` без нових залежностей.
2. Запити A/ANY обробляються з bounded DNS parsing; malformed та сторонні пакети ігноруються.
3. UI і QR використовують `http://kefir.local:<port>` при успішному старті mDNS; за будь-якої помилки зберігається числовий IPv4 fallback.
4. mDNS socket/thread закриваються разом із вебсервером та перестворюються після resume/rebind.

## Попередній delivery: v0.13.801 — TegraScript compound-condition compatibility

Статус: реалізовано; очікується апаратний повторний тест.
1. Прибрано всі `&&` і `||` з production dump/restore/undo скриптів, оскільки TegraExplorer 4.2.17 падає на складених умовах навіть зі scalar variables.
2. Умови розкладено на прості послідовні перевірки без зміни restore, safety backup та readback логіки.
3. Contract test забороняє повторну появу compound conditions у restore script.

## Попередній delivery: v0.13.800 — Profiles and Playtime integration

Статус: реалізовано; тести та WSL ReleaseWithInstall пройшли, потрібна апаратна перевірка.
1. Dump/restore використовують потокові `readToFile` / `writeFromFile`; TegraExplorer 4.2.17 отримав потоковий `compareToFile` для readback без повного ByteArray.
2. Restore створює обов'язковий operation-specific safety backup до першого запису. Якщо Horizon не може зняти raw snapshot, його fail-closed створює TegraExplorer.
3. Safety backup зберігається поза pending/staging, прив'язаний до точного NAND і pack path; архіви та safety backup автоматично не видаляються.
4. Після commit сейв повторно відкривається і ключові файли порівнюються потоково; success marker ставиться лише після повного успіху.
5. Gemini виконав `tests/run.sh` (184 checks і 17 mutations) та WSL ReleaseWithInstall build. Залишився hardware backup/restore через Sphaira і перевірка активностей у DBI.

## Попередній delivery: v0.13.799 — Test Build skill and compilation fix

Статус: реалізовано; NRO успішно зібрано через WSL з exit code 0.
1. Створено скіл «Протестуй збірку» (`test-build`) у `.grok/skills/test-build/SKILL.md` та `.agents/skills/test-build/SKILL.md` з чітким регламентом компіляції через WSL (`cmake --preset ReleaseWithInstall`), усунення помилок, підняття версії та створення коміту.
2. Виправлено помилки компіляції через неоголошений `Result_Success` у `sphaira/source/ui/menus/users/users_restore.cpp` (рядок 329) та `sphaira/source/ui/menus/users/users_nand_library.cpp` (рядки 974, 1042) із заміною на `R_SUCCEED()`.
3. Успішно виконано повну компіляцію `sphaira_nro` у WSL без помилок.
4. Оновлено `AGENTS.md` з явним винятком на компіляцію для скіла «Протестуй збірку».
5. `sphaira_VERSION` піднято до `0.13.799`.

## Попередній delivery: v0.13.798 — Restore immediate NTP display with native timezone

Статус: реалізовано; очікується компіляція користувачем та перевірка на консолі.
1. Відновлено атомарний process-local `g_display_offset` як чисту UTC-різницю `network_time - current_time` для fallback-шляху, де Horizon застосовує збережений Network Clock лише після reboot.
2. `MenuBase::GetPolledData()` спочатку додає UTC-корекцію до `std::time(NULL)`, а потім один раз викликає `localtime_r()`, тому timezone та DST беруться лише з налаштувань консолі і не подвоюються.
3. Повернуто розділення live-запису `time:su`/`time:s` і `set:sys` fallback: `__libnx_init_time()` та `Clock synced` виконуються лише після live User Clock update; fallback коригує годинник Sphaira і чесно логує потребу reboot.
4. `sphaira_VERSION` піднято до `0.13.798`. `git diff --check` пройшов; compile/tests/NRO не запускалися за policy.

## Попередній delivery: v0.13.797 — Safe and shared remote NAND backup transfer

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. **Атомарне завантаження NAND-бекапів**:
   - Архіви завантажуються у `.part`, перевіряються за оголошеним розміром, атомарно перейменовуються у фінальне `.zip`-ім'я і лише тоді валідуються через `nand_transfer::IsPackArchive`.
   - Папки завантажуються у `_staging_*`; кожен файл звіряється з розміром з `/list-recursive`, а валідний pack перейменовується у фінальну collision-safe папку.
   - Cancel, HTTP/FS error, size mismatch, unsafe manifest або невалідна структура видаляють поточний незавершений transfer без зачіпання наявних бекапів.
2. **Спільний Console Transfer flow**:
   - `ConnectConsoleTransfer` структурно перевіряє `/list` через `yyjson` і приймає лише JSON object з масивом `entries`.
   - Старий remote user-backup flow тепер також викликає `ConnectConsoleTransfer`; дубльоване введення IP та зондування портів видалено.
   - Спільний `ParseManifestResponse` відхиляє порожні, malformed та небезпечні шляхи замість мовчазного створення часткового pack.
3. **Завершення restore та UX**:
   - `PromptNandPackRestore` відкривається лише після того, як завантажений pack пройшов валідацію і знайдений у `nand_transfer::ListPacks()` з точним `save_00F0`.
   - Помилка списку NAND-бекапів має окремий локалізований текст у `en.json`, `uk.json`, `ru.json`.
4. **Версія**: `sphaira_VERSION` піднято до `0.13.797`. Збірка та апаратна перевірка не запускалися за policy.

## Попередній delivery: v0.13.796 — Receive and restore profiles & play hours backups from another console

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. **Прийом бекапів з іншої консолі («Receive from another console»)**:
   - У меню «Manage Backups» (кнопка `+` / Options) та у бічному контекстному меню списку користувачів (розділ CONSOLE MOVE) додано дію «Receive from another console».
   - Використовує уніфікований `ConnectConsoleTransfer` для перевірки Wi-Fi з'єднання, автозаповнення поточної підмережі в `swkbd` та швидкого зондування портів 8080..8090.
   - Запитує список резервних копій через `/list`, парсить JSON через `yyjson` і показує модальний список `PopupList` з хмарними маркерами `SetRemoteMarkers`.
   - Дозволяє завантажити окремий бекап або всі бекапи одразу («[Receive All Backups]») безпосередньо у локальну директорію `/config/kefir/nand_transfer/` без негайного запуску відновлення.
2. **Відновлення з іншої консолі («Restore from another console»)**:
   - У меню «Restore profiles & play hours» (кнопка `+` / Options), у вікні перегляду деталей бекапу `NandPackDetailMenu` та у бічному меню користувачів додано дію «Restore from another console».
   - Завантажує вибраний віддалений бекап на локальну microSD карту (`/config/kefir/nand_transfer/`) із захистом від колізій імен (`_1`, `_2`).
   - Після успішного локального збереження відразу відкриває стандартний діалог відновлення `PromptNandPackRestore` («Profiles only» чи «Profiles + play hours» при виявленні сейву `00F0`) з підготовкою середовища для TegraExplorer.
3. **Максимальне перевикористання коду без дублювання**:
   - Перевикористано спільні компоненти `ConnectConsoleTransfer`, `PopupList`, `yyjson`, `curl::ToFile`, `curl::ToMemory`, `PromptNandPackRestore`.
   - Забезпечено відкриття контекстного меню в `NandPackLibraryMenu` навіть при порожньому списку локальних бекапів (щоб користувач міг прийняти бекап на чисту консоль).
4. **Локалізація та i18n**:
   - Додано всі необхідні ключі перекладів у `en.json`, `uk.json`, `ru.json`.
5. **Версія та документація**:
   - Піднято `sphaira_VERSION` до `0.13.796` у `sphaira/CMakeLists.txt`.
   - Оновлено `README.md`, `task.md`, `walkthrough.md`, `audit.md`.

## Попередній delivery: v0.13.795 — Manage Backups context menu with legend parity, direct Restore & Send to another console

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. **Повна синхронізація дій легенди та контекстного меню Manage Backups (`users_nand_library.cpp`)**:
   - Усі дії з нижньої панелі дій (легенди геймпада) та додаткові операції перенесено у контекстне меню (бічний `Sidebar`), що відкривається кнопкою `+` або тапом на екран:
     - `Open` (ActionIcon::Folder) — відкриття деталей бекапу (відповідає кнопці `A`).
     - `Restore` (ActionIcon::Save) — пряме відновлення поточної резервної копії (`ConfirmRestoreCurrent()`) без необхідності спочатку відкривати вікно деталей.
     - `Rename` (ActionIcon::Edit) — перейменування бекапу.
     - `Delete` (ActionIcon::Delete) — видалення виділених або поточного бекапу (відповідає кнопці `Minus` / Select).
     - `Send to another console` (ActionIcon::Move) — відправка резервної копії на іншу консоль або ПК через Console Transfer по локальній мережі Wi-Fi.
   - Секція `SELECTION`:
     - `Select` / `Deselect` (ActionIcon::Toggle) — виділити або зняти виділення з поточної резервної копії (відповідає кнопці `X` у легенді), без зміщення фокусу.
     - `Select All` (ActionIcon::Range) — виділити всі бекапи.
     - `Clear selection` (ActionIcon::Undo) — зняти виділення з усіх елементів (відповідає поведінці кнопки `B` при активному виділенні).
     - `Invert` (ActionIcon::Refresh) — інвертувати виділення (відповідає кнопці `Y`).
2. **Підтримка поширення бекапів профілів та годин гри (`StartConsoleTransferShareNandBackups`)**:
   - Реалізовано функцію `StartConsoleTransferShareNandBackups()` у `install_share.hpp` / `install_share.cpp` для монтування теки `/config/kefir/nand_transfer` на сервері Console Transfer.
   - У меню `ConsoleTransferMenu` додано окремий пункт «Share Profiles & Play Hours».
3. **Оновлення меню дій у `users_manage.cpp` (`ManageBackupsMenu::PromptAction`)**:
   - Додано векторні іконки `ActionIcon` для всіх пунктів меню вибору дій.
   - Додано дії `Select` / `Deselect`, `Select All`, `Clear selection` та `Invert` для досягнення повної функціональної відповідності легенді.
4. **Локалізація та i18n**:
   - Додано ключ перекладу `"Deselect"` до всіх 14 мовних словників `assets/romfs/i18n/*.json`.
   - Додано ключі для нових опцій спільного доступу та попереджень у `en.json`, `uk.json`, `ru.json`.
5. **Версія та документація**:
   - Піднято `sphaira_VERSION` до `0.13.795` у `sphaira/CMakeLists.txt`.
   - Оновлено `README.md`, `task.md`, `walkthrough.md`, `audit.md`.

## Попередній delivery: v0.13.794 — Manage Backups context menu with Select All, Delete and Rename

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. **Контекстне меню (Options / Sidebar) для Manage Backups (`users_nand_library.cpp`)**:
   - Додано обробку кнопки `Button::START` (кнопка `+` на Switch / `Options` у рядку дій) для виклику бічного контекстного меню `Sidebar`.
   - Оновлено підказку у заголовку вікна: `"+ opens options. A opens pack details. X marks backups."`.
   - Розділ `ACTIONS`:
     - `Open` — перегляд деталей бекапу (відновлення профілів та годин гри).
     - `Rename` — безпечне перейменування вибраного бекапу через системну клавіатуру (`swkbd`) з можливістю надати будь-яке унікальне ім'я.
     - `Delete` — видалення вибраних бекапів (або поточного) з підтвердженням `OptionBox` та прогресом `ProgressBox`.
   - Розділ `SELECTION`:
     - `Select All` — виділити всі резервні копії у списку (`SelectAll()`).
     - `Clear selection` — зняти виділення (відображається динамічно, коли є виділені елементи).
     - `Invert` — інвертувати поточний стан виділення.
2. **Безпечне перейменування бекапів (`RenamePack`) та відображення унікальних імен**:
   - `RenamePack`: викликає екранну клавіатуру `swkbd::ShowText`, очищає пробіли, санітизує небезпечні символи файлової системи FAT32 (`/`, `\`, `:`, `*`, `?`, `"`, `<`, `>`, `|`), перевіряє наявність колізій імен та перейменовує архів або теку.
   - Валідація результату через `nand_transfer::IsPackArchive` / `IsPack` із автоматичним відкатом при помилці.
   - `GetPackDisplayName`: якщо бекапу надано унікальне ім'я (перейменовано користувачем), у списку великим шрифтом показується задане ім'я, а якщо воно містить інформацію про створення — дата виводиться у підзаголовку поруч із лічильником акаунтів.
   - Заголовок екрана деталей `NandPackDetailMenu` синхронізовано з унікальним іменем бекапу.
3. **Підтримка Select All у `users_manage.cpp`**:
   - Додано метод `SelectAll()` та пункт `Select All` у спливаюче меню дій `ManageBackupsMenu::PromptAction`.
4. **Версія та документація**:
   - Піднято `sphaira_VERSION` до `0.13.794` у `sphaira/CMakeLists.txt`.
   - Оновлено `README.md`, `task.md`, `walkthrough.md`, `audit.md`.

## Попередній delivery: v0.13.793 — diagnostic dashboard for dump script and 5s auto-reboot to Hekate

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. **Графічний діагностичний дашборд для скрипта дампу (`nand_transfer_dump_auto.te`)**:
   - Повністю перероблено архітектуру відображення за зразком рестора (`nand_transfer_restore_auto.te`):
     - Рядок 0: попіксельний темно-сірий банер `setpixels(0, 0, 1280, 16, 0x1B1B1B)`, бірюзовий заголовок `=== Dump Profiles and Playtime | DIAG 4.2.13 ===` та апаратний анімований спінер `spinner(1, 77, 0, 0x00FF00)`.
     - Рядки 1–10: стабільна таблиця діагностики з індикацією джерела (Source NAND), цільової теки (Target Pack), поодиноких статусів системних сейвів (`0010`, `0011`, `00F0`, `0041`), кількості записаних файлів та лічильника помилок.
     - Рядок 11: динамічний рядок активності з обрізанням задовгих імен файлів (понад 25 символів) без перенесення рядків.
     - Рядки 13–38: виділена зона `Event Log` для детального звіту ходу операції.
   - Усунуто мерехтіння екрана: прибрано виклики `clear()` для кожного окремого сейву.
   - Інтегровано живий спінер: виклик `upd_s() -> spinner()` під час читання й запису.
2. **Автоматичне 5-секундне перезавантаження в Hekate замість ручної паузи**:
   - У скриптах дампу (`nand_transfer_dump_auto.te`) та рестора (`nand_transfer_restore_auto.te`), а також `account_0010_dump.te`, `account_0010_apply_link.te` та `playtime_restore.te` ручне блокуюче очікування `pause()` замінено на автоматичний 5-секундний таймаут `sleep(5000)` із повідомленням `Rebooting to Hekate in 5 seconds...`.
   - Автоматичний 5-секундний таймаут застосовано також до preflight-перевірок помилок перед ланцюжком `goHekate()`.
3. **Версія та документація**:
   - Піднято `sphaira_VERSION` до `0.13.793` у `sphaira/CMakeLists.txt`.
   - Оновлено `README.md`, `task.md`, `walkthrough.md`, `audit.md`.

## Попередній delivery: v0.13.792 — payload swap fallback for legacy Hekate and bidirectional Hekate restore

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. **Реалізація надійного фолбеку підміни пейлоада (`swapPayload`) при відсутності Hekate Payload API**:
   - Якщо одноразовий Hekate Payload API недоступний або запис запиту неможливий, Sphaira переходить на апаратний фолбек підміни `sd:/payload.bin`.
   - Перед підміною Sphaira перевіряє наявність `sd:/bootloader/update.bin`; якщо файл відсутній, вона копіює поточний `sd:/payload.bin` (Hekate) у `sd:/bootloader/update.bin`, гарантуючи збереження Hekate як резервної копії.
   - Далі цільовий пейлоад (TegraExplorer) копіюється в `sd:/payload.bin`, записується autoboot в `hekate_ipl.ini` та виконується перезавантаження через `requestForcedReboot`.
2. **Примусове зворотне відновлення Hekate у скриптах TegraExplorer**:
   - У всіх шести `.te` скриптах RomFS (`nand_transfer_dump_auto.te`, `nand_transfer_restore_auto.te`, `account_0010_dump.te`, `account_0010_apply_link.te`, `playtime_restore.te`, `Undo_restore_if_wont_boot.te`) як на початку (early disarm), так і у фінальній процедурі `goHekate()` додано примусове відновлення Hekate:
     `if (fsexists("sd:/bootloader/update.bin")) { delfile("sd:/payload.bin") copyfile("sd:/bootloader/update.bin", "sd:/payload.bin") }`
   - Це миттєво повертає `sd:/payload.bin` у стан Hekate ще до запуску копіювання файлів, захищаючи консоль від boot loop навіть при раптовому вимкненні під час дампу.
3. **Версія та документація**:
   - Піднято `sphaira_VERSION` до `0.13.792` у `sphaira/CMakeLists.txt`.
   - Оновлено `README.md`, `task.md`, `walkthrough.md`, `audit.md`.

## Попередній delivery: v0.13.791 — restore payload.bin in TE scripts and verify Hekate integrity

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. **Відновлення перевірки `sd:/payload.bin` у процедурі повернення до Hekate (`goHekate`)**:
   - На збірках Kefir кореневий файл `sd:/payload.bin` є саме бінарником Hekate IPL.
   - У всіх автоматизованих скриптах TegraExplorer (`assets/romfs/tegra/*.te`: `nand_transfer_dump_auto.te`, `nand_transfer_restore_auto.te`, `account_0010_dump.te`, `account_0010_apply_link.te`, `playtime_restore.te`, `Undo_restore_if_wont_boot.te`) відновлено перевірку та завантаження `sd:/payload.bin` одразу після `sd:/bootloader/update.bin`.
   - Порядок завантаження Hekate: `sd:/bootloader/update.bin` → `sd:/payload.bin` → `sd:/bootloader/payloads/hekate.bin` → `sd:/atmosphere/reboot_payload.bin` → `reboot()`.
2. **Раннє роззброєння конфігурацій повернення (Early Disarm)**:
   - На початку виконання кожного скрипта додано примусове видалення тимчасового запиту `sd:/config/kefir/hekate-payload-request.ini` та відновлення оригінального `hekate_ipl.ini` з видаленням `.bak`, що запобігає зацикленню завантаження TegraExplorer у разі аварійного переривання або вимкнення консолі під час дампу/відновлення.
3. **Аудит цілісності пейлоадів**:
   - Підтверджено, що функції Sphaira (`ensureTegraExplorerPayload`, `rebootToPayload`) не модифікують і не підміняють `sd:/payload.bin` або `sd:/bootloader/update.bin`. Запис бінарника TegraExplorer виконується виключно у `/bootloader/payloads/TegraExplorer.bin`.
4. **Версія та документація**:
   - Піднято `sphaira_VERSION` до `0.13.791` у `sphaira/CMakeLists.txt`.
   - Оновлено `README.md`, `task.md`, `walkthrough.md`, `audit.md`.

## Попередній delivery: v0.13.790 — embedded TegraExplorer in RomFS, payload version auto-sync, dump script pause & Hekate reboot, upstream libhaze fixes

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. **Інтеграція актуального TegraExplorer у RomFS Sphaira**:
   - `Makefile` проекту TegraExplorer оновлено: після WSL-збірки бінарник `TegraExplorer.bin` автоматично копіюється в `../sphaira/assets/romfs/tegra/TegraExplorer.bin` (та `/mnt/d/git/dev/sphaira/assets/romfs/tegra/`).
   - Актуальну зібрану версію `TegraExplorer.bin` (v4.2.13.900) скопійовано безпосередньо у RomFS Sphaira `assets/romfs/tegra/TegraExplorer.bin`.
2. **Автоматична перевірка та синхронізація версії пейлоада на SD**:
   - У `sphaira/source/utils/utils.cpp` реалізовано структури `TegraExplorerFooter` (сигнатури `KFRP` ... `PRFK`) та `TegraExplorerVersion` із повноцінним лексикографічним порівнянням версій (`major`, `minor`, `patch`, `kefir_version`).
   - Додано `utils::ensureTegraExplorerPayload`: якщо TegraExplorer на SD відсутній, він автоматично встановлюється з RomFS у `/bootloader/payloads/TegraExplorer.bin`; якщо версія на SD старіша за версію в RomFS — файл на SD оновлюється на нову версію; якщо на картці пам'яті вже новіша або така сама версія — файл залишається без змін.
   - `utils::findTegraExplorerPayload` та `utils::rebootToPayload` викликають `ensureTegraExplorerPayload` перед запуском.
3. **Відображення результатів скрипта дампа та повернення в Hekate**:
   - У `nand_transfer_dump_auto.te` у функціях `printResult()` та `failOut()` додано кольоровий звіт результатів (зелений OK / червоний NOT OK) та обов'язковий `pause()` («Press any button to reboot to Hekate...»), завдяки чому екран дампа більше не зникає безслідно.
   - У функції `goHekate()` видалено помилковий фолбек на `sd:/payload.bin` (який перезапускав сам TegraExplorer), очищено `sd:/config/kefir/hekate-payload-request.ini`, відновлено первинний `hekate_ipl.ini` із `.bak`, а пріоритет завантаження віддано Hekate (`sd:/bootloader/update.bin`, `bootloader/payloads/hekate.bin`, `atmosphere/reboot_payload.bin`). При відсутності Hekate користувач бачить попередження та підтверджує reboot.
   - Аналогічні виправлення `goHekate` та `pause` внесено у `account_0010_dump.te`, `account_0010_apply_link.te`, `nand_transfer_restore_auto.te`, `playtime_restore.te` та `Undo_restore_if_wont_boot.te`.
4. **Upstream post-1.0.6 libhaze fixes**:
   - Idempotent patch `sphaira/cmake/patch_libhaze.cmake`: MTP `GetDeviceInfo` повідомляє `Kefir Hub/<version> (HOS/<firmware>)`, а EOF у libhaze threaded transfer стискає буфер до фактичного read size перед виходом.
   - Версію передано у `libhaze` через `target_compile_definitions(libhaze PRIVATE -DSPHAIRA_VERSION="${sphaira_VERSION}")`.
   - Тести `tests/test_patch_libhaze.sh` розширено перевірками.
5. **Версія та документація**:
   - Піднято `sphaira_VERSION` до `0.13.790` у `sphaira/CMakeLists.txt`.
   - Оновлено `README.md`, `task.md`, `walkthrough.md`, `audit.md`.

## Попередній delivery: v0.13.789 — auto launch TegraExplorer via hekate payload fallback on backup

Статус: реалізовано; очікується компіляція користувачем та перевірка на апаратному пристрої.
1. При виборі «Backup profiles & play hours» після зняття Horizon-частини Sphaira автоматично перезавантажує консоль у TegraExplorer без показу зайвого діалогу OptionBox «Launch TegraExplorer».
2. Скрипт дампа `nand_transfer_dump_auto.te` автоматично вивантажується в корінь SD як `sd:/startup.te` із примусовим `fflush`, `fsdevCommitDevice("sdmc")` та `sd.Commit()` для гарантії синхронізації блоків на фізичному носії перед ребутом.
3. У `utils::rebootToPayload` інтегровано автоматичний фолбек: якщо одноразовий Hekate Payload API (`hekate-payload-api.ini` / `hekate-payload-request.ini`) недоступний або запис завершується помилкою, функція автоматично перемикається на `setHekateAutobootPayload` (створення резервної копії `hekate_ipl.ini.bak` та запис тимчасового autoboot) з наступним примусовим перезавантаженням (`requestForcedReboot`).
4. Шлях пейлоада у `writeHekateAutobootIni` нормалізується з очищенням префіксів `sdmc:/`, `sd:/` та початкових слешів.
5. У `account_restore.cpp` метод `ReadRomfsTe` захищено від помилок подвійної ініціалізації romfs: якщо romfs вже змонтований, файл читається напряму.
6. Гарантовано очищення `sd:/startup.te` як у самому скрипті TegraExplorer, так і у функціях `CleanDumpHandshake` та `ClearPending` у Kefir Hub.
7. Версію піднято до `0.13.789` у `sphaira/CMakeLists.txt`; оновлено документацію `README.md`.

## Попередній delivery: v0.13.788 — archive Profiles and Playtime backups and show accounts in a 2x4 grid

Статус: реалізовано; апаратна перевірка очікується.
1. Нові `Profiles and Playtime` пакети мають завершуватися одним ZIP-архівом із атомарним `.part` → final перейменуванням; неповний каталог після помилки не повинен виглядати готовим backup.
2. `Manage Backups` має читати новий archive format і залишити read/restore/delete сумісність зі старими каталогами.
3. Перед TegraExplorer restore рівно вибраний архів розпаковується у тимчасовий прямий каталог `nand_transfer`, а в `nand_pack.txt` передається саме він; після завершення або скасування staging прибирається без зміни оригінального архіву.
4. Деталі пакета показують до восьми акаунтів сіткою 2 колонки × 4 рядки з коректною навігацією, аватаром, nickname та UID.
5. Використати наявні minizip/transfer helpers і `List(2, 4, ...)`; нових залежностей або нового загального archive framework не додавати.
6. Версію піднято до `0.13.788`; додано host contract tests. Gemini виконав повний `tests/run.sh` (150 checks нового тесту, all green) і ReleaseWithInstall build до фінальної senior guard-правки. Фінальний diff пройшов `git diff --check`; повторний compile/test senior не запускав за policy.

## Попередній delivery: v0.13.787 — restore exact selected NAND transfer pack through TegraExplorer

Статус: реалізовано; Kefir Hub встановлює перевірений TegraExplorer restore-скрипт і передає йому точний каталог пакета, який вибрав користувач:
1. Скрипт приймає лише прямого нащадка `sd:/config/kefir/nand_transfer/<pack>` із `restore_pending/nand_pack.txt`; сканування каталогів і fallback на «останній» пакет відсутні.
2. Відновлення 0010, 0011 та опційного 00F0 використовує відомі дерева, перевірені create/write/commit та fail-closed маркер `nand_restored.ok`.
3. Статус Undo враховує лише safety snapshot, потрібні для сейвів у вибраному пакеті; текст помилки не приховує раніше успішні commit.
4. Додано `tests/test_nand_restore_auto_contract.sh` і запуск у `tests/run.sh`. Повний набір тестів та NRO-збірка пройшли на основному варіанті скрипта до фінальної малої правки версії/status; після неї compile/tests не запускалися за policy. Апаратна перевірка через Hub очікується.

## Попередній delivery: v0.13.786 — harden remote update check, stream-free version parsing and auto-update install destination

Статус: реалізовано; усунено помилковий запуск завантаження оновлення на старті програми, коли встановлена версія є вищою за віддалену, усунено залежність від `std::stringstream` у парсингу версій та оптимізовано шлях встановлення бінарника:
1. **Причина проблеми**:
   - При парсингу номерів версій через `std::stringstream` помилка ініціалізації або порожній результат для поточної версії програми (`APP_VERSION`) залишали вектор компонентів порожнім.
   - У `version::IsLower` порожній вектор компонентів інтерпретувався як `0.0.0`, що призводило до помилкової оцінки встановленої збірки як застарілої відносно віддаленого релізу (`0.13.601`).
   - Як наслідок, при запуску в режимі `Silent` запускалося завантаження релізу у фоні, у шапці відображалася смуга «Updating», а через відсутність перезапису поточного запущеного файлу версія після перезапуску не змінювалася.
2. **Чистий та надійний парсер версій без `<sstream>`**:
   - У `sphaira/include/version_compare.hpp` функцію `version::Parse` переписано на прямий покажчиковий цикл без використання важкого `std::stringstream` та прив'язки до локалі iostreams.
   - Додано функцію `version::IsNewer(current, candidate)`, яка суворо перевіряє наявність валідних числових компонентів у обох версіях. Якщо будь-яка з версій порожня або не парситься в числа, функція повертає `false` (fail-safe: за жодних обставин не запускати оновлення).
3. **Посилена перевірка та діагностичне логування у `main_menu.cpp`**:
   - Додано перевірку HTTP-кодів відповіді GitHub API: якщо отримано код помилки (наприклад, 403 Rate Limit, 404 тощо), перевірка завершується без спроб оновлення.
   - Перевірку новішої версії переведено на `version::IsNewer(APP_VERSION, version)` з докладним логуванням (`[UpdateCheck] Installed %s >= remote %s; no update required`).
4. **Синхронізація AboutBox та шляху встановлення**:
   - У `sphaira/source/ui/about_box.cpp` перевірку релізу оновлено на `version::IsNewer(APP_VERSION, tag)`.
   - У `sphaira/source/auto_update.cpp` (`ResolveInstallDestination`) канонічний шлях `/switch/kefir-hub/kefir-hub.nro` встановлено найвищим пріоритетом.
5. **Тести та версія**:
   - У `tests/test_version_compare.cpp` додано повний набір тестів `test_is_newer` для валідних, рівних, застарілих та порожніх/некоректних версій.
   - Оновлено `README.md`.
   - Версію піднято до `0.13.786` у `sphaira/CMakeLists.txt`. Compile/tests/NRO не запускалися за policy.

1. У `sphaira/include/version_compare.hpp`:
   - Реалізовано потоково-безпечний покажчиковий `version::Parse` та додано `version::IsNewer`.
2. У `sphaira/source/app.cpp`:
   - `App::IsVersionNewer` переведено на `version::IsNewer` з перевіркою непорожніх рядків.
3. У `sphaira/source/ui/menus/main_menu.cpp`:
   - Додано валідацію HTTP-кодів, безпечну перевірку через `version::IsNewer` та діагностичне логування.
4. У `sphaira/source/ui/about_box.cpp`:
   - Синхронізовано виклик `version::IsNewer`.
5. У `sphaira/source/auto_update.cpp`:
   - Пріоритезовано шлях `/switch/kefir-hub/kefir-hub.nro`.
6. У `tests/test_version_compare.cpp`:
   - Додано юніт-тести для `version::IsNewer`.
7. Оновлено `README.md`.
8. Підняти версію до `0.13.786` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.785 — fix svcCallSecureMonitor build error, SetRegion_HTK compatibility and DBI USB status/target synchronization

Статус: реалізовано; усунено помилку компіляції виклику TrustZone/Exosphere SMC у `sphaira/source/app.cpp`, адаптовано перелічення `SetRegion` під сучасний libnx, реалізовано двосторонню передачу параметрів і статусів у протоколі DBI USB:
1. **Виправлення виклику SMC `svcCallSecureMonitor`**:
   - У `sphaira/source/app.cpp` виклик `svcCallSecureMonitor(&args)` повертає `void` згідно з прототипом у системному інтерфейсі `libnx`.
   - Результат виконання зберігається у вихідному регістрі `args.X[0]`. Додано коректне вичитування `const Result smc_rc = static_cast<Result>(args.X[0])`.
2. **Сумісність `SetRegion`**:
   - У `sphaira/source/ui/menus/settings/settings_translate.cpp` замінено неіснуючі в оновленому `libnx` прапорці `SetRegion_KO` та `SetRegion_TWN` на єдиний регіон `SetRegion_HTK` ("Hong Kong/Taiwan/Korea").
3. **Двостороння синхронізація у DBI USB**:
   - Розширено парсинг дескриптора файлів черги на 4-й токен (`file|size|selected|target`), де `target` приймає значення `0` (Auto), `1` (SD) або `2` (NAND).
   - При синхронізації зі списком ПК через `FetchLiveSelection` план автоматично розподіляє місце та оновлює цільові накопичувачі файлів черги.
4. **Звітування про результат встановлення та ємність сховища**:
   - Додано команди `CmdId::PackageStatus` (`0x04`) та `CmdId::StorageInfo` (`0x05`) у протокол DBI USB (`sphaira/include/usb/dbi.hpp`).
   - Після кожного файлу консоль відправляє на ПК результат операції (0 = Installed, 1 = User Skipped, 2 = Already Installed, 3 = Failed) разом із системним `Result` кодом Horizon.
   - Постійно транслюються дані про вільний та загальний об'єм внутрішньої пам'яті NAND та карти microSD.
5. **Верифікація**:
   - Збірка NRO у WSL (`make build`) успішно пройшла на 100%.
   - Усі тести (`tests/run.sh`) успішно виконані.

1. У `sphaira/source/app.cpp`:
   - Виправлено виклик `svcCallSecureMonitor` на `void` із читанням результату з `args.X[0]`.
2. У `sphaira/source/ui/menus/settings/settings_translate.cpp`:
   - Виправлено `GetRegionName` на використання `SetRegion_HTK`.
3. У `sphaira/include/usb/dbi.hpp`, `sphaira/include/yati/source/usb.hpp`, `sphaira/source/yati/source/usb.cpp`:
   - Додано структури `PackageStatusHeader` і `StorageInfoHeader`, методи `GetFileTarget`, `FetchLiveSelection` з таргет-мапою, `SendPackageStatus` та `SendStorageInfo`.
4. У `sphaira/source/ui/menus/dbi/dbi_plan.cpp` та `sphaira/source/ui/menus/dbi/dbi_usb.cpp`:
   - Підключено застосування цільового накопичувача з ПК, відправку статусу пакета та оновлення інформації про сховище.
5. Оновлено документацію `README.md`.
6. Підняти версію проекту до `0.13.785` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.784 — accurate EmuNAND/SysNAND detection, header NAND label and SysNAND forwarder ban protection

Статус: реалізовано; реалізовано точне розпізнавання середовища EmuNAND та SysNAND/Semi-stock при старті програми, виправлено відображення типу сховища у верхньому хедері та додано захист від небажаного встановлення форвардера на чистому SysNAND:
1. **Причина проблеми в детекції EmuNAND**: Раніше `App::IsEmummc()` у `sphaira/source/app_settings.cpp` перевіряв `paths.file_based_path[0] != '\0' || paths.nintendo[0] != '\0'`. Secure monitor call `0xF0000404` (`smc_ams_get_emummc_config`) в Exosphere записує шлях перенаправлення `cfg.emu_dir_path` у вихідний буфер користувача навіть тоді, коли тип конфігурації — `EmummcType_None` (0, тобто SysNAND!). Через це `paths.nintendo` завжди містив рядок шляху, а `App::IsEmummc()` хибно повертав `true` незалежно від фактичного середовища запуску.
2. **Точне визначення активного EmuNAND**:
   - У `App::App()` (`sphaira/source/app.cpp`) вичитуються поля `magic` (нижні 32 біти `args.X[1]` мають дорівнювати `0x30534645` — `'EFS0'`) та `type` (старші 32 біти `args.X[1]`: 0 — SysNAND, 1 — Partition, 2 — File).
   - Додано фолбек через `splGetConfig(SplConfigItem 65007, &val)` (`ExosphereEmummcType`), де Exosphere повертає 1 для активного EmuNAND і 0 для SysNAND.
   - `App::IsEmummc()` тепер повертає `true` суто тоді, коли система дійсно завантажена в EmuNAND (літера 'E' у налаштуваннях консолі).
3. **Визначення присутності EmuNAND на консолі (`App::HasEmummc()`)**:
   - Додано метод `App::HasEmummc()`, який повертає `true`, якщо EmuNAND активний зараз, або якщо на карті пам'яті наявна валідна конфігурація `/emummc/emummc.ini` чи `/emuMMC/emummc.ini` (наявність `enabled == 1`, `path`, `sector != 0`, `nintendo_path`) або стандартні папки `/emuMMC/RAW1`, `/emuMMC/SD00` тощо.
4. **Захист SysNAND від бану Nintendo**:
   - У `forwarder_auto_install.cpp` перед автоінсталяцією форвардера додано перевірку: якщо програма запущена у SysNAND, але на консолі присутній EmuNAND (`!App::IsEmummc() && App::HasEmummc()`), створення нового форвардера HOME Menu скасовується (`plan.install_new = false`), а сповіщення очищається (`plan.notice = Notice::None`). Завдяки цьому чистий SysNAND залишається незмінним.
5. **Напис у хедері**:
   - У `sphaira/source/ui/menus/menu_base.cpp` мітка над індикатором вільного місця формується як `pdata.is_emummc ? "EmuNAND" : "NAND"`. При роботі в SysNAND або Semi-stock (літера 'S' у налаштуваннях) відображається «NAND», а в EmuNAND — «EmuNAND».
6. **Тести та версія**:
   - У `tests/test_forwarder_auto_lifecycle.cpp` додано юніт-тест Test 6 для перевірки блокування створення форвардера у SysNAND при наявному EmuNAND.
   - Оновлено `README.md`. Версію піднято до `0.13.784` у `sphaira/CMakeLists.txt`. Compile/tests/NRO не запускалися за policy.

1. У `sphaira/include/app.hpp`:
   - Додано `static auto HasEmummc() -> bool;`.
   - Додано збереження `m_is_emummc` та `m_emummc_type` у класі `App`.
2. У `sphaira/source/app.cpp`:
   - У `App::App()` додано декодування `magic` і `type` з `args.X[1]` виклику SMC `0xF0000404` та фолбек через `splGetConfig(65007)`.
3. У `sphaira/source/app_settings.cpp`:
   - `App::IsEmummc()` переведено на булевий прапорець `m_is_emummc`.
   - `App::IsParitionBaseEmummc()` та `App::IsFileBaseEmummc()` переведено на перевірку `m_emummc_type`.
   - Реалізовано `App::HasEmummc()`.
4. У `sphaira/source/forwarder_auto_install.cpp`:
   - Додано блокування `plan.install_new = false` при `!App::IsEmummc() && App::HasEmummc()`.
5. У `sphaira/source/ui/menus/menu_base.cpp`:
   - `nand_bar_label = pdata.is_emummc ? "EmuNAND" : "NAND"`.
6. У `tests/test_forwarder_auto_lifecycle.cpp`:
   - Додано Test 6 для правила SysNAND + EmuNAND.
7. Документація (`README.md`):
   - Оновлено опис автоінсталяції форвардера та мітки індикатора сховища.
8. Підняти версію проекту до `0.13.784` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.783 — strict only-if-newer remote update detection via version_compare

Статус: реалізовано; виправлено хибне спрацьовування механізму автооновлення з віддаленого репозиторію GitHub: оновлення тепер пропонується та встановлюється виключно тоді, коли версія релізу на віддаленому репозиторії строго вища за поточну версію програми (`version::IsLower(APP_VERSION, remote_tag)`); раніше функція `App::IsVersionNewer` покладалася на `MAKEHOSVERSION(major, minor, macro)`, де макрос Horizon OS пакує кожне поле у 8 біт, через що для номерів релізів Sphaira з patch > 255 (наприклад, 0.13.782) старші біти patch переповнювали поле minor і локальна версія 0.13.782 чисельно оцінювалася як менша/старіша за реліз 0.13.601, викликаючи небажаний відкат/оновлення при кожному старті; реалізацію `App::IsVersionNewer` переведено на повнорозмірне порівняння довільної кількості числових компонентів через `sphaira::version::IsLower`, видалено залишковий тестовий прапорець `kForceUpdateForTest` у `main_menu.cpp`, у вікні `AboutBox` додано скидання стану в `Idle`, якщо версія релізу не є новішою, додано юніт-тести та оновлено документацію в `README.md`; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/app.cpp`:
   - Підключено `#include "version_compare.hpp"`.
   - `App::IsVersionNewer(const char* current, const char* new_version)` реалізовано через делегування до `version::IsLower(current, new_version)` замість упакування в `MAKEHOSVERSION`.
2. У `sphaira/source/ui/menus/main_menu.cpp`:
   - Видалено тестову константу `kForceUpdateForTest`.
   - Перевірку нових версій релізів переведено на `if (!App::IsVersionNewer(APP_VERSION, version))` без тестових обходів.
3. У `sphaira/source/ui/about_box.cpp`:
   - У `ApplyRelease` додано скидання стану завдання автооновлення в `JobState::Idle`, якщо віддалений тег не є новішим за `APP_VERSION`.
4. У `sphaira/include/version_compare.hpp`:
   - Оновлено коментар щодо `App::IsVersionNewer`.
5. У `tests/test_version_compare.cpp`:
   - Додано юніт-тести для версій з номерами патчів > 255 (`0.13.782` vs `0.13.601`, `0.13.783`, `0.14.0`, `1.0.0` та префіксами `v`).
6. Документація (`README.md`):
   - Оновлено підрозділ `Automatic Silent Update & Self-Updating`.
7. Підняти версію проекту до `0.13.783` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.782 — fix account link detection via Baas administrator IPC

Статус: реалізовано; виправлено хибне визначення стану прив'язки облікового запису Nintendo Account у Sphaira; раніше `QueryHorizonUserLink` та `QueryNintendoAccountId` зверталися до `acc:su` через команду 102 (`GetBaasAccountManagerForSystemService`) та перевіряли `CheckAvailability`, яка завжди повертала успіх (0) навіть для неприв'язаних профілів, через що Sphaira вважала всі локальні профілі зв'язаними офлайн (`Offline`) та блокувала автоматичну пропозицію прив'язки при старті програми і в меню користувачів («All user profiles are already linked»); логіку переведено на канонічний IPC-механізм Horizon, аналогічний Linkalho: виклик команди 250 на `acc:su` (`GetBaasAccountAdministrator`) та перевірка команди 250 на отриманій службі адміністратора (`IsLinkedWithNintendoAccount`), а також отримання NAS ID через команду 120 (`GetNasId`); відтепер неприв'язані профілі точно визначаються як незв'язані (`LinkKind::None`), відображаються червоним індикатором «Not linked» у меню Users та коректно пропонуються для прив'язки через пул донорів RomFS при старті; оновлено `README.md`; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/account/account_link.cpp`:
   - У `QueryHorizonUserLink` замінено виклик команди 102 на команду 250 (`GetBaasAccountAdministrator`), на отриманій службі `admin` перевіряється статус `IsLinkedWithNintendoAccount` (команда 250). Якщо акаунт не прив'язаний (`!is_linked` або реєстрація потрібна), повертається `out_linked = false` та `out_kind = LinkKind::None`. Лише якщо `is_linked == true`, виконується перевірка кешу ID-токенів (`QueryIdTokenCache(&admin)`).
   - У `QueryNintendoAccountId` аналогічно використано `GetBaasAccountAdministrator` (команда 250 на `acc:su`), перевірку `IsLinkedWithNintendoAccount` (команда 250) та вичитування NAS ID через команду 120 (`GetNasId`).
   - У `QueryIdTokenCacheRaw` аргумент перейменовано на `Service* srv`.
2. Документація (`README.md`):
   - Оновлено підрозділ `User Profile Management` описом точного визначення прив'язки через `GetBaasAccountAdministrator` та `IsLinkedWithNintendoAccount`.
3. Підняти версію проекту до `0.13.782` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.781 — consistently center folder and file labels in icon layout

Статус: реалізовано; у макеті відображення іконок («Icon layout») файлового браузера та діалогу вибору файлів усунено небажане зсування тексту назв папок та файлів на лівий край при фокусуванні курсором; відтепер назви елементів завжди центруватимуться по горизонталі під іконкою плитки (`NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE` при `x + w / 2.f`) як у невиділеному стані, так і при виділенні/наведенні курсору; переповнення тексту плавно обтинається ножицями NanoVG; оновлено `README.md`; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/ui/menus/filebrowser/filebrowser_view.cpp`:
   - У лямбді `draw_name` для `icon_grid` прибрано перехід на ліве вирівнювання (`NVG_ALIGN_LEFT`) та виклик `m_scroll_name.Draw` при `selected == true`.
   - Забезпечено постійне центрування назви (`NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE`) у точці `x + w / 2.f` під контуром папки чи файлу з маскою `nvgIntersectScissor` як для виділеного, так і для невиділеного станів.
2. У `sphaira/source/ui/menus/file_picker.cpp`:
   - Уніфіковано відмальовку назв у режимі `icon_grid`: прибрано зсування на лівий край при `selected == true`, назва завжди відмальовується по центру плитки з підсвічуванням кольором `text_id`.
3. Документація (`README.md`):
   - Оновлено підрозділ `Display Layouts` (пункт `Grid & Icon Views`) з описом стабільного центрування назв у макеті іконок.
4. Підняти версію проекту до `0.13.781` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.780 — diagnostic firmware, target translation tag and release URLs preview in interface translation menu

Статус: реалізовано; у меню «Translate Interface» реалізовано прозоре діагностичне відображення версії системного ПЗ консолі, визначеного цільового тегу перекладу та точних GitHub URL джерел до початку будь-якого завантаження; у заголовку меню постійно відображаються TitleStats («FW ...» та цільовий тег); додано перший діагностичний пункт «Console Firmware» із детальним модальним вікном (прошивка, регіон, теги релізу/метаданих та повні посилання); для дії «Load/Refresh translations» додано попереднє інформування з HoldConfirm та показ точних URL у повідомленнях передачі ProgressBox; для мовних елементів виведено прямий URL архіву в опис/підзаголовок, діалог встановлення розширено повною діагностикою (прошивка, тег, варіант заміни `replaces_...`, URL), а при відсутності прямого збігу з поточною мовою/регіоном надано вибір серед усіх доступних варіантів заміни; у `translation_policy.hpp` додано інлайн-хелпери `GetReleaseUrl` та `GetMetadataUrl` з юніт-тестами; оновлено `README.md`; compile/tests/NRO не запускаються за policy.

1. У `sphaira/include/ui/menus/settings/translation_policy.hpp`:
   - Додано допоміжні функції `GetReleaseUrl(const std::string& target_tag)` та `GetMetadataUrl(const std::string& metadata_tag)` для централізованої побудови повних URL релізів та метаданих.
2. У `sphaira/source/ui/menus/settings/settings_translations.cpp`:
   - У `FetchAndCacheTranslations` та `InstallInterfaceTranslation` деталізовано виклики `pbox->NewTransfer` та `DownloadFile` із включенням цільових тегів та точних URL-адрес завантаження.
3. У `sphaira/source/ui/menus/settings/settings_translate.cpp`:
   - У конструкторі `TranslateMenu` та `OnFocusGained` додано виклик `SetTitleStats("FW " + fw, compat.available ? compat.target_tag : "Unsupported")`.
   - У `BuildTranslateItems` першим елементом додано пункт діагностики «Console Firmware: <fw>» із показом відповідності тегу в описі та викликом детального модального вікна з усіма параметрами (прошивка, регіон, цільовий тег, тег метаданих, release URL, metadata API URL).
   - У дію «Refresh / Load translations» додано опис із цільовим тегом і посиланням та захисне вікно HoldConfirmBox із попереднім переглядом параметрів.
   - Для кожної мови в списку опис призначено на повний `entry.zip_url` (відображається в підзаголовку меню при навігації), а значення — на ім'я архіву `FileNameFromUrl`.
   - При відсутності прямого збігу регіону/мови консолі користувачеві надається список усіх доступних замін замість блокування; у фінальному вікні підтвердження `HoldConfirmBox` виводиться повна діагностика: прошивка, тег, папка заміни `replaces_...`, ім'я архіву та URL перед стартом встановлення й перезавантаженням.
4. У `tests/test_translation_policy.cpp`:
   - Додано тест `test_urls()` для перевірки формування посилань релізів та метаданих.
5. Документація (`README.md`):
   - Додано новий розділ `## Interface Translation & Diagnostics` із описом діагностики сумісності, відображення джерел та гнучкого вибору мов заміни.
6. Підняти версію проекту до `0.13.780` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

Статус: реалізовано; у вбудованому переглядачі зображень файлового браузера реалізовано поворот перегляду зображення на льоту кнопками L (проти годинникової стрілки, -90°) та R (за годинниковою стрілкою, +90°); поворот здійснюється суто на рівні рендерингу (трансформація NanoVG) без зміни файлу, його даних чи атрибутів на диску; динамічно оновлюються ефективні розміри та межі панорамування/масштабування в'юпорта; додано локалізацію ключа "Rotate" для всіх 14 мов; оновлено `README.md`; compile/tests/NRO не запускаються за policy.

1. У `sphaira/include/ui/menus/file_viewer.hpp`:
   - Оголошено метод `void RotateImage(s64 direction);`.
   - Додано стан кута повороту `s64 m_rotation{0};` (0: 0°, 1: 90°, 2: 180°, 3: 270°).
2. У `sphaira/source/ui/menus/file_viewer/file_viewer_image.cpp`:
   - У `Menu::LoadImageFile()` прив'язано дії `Button::L` (із підказкою `"\uE0E4 / \uE0E5"` та текстом `"Rotate"_i18n`) для виклику `RotateImage(-1)` та `Button::R` для виклику `RotateImage(1)`.
   - У `Menu::FreeImage()` скидається `m_rotation = 0`.
   - Реалізовано `Menu::RotateImage(s64 direction)`: перевіряє активність перегляду зображення, блокує випадковий поворот при затиснутому зум-тригері `Button::L2`, інкрементує/декрементує стан `m_rotation`, скидає в'юпорт `m_viewport.Reset()` для адаптації під нове співвідношення сторін та викликає `UpdateImageAAction()`.
3. У `sphaira/source/ui/menus/file_viewer.cpp`:
   - У `Menu::LoadCurrentFile()` додано скидання `m_rotation = 0`.
   - У `Menu::Update()` для переглядача зображень обчислюються ефективні габарити `eff_w`/`eff_h` (міняються місцями при повороті на 90°/270°) та передаються у `m_viewport.Update()` для коректного панорамування й обмеження меж.
   - У `Menu::Draw()` реалізовано рендеринг повернутого зображення через локальну систему координат NanoVG (`nvgTranslate`, `nvgRotate`, `nvgImagePattern`, `nvgRoundedRect`, `nvgFillPaint`) під маскою скролінгу/ножиць (`nvgIntersectScissor`) без виходу за межі екрана.
4. Локалізація (`assets/romfs/i18n/*.json`):
   - Додано ключ перекладу `"Rotate"` до всіх 14 мовних файлів.
5. Документація (`README.md`):
   - Додано опис керування переглядачем зображень та повороту кнопками L/R без зміни файлу на диску.
6. Підняти версію проекту до `0.13.779` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.778 — move file browser layout setting into view options submenu

Статус: реалізовано; у файловому браузері вибір макета відображення («Layout»: «List» / «Icon») перенесено з верхнього рівня бічної панелі опцій («File Options») у вкладене підменю налаштувань відображення «View» («View Options») поруч із сортуванням, порядком та параметрами видимості елементів; з `DisplayOptions` видалено дубльований виклик `view_entry->SetHasSubmenu(true)`; оновлено `README.md`; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/ui/menus/filebrowser/filebrowser_options.cpp`:
   - Видалено пункт «Layout» із початку головної панелі `FsView::DisplayOptions()` («File Options»).
   - Додано пункт вибору макета «Layout» (з іконкою `ActionIcon::Layout`) на початок субменю `View` («View Options») із викликом `m_menu->SetIconLayout(index_out)`.
   - Прибрано дублювання виклику `view_entry->SetHasSubmenu(true)`.
2. Документація (`README.md`):
   - Оновлено розділ `File Browser & Vector Iconography` з описом розташування налаштування Layout у підменю View Options.
3. Підняти версію проекту до `0.13.778` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.777 — dynamic minus button navigation to homebrew screen or app exit

Статус: реалізовано; кнопку Minus переведено на інтелектуальну навігацію з перевіркою поточного екрана: якщо користувач уже на головному екрані (екран Homebrew), кнопка Minus закриває програму (`App::Exit()`); якщо кнопка натиснута в будь-якому іншому місці програми (вкладка Tools у MainMenu, меню налаштувань, файловий браузер, бічні панелі/опції або підменю), вона миттєво відкриває головний екран Homebrew; compile/tests/NRO не запускаються за policy.

1. У `Widget` (`sphaira/include/ui/widget.hpp`):
   - Додано віртуальні методи `virtual auto IsMainScreen() const -> bool` (за замовчуванням `false`) та `virtual void OpenMainScreen()`.
2. У `MainMenu` (`sphaira/include/ui/menus/main_menu.hpp` та `sphaira/source/ui/menus/main_menu.cpp`):
   - Реалізовано `IsMainScreen() const`: повертає `true`, якщо активне меню `m_current_menu` дорівнює `m_centre_menu.get()` (екран Homebrew).
   - Реалізовано `OpenMainScreen()`: перемикає вкладку на `m_centre_menu.get()` (`SwitchTo(m_centre_menu.get())`).
   - У конструкторі дію `Button::SELECT` переведено з безумовного `App::Exit` на `App::HandleMinus`.
3. У `App` (`sphaira/include/app.hpp` та `sphaira/source/app.cpp`):
   - Додано статичні методи `IsMainScreen()`, `OpenMainScreen()` та `HandleMinus()`.
   - `IsMainScreen()`: перевіряє, що розмір стека віджетів дорівнює 1 (відсутні оверлеї/субменю) та кореневий віджет `MainMenu` перебуває на екрані Homebrew.
   - `OpenMainScreen()`: позначає всі дочірні віджети стека вище кореневого через `SetPop()` і перемикає `MainMenu` на Homebrew.
   - `HandleMinus()`: якщо `IsMainScreen() == true`, викликає `App::Exit()`, інакше викликає `OpenMainScreen()`.
4. Уніфікація обробників кнопки Minus:
   - У `MenuBase` (`sphaira/source/ui/menus/menu_base.cpp`) дефолтну дію `Button::SELECT` змінено з `App::Exit` на `App::HandleMinus`.
   - У `settings_fancurve.cpp`, `filebrowser.cpp`, `file_picker.cpp` та `sidebar.cpp` дію кнопки Minus переведено на `App::HandleMinus`.
5. Документація (`README.md`):
   - Додано опис нової логіки кнопки Minus у підрозділ швидкої навігації.
6. Підняти версію проекту до `0.13.777` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.776 — single-prompt concise notification on incomplete TegraExplorer restore

Статус: реалізовано; у `sphaira/source/ui/menus/users/users_restore.cpp` (`OfferPendingRestore`) для фази `wait_nand_restore` у разі відсутності маркера завершення TE додано переведення pending стану в `applied` через `SavePending`, завдяки чому сповіщення показується лише один раз і більше не спливає при наступних запусках програми; з модального повідомлення прибрано зайвий текст «If the console will not boot...», оскільки консоль уже успішно завантажена; додано ключ перекладу до 14 мовних файлів `assets/romfs/i18n/*.json`; оновлено `README.md`; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/ui/menus/users/users_restore.cpp`:
   - У блоці перевірки `pending.phase == "wait_nand_restore"` у гілці `else` додано збереження `account_restore::SavePending(pending.pack_dirs, "applied", pending.snapshot_ok)` перед показом `OptionBox`.
   - Текст повідомлення спрощено до чистого лаконічного статусу `"TegraExplorer did not finish restoring profiles & play hours."_i18n` без нерелевантного для вже запущеної системи блоку інструкцій щодо невдалого завантаження консолі.
   - У гілці `pending.phase == "wait_link"` в `else` також додано `SavePending(pending.pack_dirs, "applied", pending.snapshot_ok)` для запобігання аналогічного циклічного показу.
2. Локалізація (`assets/romfs/i18n/*.json`):
   - Додано ключ `"TegraExplorer did not finish restoring profiles & play hours."` до всіх 14 мовних файлів з відповідними перекладами.
3. Документація (`README.md`):
   - Оновлено підрозділ `User Profile Management` із зазначенням одноразового інформування про незавершений рестор у TegraExplorer.
4. Підняти версію проекту до `0.13.776` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.775 — eliminate interior tab overlap from vector folder icon

Статус: реалізовано; у `sphaira/source/ui/file_icon.cpp` (`StrokeFolder`) ліквідовано паразитне внутрішнє перекриття та лінію під язичком папки; арка язичка малюється як відкритий контур, що спирається на верхнє ребро тіла папки (`top_body`), а тіло папки формує завершений прямокутник зі скругленими кутами та суцільним верхнім горизонтальним роздільником; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/ui/file_icon.cpp`:
   - У функції `StrokeFolder` відокремлено відмальовку верхньої арки язичка від прямокутника тіла.
   - Арка язичка починається в точці `(left, top_body)`, плавно огинає кути `(left, top_tab)` та `(tab_right, top_tab)` радіусами `r` та `r_tab`, і спускається вертикально до `(tab_right, top_body)`. Вона не заходить углиб папки та не містить нижньої замикаючої лінії.
   - Тіло папки малюється через `nvgRoundedRectVarying` із суцільною верхньою гранню на `top_body`, нульовим верхнім лівим радіусом (для безшовного вертикального переходу лівого краю язичка у лівий край тіла) та скругленням решти трьох кутів радіусом `g.radius`.
2. Документація (`README.md`):
   - Оновлено опис векторної іконографіки папок у підрозділі File Browser.
3. Підняти версію проекту до `0.13.775` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.774 — prompt reboot after setting user profile avatar

Статус: реалізовано; у меню Tools -> Users після зміни аватарки профілю користувача додано запит на перезавантаження консолі (OptionBox) з поясненням, що новий аватар набуде чинності після перезавантаження, та можливістю перезавантажитися зараз або пізніше; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/ui/menus/users/users_profile.cpp`:
   - У `Menu::RunSetAvatar(std::vector<u8> jpeg)` після успішного запису аватара `account_user::SetImageJpeg(uid, jpeg)` та оновлення інтерфейсу `Refresh()` додано показ модального діалогу `App::Push<OptionBox>`.
   - Текст повідомлення: `"Avatar changed.\n\nThe change will not take effect until the console is rebooted.\n\nReboot now?"_i18n`.
   - Опції вибору: `"Later"_i18n` (індекс 0) та `"Reboot"_i18n` (індекс 1, фокус за замовчуванням).
   - При виборі "Reboot" викликається `utils::requestForcedReboot()`. При виборі "Later" або закритті кнопкою B користувач залишається у меню.
2. Локалізація (`assets/romfs/i18n/*.json`):
   - Додано рядок `"Avatar changed.\n\nThe change will not take effect until the console is rebooted.\n\nReboot now?"` до всіх 14 мовних файлів із відповідними перекладами.
3. Документація (`README.md`):
   - Додано розділ `User Profile Management` із описом налаштування аватарів та модального запиту на перезавантаження.
4. Підняти версію проекту до `0.13.774` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.773 — fix minimized install touch badge compile error and verify WSL build

Статус: реалізовано; виправлено звернення до полів `TouchInfo` у `sphaira/source/app.cpp` для тапу по міні-бейджі згорнутого встановлення; повна компіляція NRO (`build/wsl_build.sh`) під Nintendo Switch та набір хост-тестів (`tests/run.sh`) у WSL успішно пройдені.

1. У `sphaira/source/app.cpp`:
   - Виправлено перевірку сенсорного дотику до міні-бейджів активної інсталяційної сесії та мінімізованого віджета: замінено неіснуючі поля `m_touch_info.finger_down`, `m_touch_info.x`, `m_touch_info.y` на коректний API `TouchInfo`: `m_touch_info.is_clicked && m_touch_info.in_range(Vec4(bx, by, bw, bh))`.
2. Верифікація збірки:
   - Проведено повну збірку Nintendo Switch NRO (`kefir-hub.nro`) через `build/wsl_build.sh` у середовищі WSL з результатом 100% успіху.
   - Виконано паралельний запуск усіх хост-тестів та перевірок (`tests/run.sh`): усі тести та перевірки мертвих символів/патчів пройдені ("all green").
3. Підняти версію проекту до `0.13.773` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.772 — direct firmware-matched interface translations

Статус: реалізовано; пряма взаємодія з GitHub API NX-Family/NX-Translation за точним тегом релізу, метадані api.json із фолбеком на FW17.0.1-TR1.18 для FW16.1.0-TR1.09 та FW17.0.0-TR1.11, підтримка modern та legacy схем найменування архівів і коректна резолюція шляхів розпакування; compile/tests/NRO не запускаються за policy.

1. Застосовано прямий запит точного релізу GitHub API за адресою `https://api.github.com/repos/NX-Family/NX-Translation/releases/tags/<target-tag>` без завантаження та перебору списку релізів.
2. Відокремлено `target_tag` (реліз ZIP-архівів) та `metadata_tag` (реліз для завантаження `api.json`). Для старих версій FW16.1.0-TR1.09 та FW17.0.0-TR1.11 метадані беруться з FW17.0.1-TR1.18, тоді як наявність мов суворо визначається фактичними асетами цільового релізу.
3. Додано детерміноване розпізнавання назв архівів для modern (`NX-Translation_<id>.zip`) та legacy (`TR1.09_<id>_FW16.1.0.zip` / `TR1.11_<id>_FW17.0.0.zip`) схем найменування.
4. У `TranslationExtractFolder` реалізовано коректне визначення папки розпакування для legacy архівів (наприклад, `TR1.09_ukrainian_FW16.1.0.zip` -> `ukrainian_FW16.1.0`) зі збереженням поведінки modern архівів (`NX-` <-> `Nx-`).
5. Посилено надійність кешування: перевірка помилок створення директорій `EnsureParentDirectory`, валідація створення та запису yyjson документа, безпечна перевірка рядкових типів `yyjson_is_str` у `LoadTranslationsCache` з ігноруванням пошкоджених записів.
6. Хост-тест `tests/test_translation_policy.cpp` розширено перевірками `metadata_tag`, нормального `same-tag`, визначення папок розпакування legacy та modern архівів і збереження всіх меж сумісності прошивок.

## Попередній delivery: v0.13.771 — USB install Minimize / Expand and dedicated origin identification

Статус: реалізовано; для USB встановлення додано виокремлений `TransportOrigin::Usb`, розширено дію Minimize/Expand (кнопка L3) на всі стани очікування та перегляду черги, додано підказку " Expand" та тач-розгортання на міні-бейджі; compile/tests/NRO не запускаються за policy.

1. У `include/ui/menus/dbi/install_queue_state.hpp`:
   - Додати значення `Usb` до переліку `TransportOrigin`.
   - Включити `TransportOrigin::Usb` у функцію `HasKnownBatchTotals`.
2. У `sphaira/source/ui/menus/dbi_menu.cpp`:
   - Ініціалізувати `Menu::Menu(u32 flags)` (PC Install over USB) із `TransportOrigin::Usb`.
   - Додати дію `Button::L3` ("Minimize" / "Expand") у стани очікування USB/списку (`State::WaitingForUsb`, `State::WaitingForList`, `State::Analysing`) та у меню перегляду черги (`State::ReviewQueue`).
   - У `ComputeSaverInfo` додати відображення статусу `"USB Install"_i18n` для `TransportOrigin::Usb`.
3. У `sphaira/source/ui/menus/dbi/dbi_draw.cpp`:
   - У `DrawMiniBadge` відображати origin як `"USB"`, адаптувати відображення для станів очікування та додати індикатор розгортання ` Expand` (з іконкою стика L3 `\uE104`).
4. У `sphaira/source/app.cpp`:
   - Додати підтримку розгортання сесії або віджета торканням (touch tap) по міні-бейджі у правому верхньому кутку екрана.
5. У `tests/test_transport_install_queue.cpp`:
   - Додати перевірки для `TransportOrigin::Usb` у юніт-тести.
6. Оновити `README.md` із описом згортання та розгортання встановлення.
7. Підняти версію проекту до `0.13.771` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.770 — disable B and map X to Cancel installation for FTP/MTP/HTTP

Статус: реалізовано; для MTP, FTP та Web/HTTP кнопка B вимкнена під час інсталяції, кнопка X відповідає за Cancel installation із підтвердженням; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/ui/menus/dbi_menu.cpp` (`InstallSession::UpdateActions`):
   - Для стрімінгових джерел (`TransportOrigin::Mtp`, `TransportOrigin::Ftp`, `TransportOrigin::Web`) під час стану `State::Installing`:
     - Дія кнопки `X` призначається як `"Cancel installation"_i18n` із модальним запитом `"Cancel installation?"_i18n`.
     - Дія кнопки `B` повністю відсутня (не реєструється ані `"Skip package"`, ані `"Done"`), оскільки потокові мережеві та USB протоколи не підтримують безпечний пропуск пакета на льоту без розриву з'єднання.
     - Для fallback стану кнопки за замовчуванням `X` призначається `"Cancel installation"`, а `B` не використовується.
   - Для черги локального встановлення (DBI) збережено дію `X` як `"Cancel queue"` та `B` як `"Skip package"` / `"Done"`.
2. Додати ключ `"Cancel installation?"` у всі 14 локалізацій `assets/romfs/i18n/*.json`.
3. Оновити `README.md` з роз'ясненням керування кнопками для потокових інсталяцій.
4. Підняти версію до `0.13.770` у `sphaira/CMakeLists.txt`, оновити `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.769 — Fix vector folder icon outline and remove interior tab overlap

Статус: реалізовано; контур папки переведено на єдиний суцільний векторний шлях без перекриття язичка і тіла; compile/tests/NRO не запускаються за policy.

1. У `sphaira/source/ui/file_icon.cpp` (`StrokeFolder`):
   - Усунути роздільне малювання двох прямокутників (`nvgRoundedRectVarying` язичка та `nvgRoundedRect` тіла папки), яке призводило до заїзду язичка всередину тіла папки та утворення подвійної горизонтальної лінії/перекриття.
   - Сформувати єдиний замкнений векторний контур (`nvgMoveTo`, `nvgArcTo`, `nvgLineTo`, `nvgClosePath`): лівий край, плавне скруглення язичка вгорі, опускання правого краю язичка до верхньої межі тіла папки та плавний перехід у верхню грань і контур тіла без внутрішніх ліній перекриття.
2. Оновити `README.md`: додати опис уніфікованих векторних іконок папок у секцію файлового браузера.
3. Підняти версію до `0.13.769` у `sphaira/CMakeLists.txt`, синхронізувати `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.768 — Fix menu list selection frame draw priority and z-order

Статус: реалізовано; z-order виправлено шляхом відокремлення шару фонів неактивних пунктів від шару виділеного активного елемента.

1. У `ConsoleTransferMenu::Draw` (`sphaira/source/ui/menus/install_share.cpp`) розділити відмальовку на два логічні проходи:
   - Прохід 1: малювання всіх неактивних пунктів списку (`DrawElement(v, ThemeEntryID_GRID)` та звичайні тексти міток).
   - Прохід 2: малювання активного пункту з рамкою виділення `gfx::drawRectOutline(vg, theme, 4.f, v)` та виділеним текстом поверх усього списку.
2. Застосувати аналогічний двохпрохідний порядок відмальовки для списків у `users_manage.cpp` та `users_restore.cpp` для запобігання перекриттю рамки фокусу фонами сусідніх елементів.
3. Оновити версію проекту до `0.13.768` у `sphaira/CMakeLists.txt`, синхронізувати плани `task.md`, `plan.md`, `audit.md`, `walkthrough.md`.

## Попередній delivery: v0.13.767 — Transport-specific install success notifications and i18n decoupling

Статус: реалізовано, host-тести у WSL (`./tests/run.sh`) та Switch NRO build (`make build`) успішно пройдені.

1. Розділити повідомлення про успішне встановлення за типом джерела (origin): у `BackgroundInstaller::OnInstallStart` (`install_stream_menu_base.cpp`) замість жорсткого `App::Notify("Install success!"_i18n)` надсилати відповідне транспорту повідомлення:
   - для `TransportOrigin::Mtp` -> `"MTP install success!"_i18n`
   - для `TransportOrigin::Ftp` -> `"FTP install success!"_i18n`
   - для `TransportOrigin::Web` -> `"Web install success!"_i18n`
   - за замовчуванням -> `"Install success!"_i18n`
2. Очистити загальний ключ `"Install success!"` у каталогах перекладів (`ru.json`, `es.json`, `ja.json`, `ko.json`, `zh.json`), де він історично містив жорстку згадку FTP, зробивши його нейтральним.
3. Додати ключі `"MTP install success!"`, `"FTP install success!"`, `"Web install success!"` до всіх 14 мовних файлів `assets/romfs/i18n/*.json`.
4. Версія `0.13.767`; перевірка повної збірки NRO `make build` у WSL та хост-тестів `./tests/run.sh`.

## Попередній delivery: v0.13.766 — MTP batch installation summary grace period and transfer sync

Статус: реалізовано, host-тести у WSL успішно пройдено (`./tests/run.sh`).

1. Усунути передчасний перехід у Summary під час пакетної передачі файлів через MTP: прибрати помилковий виклик `TransitionToSummary` з робочого потоку `BackgroundInstaller::OnInstallStart`, коли черговий файл щойно завершився, але наступні файли ще надходять з ПК.
2. Ввести 3-секундний пільговий період (grace period) у `InstallSession::Update()` для джерел без попереднього маніфесту (MTP, FTP): перехід у Summary відбувається лише тоді, коли всі поточні пакети завершені, інсталятор вільний і відсутні активні передачі або черги в транспорті (`haze::HasActiveTransfer()`, `ftpsrv::HasActiveOrQueuedFiles()`).
3. Динамічна дія кнопки `B` ("Done"_i18n): під час дії пільгового періоду користувач може натиснути кнопку `B` для миттєвого переходу до підсумків (Summary), або дочекатися автоматичного переходу через 3 секунди бездіяльності.
4. Миттєве закриття при відключенні MTP: якщо сесія MTP закривається ПК (`CallbackType_CloseSession`), активна інсталяційна сесія переходить у Summary без затримки, якщо всі поточні пакети термінальні.
5. Потокобезпека: захистити доступ до `g_shared_data.current_file` м'ютексом `g_shared_data.mutex` у `haze_install_proxy.cpp`.
6. Оновлення тестів: додати тести у `tests/test_transport_install_queue.cpp` для `SUMMARY_GRACE_PERIOD_SEC`, `ShouldStartSummaryGracePeriod` та `CanTransitionToSummary` для MTP.
7. Версія `0.13.766`; перевірка через `./tests/run.sh` у WSL та `git diff --check`.

## Попередній delivery: v0.13.765 — MTP install button controls and stat row label spacing

Статус: реалізовано; compile/tests/NRO не запускаються за policy.

1. Уніфікувати футер-дії під час встановлення: в MTP інсталяторі призначити скасування на кнопку `X` ("Cancel installation" із підтвердженням "Cancel installation queue?"), а пропуск поточного файлу на кнопку `B` ("Skip package" із підтвердженням "Skip this package?").
2. У `DrawStatRow` (`dbi_internal.cpp`) додати шрифтовий пробіл між двокрапкою мітки та її значенням, ліквідувавши злипання міток зі значеннями (Mode, Installed, Average speed, Remaining, Skipped, Failed, Packages тощо) у вікнах MTP/USB/PC інсталяції та підсумків.
3. Додати ключ `"Cancel installation"` у всі 14 локалізацій `assets/romfs/i18n/*.json`.
4. Version `0.13.765`; compile/tests/NRO не запускалися за repository policy, потрібна апаратна перевірка користувачем.

## Попередній delivery: v0.13.764 — safe USB-unplug UI teardown

Статус: реалізовано Gemini й прийнято після senior review; compile/tests/NRO не запускалися за policy, потрібна апаратна перевірка.

1. Після USB mass-storage removal позначити `SetPop()` не лише на matching File Browser, а й на весь залежний widget subtree над ним.
2. Не викликати `OnFocusGained()` до фактичного top-down pop, щоб USB-backed `FsView` не сканував вже видалений mount.
3. Повторно використати чинні `ShouldPop()` / `SetPop()` і звичайний App pop-loop; не додавати lifecycle manager, RTTI, новий virtual API чи cancellation framework.
4. Version `0.13.764`; верифікація — Atmosphère crash-report analysis, повний UI-stack/caller review і `git diff --check`, без compile/tests/NRO.

## Попередній delivery: v0.13.763 — fresh initial Auto plan and split queue options

Статус: реалізовано Gemini й прийнято після senior review; compile/tests/NRO не запускалися за policy, потрібна апаратна перевірка.

1. На кожному переході в `ReviewQueue` форсовано оновити storage snapshot до першого Auto-плану, щоб правильний NAND/microSD розподіл не залежав від 15-секундного cache cadence або зміни сортування.
2. Зміна install location чи NAND/microSD reserve в sidebar одразу переплановує review-чергу.
3. У sidebar зверху показати секцію install options; секцію `VIEW` із sort/order перенести вниз.
4. Не змінювати `PlanPickSd`, packing order, target colour/legend із `v0.13.762` або install execution; version `0.13.763`. Верифікація — source-level review усіх переходів/callers і `git diff --check`, без дублювання Horizon/NanoVG runtime у host-test.

## Попередній delivery: v0.13.762 — stable install storage legend and target colours

Статус: реалізовано Gemini й прийнято після senior review; compile/tests/NRO не запускалися за policy, потрібна апаратна перевірка.

1. У projection mode обидва storage-рядки завжди показують однакові три поля: `+пакет у фокусі / уся запланована черга на носій / фактично вільно`, включно з `0 B` для невикористаного носія.
2. Прибрати дубльовані `microSD`/`NAND` planned totals зі stat-row черги: ті самі totals уже видно в storage header.
3. Розрізнити цілі без нової палітри: microSD використовує наявний `HIGHLIGHT_1`, NAND — `HIGHLIGHT_2`; Auto лишається двоколірним (`Auto` нейтральний, resolved destination кольоровий), pinned target — суцільно кольоровим.
4. Не змінювати `RecomputePlan()`, sorting, reserve semantics або install execution; version `0.13.762`. Окремий host-test не додавати: форматування й малювання локальні для NanoVG UI, а дубль production-логіки в тесті не перевіряв би реальний renderer.

## Попередній delivery: v0.13.761 — version correction for install queue delivery

Статус: виконано локально; функціонал `v0.13.760` без змін, compile/NRO не запускалися.

1. Підняти фактичний `sphaira_VERSION` з `0.13.760` до `0.13.761`.
2. Не змінювати сортування, Auto planner, storage header або i18n з `v0.13.760`.
3. Синхронізувати living docs і створити focused commit.

## Попередній delivery: v0.13.760 — install queue sorting and visible free space

Статус: реалізовано Gemini й прийнято після senior review; compile/NRO та host-тести не запускалися за policy, потрібна апаратна перевірка.

1. Додати в `Install Options` мінімальне сортування черги за назвою, розміром пакета та розрахованим install size, з ascending/descending order; зберегти фокус на тому самому пакеті.
2. Не дублювати `Auto`: local/file-manager/external USB і PC USB мають залишитися на спільному `RecomputePlan()` / `RefreshAutoInstallTarget()` і давати однаковий результат для однакових budget/order.
3. У review header зберегти `+файл / уся черга`, але після слеша додати фактичне вільне місце на відповідному NAND/microSD (`+файл / черга / вільно`), не плутаючи це з reserve-adjusted usable space.
4. Додати одну мінімальну host-перевірку для sort/order та Auto parity; version `0.13.760`.

## Попередній delivery: v0.13.759 — repair and complete transport install UI

Статус: реалізовано після hardware feedback; compile/NRO не запускалися за policy, потрібна апаратна перевірка.

1. Виправити спільну причину бірюзового екрана у PC USB, local/file-manager і external USB install: blocking widget пропускає лише нижні шари, але завжди малює себе.
2. Detached MTP/FTP/Web `InstallSession` малює штатний header/footer поверх body; underlying panels не occlude його chrome, а реальна modal box тимчасово володіє footer.
3. MTP/FTP без наперед відомого manifest не показують вигадані `package X/Y` та overall total; показують mode, installed, written, current remaining, speed/ETA. DBI/Web з відомим batch зберігають повні totals. Screensaver MTP показує режим без package counter.
4. Session log показує start/install/cancel/failure/source disconnect/MTP restart. `B` у MTP скасовує install без непрацюючого Skip modal; cable interruption закриває detached overlay і перезапускає MTP після worker teardown.
5. Version `0.13.759`; source-level `git diff --check`, dead-symbol check і production helper host-test coverage; без compile/NRO.

## Попередній delivery: v0.13.758 — serialize background MTP batch installs

Статус: реалізовано Gemini й прийнято після senior race/control-flow review. Успішно зібрано через make (16 потоків) у WSL, виправлено супутні помилки компіляції transport/queue; всі тести пройдені паралельно (tests/run.sh); згенеровано sphaira_nro; потрібна апаратна перевірка batch copy з Windows.

1. Background MTP `OnInstallStart` для наступного файла тієї самої `InstallSession` чекає завершення попереднього worker замість `Install aborted`.
2. `s_installing.compare_exchange_strong` атомарно видає єдиний install slot; same-origin contender повторює wait/revalidation, cross-origin або unrelated transfer відхиляється.
3. Extension validation виконується до wait; cancel/session teardown/App exit переривають polling. `OnInstallClose`, stream backpressure, queue UI, FTP/Web semantics не змінені.
4. Version `0.13.758`; виправлено супутні помилки компіляції (Widget/ProgressBox overrides, stream header syntax, drawRectOutline signature, timestamp methods, QueueEntry optional); успішно зібрано make у WSL в 16 потоків; tests/run.sh успішно пройдено.

## Попередній delivery: v0.13.757 — unified transport install queue UI

Статус: реалізовано й прийнято в primary checkout після Gemini implementation та senior corrective review. Збірку й host-тести агент не запускав за policy; потрібна апаратна перевірка.

1. Фонові встановлення MTP, FTP і Web показують той самий повноекранний сеанс черги з метриками, двома progress bars, R/W graph, NAND/microSD статусом і summary, що й DBI Backend; renderer не дублювати.
2. Minus у черзі запускає чинний install screensaver. Згортання лишається окремою дією та стає доступним і для звичайної DBI-черги; скрінсейвер і minimize працюють однаково для DBI/MTP/FTP/Web.
3. Черга показує всі відомі назви файлів. Web UI надсилає batch manifest до першого upload; FTP використовує вже наявну `queued_files`; MTP/FTP без manifest поповнюють UI лише коли файл стає відомим протоколу.
4. Cancel через MTP спочатку перериває install/stream, повністю викликає `haze::Exit()`, а після teardown знову викликає `haze::Init()` і `BackgroundInstaller::RegisterMtpCallbacks()`, якщо MTP лишається увімкненим. FTP/Web cancel не перезапускають MTP.
5. Зберегти stream backpressure/timeout, FTP serialization, Web HTTP responses, DBI Skip/Cancel semantics і install settings. Додати одну мінімальну host-перевірку чистої queue/transport-state логіки; version `0.13.757`.

## Попередній delivery: v0.13.756 — clean TegraExplorer NAND-dump progress

Статус: реалізовано й прийнято в primary checkout; потрібна апаратна перевірка разом із виправленим TegraExplorer. Збірку агент не запускає за політикою checkout.

1. `nand_transfer_dump_auto.te` не друкує окремий рядок для кожного файла: поточний `copy index/total name` перемальовується в одному фіксованому рядку, тому після заповнення екрана текст не накладається на заголовок.
2. Рядок лишається активним під час великого `PlayEvent.dat` через наявний spinner у виправленому TegraExplorer `saveObj.read()`; Sphaira не дублює chunked I/O і не додає залежностей.
3. RESULT, кольори, `dumped.ok`, skip `player.vend.dat`, known-tree paths, cleanup і reboot semantics не змінюються.
4. Version `0.13.756`; лише source-level перевірки без compile/NRO за policy. Окремий checkout TegraExplorer не редагується в цій Sphaira delivery.

## Попередній delivery: v0.13.755 — stable Kefir Hub forwarder Title ID

Статус: реалізовано й прийнято в primary checkout; потрібна апаратна перевірка. Збірку агент не запускає за політикою checkout.

1. Власний Kefir Hub HOME-форвардер має сталий `0x05C838DF22834000`; auto-install і ручний `Install Title Mode forwarder` передають цей самий explicit ID.
2. `OwoConfig::title_id` опційний: звичайні NRO/ROM-форвардери без override зберігають наявний SHA-256(`nro_path + args`) Title ID.
3. Запуск з раніше створеного власного `0x05…` Kefir Hub/Sphaira (`StaleOwn`) більше не ставить новий ID; Album/nxlink guard і legacy HBL cleanup без змін.
4. Version `0.13.755`; Gemini й senior підтвердили `git diff --check`, межі семи продуктових/тестових файлів, всі `OwoConfig` callers та вибір final TID. Compile/NRO не запускалися за policy.

## Попередній delivery: v0.13.754 — allow zero-identity BAAS placeholders

Статус: реалізовано й прийнято в primary checkout; потрібен повторний апаратний прогін. Збірку агент не запускає за політикою checkout.

1. Hardware `v0.13.753` довів: PM і BCAT/ACCOUNT/OLSC terminate успішні, RW-open `0010` успішний, BAAS/NAS dirs є; preflight зупинився на existing BAAS з zero embedded identity до rollback/мутації.
2. У global BAAS collision scan `file_nas == 0` більше не є fatal: validated incoming NAS завжди nonzero, тому zero placeholder не може конфліктувати й пропускається.
3. Unreadable/короткий BAAS і nonzero incoming collision лишаються fail-closed. Zero BAAS не видаляється preflight-ом; existing target-specific filename loop видаляє/замінює лише BAAS цільового UID.
4. Version `0.13.754`; persistent `v0.13.753` diagnostics збережені для hardware retest. Gemini й senior підтвердили `git diff --check`, scoped two-file diff і full preflight/replacement flow; compile/NRO не запускалися за policy.

## Попередній delivery: v0.13.753 — persistent account-link diagnostics

Статус: реалізовано й прийнято в primary checkout; потрібен один апаратний прогін. Збірку агент не запускає за політикою checkout.

1. Для `TerminateAccountDaemons` синхронно зберігати в `/config/kefir/errors.txt` Result ініціалізації PM та окремі Result завершення BCAT/ACCOUNT/OLSC.
2. `ApplyLinkPackages` синхронно фіксує несекретні milestones та Result для RW-open `0010`, BAAS enumeration/preflight, rollback-directory creation і Commit, щоб forced reboot не губив причину збою.
3. Не зберігати UID, NAS identity, donor identity, filenames, tokens, email чи payload; лише сталі stage labels, Result, booleans, sizes і counts. Link/validation/rollback/reboot семантику не змінювати.
4. Version `0.13.753`; Gemini й senior підтвердили `git diff --check`, 22 format strings, callers і diff boundary. Compile/NRO не запускалися за policy; collision/orphan гіпотезу до результату апаратного прогону не виправляти.

## Попередній delivery: v0.13.752 — single profiles + PlayData backup library

Статус: реалізовано й прийнято в primary checkout; апаратна перевірка відкладена користувачем. Збірку агент не запускає за політикою checkout.

1. У Users Options прибрати окрему секцію/дії `Backup user` і `Restore Backup`; єдиний доступ до бібліотеки — `Manage Backups` у секції комплексного переносу.
2. `Manage Backups` без проміжного вибору типу одразу відкриває наявний `NandPackLibraryMenu` для `/config/kefir/nand_transfer` (усі профілі + PlayData) і використовує наявний callback `RunNandRestore`.
3. Прибрати з доступного NAND backup/restore UI згадки «Not the same as Backup user» і «For one user use Restore Backup»; en/uk/ru та `docs/account-transfer.md` описують тільки актуальний комплексний шлях.
4. Legacy individual-backup/pending-restore backend і формати цього delivery не видаляти: прибирається доступ із меню, без ризикового переписування recovery-flow або даних користувача.
5. Version `0.13.752`; без compile/NRO за policy. Gemini й senior підтвердили case-sensitive JSON parse, `git diff --check`, caller/string grep, прямий маршрут `Manage Backups` → `ConfirmNandRestore` → `OpenNandPackLibrary` та нуль змін у Nintendo Account/donor коді.

## Попередній delivery: v0.13.751 — unique embedded donor pool

Статус: реалізовано й прийнято в primary checkout; апаратна перевірка на HOS 18.1.0 очікується. Збірку агент не запускає за політикою checkout.

1. З SD `F:/config/kefir/account_backups` взяти останню чітку серію з 8 ZIP (2026-09-05 16:39:44–46): кожен має 80-byte BAAS, complete core NAS, valid JWT structure/`sub` matches і унікальні NAS/email. Вшити їх як opaque indexed donors без profile/avatar/playtime та без optional `_aux.dat` / `_op2membership.dat`; старий root Kefir donor зберегти. Разом 9 унікальних packages, використовується до 8.
2. `LoadRomfsDonorPackage` розширити до pool loader з простим top-level manifest: кожен package проходить наявну BAAS/NAS/token validation; дубль `nas_id` в пулі відхиляє весь pool.
3. `LinkAllFromRomfsDonor` стабільно призначає кожному `linked_known && !horizon_linked` профілю окремого невикористаного donor; donors, які вже доведено належать live profile, пропускаються. Нестача donor або невідомий link status — fail до terminate/write.
4. `ApplyLinkPackages` до `TerminateAccountDaemons` валідує також unique target UID і unique incoming NAS. Після RW open, але до rollback/першої мутації, fail-closed відхиляє нечитабельний, короткий або zero-NAS BAAS та incoming NAS, якщо його BAAS належить іншому UID/сироті; чужий donor не переноситься й не видаляється.
5. Users і launch callbacks: failure після daemon termination → forced reboot; preflight failure до terminate → звичайна помилка без reboot. UI/i18n/docs говорять про пул і one donor per profile, не про one shared donor.
6. Safe validator для embedded pool не друкує identifiers/credentials. Version `0.13.751`; Gemini й senior підтвердили validator (9 packages), case-sensitive JSON parse, `git diff --check`, targeted caller/order/log review; senior також byte-exact звірив 8 нових payload-наборів з останньою серією SD. Compile/NRO не запускали за policy.

## Попередній delivery: v0.13.750 — cumulative NAND/microSD install progress

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Під час `State::Installing` активний рядок SysNAND/EmuNAND або microSD показує накопичувальний прогрес `written / total`: перше значення росте від `0 B`, друге лишається повним `PlanSize` поточного пакета; неактивний накопичувач як раніше показує вільне місце.
2. Новий вузький `SetStorageInstallProgress` відділяє текстовий прогрес від проєкції місткості: жовтий сегмент малює тільки `total - written`, тому зменшується без подвійного врахування вже записаних байтів і без проєкції решти черги.
3. `ReviewQueue` лишається на `SetStorageProjection` з `+focus / total`, а Games — на `SetStorageHighlight`; version `0.13.750`, нових i18n-рядків або залежностей немає.
4. Gemini виконав `git diff --check` і перевірку всіх storage-setter callers; senior перевірив повний diff, межі режимів і version bump. Компіляцію/NRO-тести не запускали за політикою checkout. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.749 — preserve BAAS payload during account linking

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. `ApplyLinkPackages` і `StageCreateLinkForTe` більше не підміняють перші 16 байтів BAAS локальним `AccountUid`: валідований донорський payload переноситься байт-у-байт, а UID цілі задається лише наявним UID-похідним ім’ям файлу.
2. `FindLiveUidByNasId` як і раніше перевіряє вбудований `nas_id` за зсувом `0x10`, але більше не десеріалізує `AccountUid` із байтів `0..15`; відповідність живому профілю доводиться тільки через `BaasCandidateNames` і case-insensitive filename match.
3. Оновлено опис інваріанта у `docs/account-transfer.md`, TE-коментарі та `account_restore.hpp`. Version `0.13.749`. Rollback, NAS cleanup, duplicate cleanup, commit і формат імен без змін.
4. Gemini виконав `git diff --check` і source-level grep; senior перевірив повний diff та всі залишкові виклики. Компіляцію/NRO-тести не запускали за політикою checkout. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.748 — delete linked users; backup all accounts before delete

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. `account_user::Delete`: перед `acc:su` DeleteUser — best-effort `GetBaasAccountAdministrator` (250) + `DeleteRegistrationInfoLocally` (203); ACCOUNT живий; без `UnlinkLinkedProfiles` / `UnregisterAsync` / terminate. Refuse last remaining profile (`Result_FsEmpty`).
2. Export без kill ACCOUNT у delete-flow: `ExportUserPacks` / `ExportUserLinkPackage` / `OpenAccountSaveForExport(may_terminate_account)`; delete-backup `false` (zip nick+avatar+playtime навіть якщо 0010 locked); `RunBackup` лишає `true` + reboot.
3. ConfirmDelete: після Hold A — backup **all** live profiles (Skip / Backup all accounts), потім saves question; `RunDelete` без account export; ProgressBox action `Deleting`, title = nick / `Delete user`, NewTransfer per nick + `Deleting saves`; UI+RunDelete guard «не всі профілі».
4. OptionBox one-button OK: A glyph `\uE0E0`. i18n en/uk/ru. Version `0.13.748`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.747 — nand pack date, nicknames, delete only from list

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Pack list title = human `created_label` (`FormatPackCreated` з folder stamp `YYYYMMDD_HHMMSS` → `"%02d.%02d.%04d, %02d:%02d"`); subtitle лишається accounts + play hours / profiles. `PackInfo.created_label` у `MakePackInfo`.
2. Pack detail: nickname з `profiles.dat` (UID 16B + UTF-8 nick at +0x28 у блоках 0xC8); overlay на jpg/baas UID (strip `-`, case-insensitive; match `%02X` bytes і `UidHexRaw` u64 form). UID лишається другим рядком. Title detail = `created_label` else `name`.
3. Delete whole pack лише зі списку (X/Y/Minus як 745). Detail: A = Restore, B = Back; прибрано SELECT Delete, PromptAction Delete pack, `ConfirmDelete` / `m_on_deleted`. Per-account nand edit **не** робимо.
4. Version `0.13.747`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.746 — TE dump: drop busy-wait RESET crash

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. `nand_transfer_dump_auto.te` `waitFive` був tight `while ((timer() - t0) < 5000) {}` — без періодичного minerva DRAM training це RESET BPMP; наступний TE boot показує orange Err 18 / E Reset.
2. Видалено `waitFive` повністю; після `printResult()` і в `failOut` одразу `goHekate()` (RESULT лишається на екрані до payload; auto reboot без busy-loop).
3. У `dumpFilesIn`: `player.vend.dat` → `skip slow` (не read/write; не помилка). Play hours лишаються на `PlayEvent.dat`.
4. Known-tree / copy idx/total / colors / RESULT / dumped.ok / goHekate cleanup / skip missing dirs без змін. Hub C++ / Manage Backups / restore не чіпали. Version `0.13.746`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.745 — nand pack list multi-select delete

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. `NandPackLibraryMenu` (stamp pack list): checkbox multi-select як ZIP Manage Backups — X toggle+advance, Y invert, B clears marks else pop, Minus delete, A opens pack detail.
2. Subheading `selected / total` коли є marks; title hint `A opens pack details. X marks backups. Minus deletes.`; draw checkbox + FOCUS tint 0.35; текст після checkbox (`text_x = v.x + 50`).
3. `ConfirmDelete`: selected dirs → `"Delete the selected backups..."`; else current → single-pack confirm; ProgressBox `DeleteDirectoryRecursively` per dir; fail → `"Could not delete the pack."`; success → `Refresh()` + `m_selected_count = 0`.
4. `NandPackDetailMenu` без checkbox / multi-restore nand packs **не** робимо. i18n en/uk/ru. Version `0.13.745`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.744 — dump progress, pack library, no sticky toast

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. TE dump: per-file `copy idx/total fname` перед blocking `read`; після RESULT / failOut — `waitFive()` (5s) потім `goHekate()` (без `pause()`). Known-tree / colors / cleanup / dumped.ok без змін.
2. Sticky Ultrahand toast: `LaunchTegraRomfs` більше не викликає `ArmReopenHubHint()`; `ClearReopenHubHint` також знімає `; kefir-hub-reopen-begin`…`end` з `boot_package.ini`.
3. Manage Backups: двопунктне меню — Backup user (ZIP) і Backup profiles & play hours (nand pack library).
4. Restore profiles: бібліотека паків замість folder picker; pack detail (акаунти read-only); вибір restore play hours; `restore_00F0` flag + TE skip 00F0. Per-account nand restore **не** робимо.
5. API: `nand_transfer::ListPacks` / `ListPackUsers`; UI `users_nand_library.cpp`. Version `0.13.744`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.743 — dump_auto deletes one-shot temp files

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Dump script лишав one-shot temps (`startup.te`, dump_auto copies, `dump_result.txt`) після RESULT/failOut; pack backup і Undo safety мають лишатися.
2. `nand_transfer_dump_auto.te` `goHekate`: перед payload — `Cleaning temp files` + delfile startup / dump_auto copies / dump_result (+ pack dump_result якщо pack відомий); bak→ipl restore як раніше; `pack=""` + `pending` ініціалізуються до будь-якого failOut. Pack folder, `dumped.ok`, nand flag, `nand_pack.txt`, Undo snapshots / Undo.te / reopen_hub.flag **не** чіпає TE.
3. Hub: після `wait_nand_dump` + `NandDumpLooksComplete` → `SavePending(..., applied)` потім `CleanDumpHandshake()` (dumped.ok, nand_pack.txt, startup.te, dump_auto copies). Без `ClearPending()`.
4. Version `0.13.743`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.742 — TE dump RESULT stats green/red

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Після dump TE 4.2.0 показував лише plain RESULT без per-save статистики й без кольорів; людина не бачила з першого погляду dumped/empty/missing/skip і OK vs NOT OK.
2. `nand_transfer_dump_auto.te`: known-tree dump без змін шляху; per-save `n0010/n0011/n00F0/n0041` і `s*` (0 missing / 1 dumped / 2 empty); `printResult` + `printSave` з `color()` — green dumped/OK, red required missing/empty + NOT OK, yellow optional skip/empty; `dumped.ok` лише при got0010+got00F0; потім Hub instruction, `pause()`, `goHekate()`.
3. Hub C++ / restore / dump.te menu / i18n не чіпали.
4. Version `0.13.742`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.741 — TE dump_auto known-tree clear and reboot

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Recursive `dumpDir` + `combinepath` / concat-only dump на TE 4.2.0: crash або overlay без scroll; missing `/su/baas` трактувався як error і блокував `dumped.ok` навіть після доброго 0010+00F0.
2. `nand_transfer_dump_auto.te` замінено console-proven known-tree dump (стиль `test.te`): `dumpFilesIn` / `dumpKnownTree` для `/`, `/su`, `/su/baas|nas|avators|cache`; без recurse, без nested foreach, без `combinepath` зі `/` у 2-му арг.; missing optional dirs = skip.
3. `clear()` на кожен save і перед RESULT; `pause()` потім `goHekate()`; `dumped.ok` лише якщо `got0010` і `got00F0`. Без `dump_result.txt`. Hub C++ / restore / i18n не чіпали.
4. Version `0.13.741`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.740 — fix TE dump_auto combinepath on nested save files

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Backup profiles & play hours через TE: `nand_transfer_dump_auto.te` падав на `su/registry.dat` — `combinepath(dstRoot, fsrc)` не приймає `/` у другому аргументі; після крашу `rel` губився на `mkdir`.
2. `dumpDir`: `out` / `mkdir` через конкатенацію `dstRoot + "/" + …`; шлях пака з `nand_pack.txt` у `packUnix` (не в `rel`); перед dump `rel = ""`.
3. Restore scripts: `writeDir` для nested `rel` теж через `srcRoot + "/" + rel` (не `combinepath` з slash). Hub C++ staging не чіпали. Pack лишається `/config/kefir/nand_transfer/<stamp>/`.
4. Version `0.13.740`. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.739 — fix MTP haze split (SUPPORTED_EXT bound, FsSaveProxy members)

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Після v0.13.736 haze split WSL ламався на `std::size(SUPPORTED_EXT)` (incomplete array) і на truncated `FsSaveProxy` (відсутні `m_mounts`/`m_mount_tick`/`m_mount_mutex` і `};` перед `MakeFsSaveProxy`).
2. `haze_internal.hpp`: `SUPPORTED_EXT` і `NRO_EXT` як `inline constexpr` з bound; визначення прибрано з `haze_internal.cpp` (`ROOT_DROP_RULES` лишається).
3. `haze_save_proxy.cpp`: відновлено три члени кешу mount і закрито struct; `MakeFsSaveProxy` знову поза класом. MountSave логіку не чіпали.
4. Version `0.13.739`. MTP поведінку не рефакторили. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.738 — fix Games split includes for title_nsp/ncm/save_paths

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Після v0.13.735 Games split WSL ReleaseWithInstall ламався на відсутніх includes: `NspEntry`/`ContentInfoEntry`/`BuildContentEntry` живуть у `title_nsp.hpp`, не в `title_info.hpp`; `ncm::GetAppId` потребує `yati/nx/ncm.hpp`; `save::GetSaveTypeLabel` — `ui/menus/save/save_paths.hpp`.
2. Додано `#include "title_nsp.hpp"` у `game/game_internal.hpp` (збережено `using title::NspEntry` і wrapper `game::BuildNspEntries`). У `game_scan.cpp` — `#include "yati/nx/ncm.hpp"`. У `game_details.cpp` — `#include "ui/menus/save/save_paths.hpp"`. Існуючі `using title::ContentInfoEntry` / `BuildContentEntry` без змін.
3. Version `0.13.738`. Логіку dump/scan/details не чіпали; **`gc_menu` не чіпали**. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.737 — include account_user.hpp in slim users_menu.cpp

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout.

1. Slim `users_menu.cpp` (після v0.13.731 split) викликає `account_user::LoadImageJpeg` у `Menu::TryLoadAvatar`, але включав лише `account/account_link.hpp`.
2. Додано `#include "account/account_user.hpp"` поруч із `account_link.hpp`. Публічний `users_menu.hpp` не чіпали (там лише forward-declare `account_user::Pack`).
3. Version `0.13.737`. Packed `CmdHeader` warning у `usb/dbi.hpp` і parallel-compile noise з `save_menu_ops.cpp` ігноровані як pre-existing / не пов’язані. Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.736 — split USB queue, MTP haze, and File Viewer TUs

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout. Поведінка без змін — лише розкладка файлів (architecture slice 5a/5b/5c).

1. USB/DBI install queue: спільні helpers у `dbi/dbi_internal.{hpp,cpp}`; `dbi_draw` / `dbi_usb` / `dbi_local` / `dbi_plan` / `dbi_session`; slim `dbi_menu.cpp`. Публічний `dbi_menu.hpp` без змін. 733 auto-install / yellow-bar логіка збережена byte-for-byte.
2. MTP haze: `include/haze/haze_internal.hpp` + `source/haze/` (`haze_internal`, `haze_fs_proxy`, `haze_install_proxy` з `InitInstallMode`/`DisableInstallMode`, `haze_save_proxy`, `haze_game_proxy`); slim `haze_helper.cpp`. Публічний `haze_helper.hpp` без змін.
3. File Viewer: `file_viewer/file_viewer_internal` + `file_viewer_text` + `file_viewer_image`; slim `file_viewer.cpp`. Публічний `file_viewer.hpp` без змін.
4. Version `0.13.736`. **gc_menu не чіпали.** Черга audit §2 A1–A7 **не** закрита.

## Попередній delivery: v0.13.735 — split Games menu into game/ TUs

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout. Поведінка Games без змін — лише розкладка файлів (architecture slice 4).

1. Спільні helpers з anonymous namespace винесені в `game/game_internal.{hpp,cpp}` (named `sphaira::ui::menu::game`: `NspSource`, control/load/dump helpers, badges/draw, move labels, `HeaderItem`/`TAB_*`/`HEADER_*`, `DeleteApplicationKeepSave`). Публічний `game_menu.hpp` і `game_list_info.hpp` без змін API.
2. Live Game Details: `DbiDetailsMenu` exact class body у `game/game_details.cpp`; `OpenGameDetails(...)` у `game_details.hpp` (Menu ctor Push через цей door).
3. Scan/ops TUs: `game_scan.cpp` (`AppendGameCardEntries`, `ScanHomebrew`, play stats/playtime, search/sort/free), `game_ops.cpp` (contents folders, delete/dump/repack/saves). Slim `game_menu.cpp` (~591) — ctor/dtor/Update/Draw/focus/selection/layout + live START dump options.
4. Version `0.13.735`. **gc_menu не чіпали.** Черга audit §2 A1–A7 **не** закрита. Наступний live (optional): `dbi_menu.cpp` USB UI, `haze_helper`, `file_viewer`.

## Попередній delivery: v0.13.734 — split Settings menu into settings/ TUs

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout. Поведінка Settings без змін — лише розкладка файлів (architecture slice 3).

1. Спільні helpers з anonymous namespace винесені в `settings/settings_internal.{hpp,cpp}` (`OnOff`, `ClampIndex`, `SettingsValueColour`, `MakeHeader`/`MakeFolderItem`/`MakeBoolItem`/`MakeOptionItem`, `ResolveItemIndex`, draw helpers, `LANGUAGE_ITEMS` / `TEXT_SCROLL_SPEED_ITEMS`, `NETWORK_LOCATION_ID`). Публічний `settings_menu.hpp` без змін API.
2. Builders + submenu TUs: `settings_categories` (`Menu::BuildCategories` + category builders), `settings_software`, `settings_dbi`, `settings_kefir` (+ `MakePackageAction` header), `settings_themes`, `settings_translate`, `settings_sources` (`TestLocationConnection`, `SourceEditMenu`, `BuildSourcesCategoryItems`). Шість окремих типів меню не уніфіковано.
3. Slim `settings_menu.cpp` (~644) тримає лише `Menu` ctor/dtor/focus/Update/Draw/DrawItemRow/folder/navigation. Існуючі `settings_{fs_utils,translations,tweaks,fancurve}` без змін.
4. Version `0.13.734`. Черга audit §2 A1–A7 **не** закрита; gray zone не чіпали. Наступний live slice: `game_menu.cpp` (не `gc_menu`).

## Попередній delivery: v0.13.733 — USB auto-install balances usable space; live yellow storage bar

Статус: реалізовано в primary checkout. Збірку й тести агент не запускає за політикою checkout.

1. `PlanPickSd` automatic (loc 4) балансує usable free: серед destinations де `size <= free_*` обирає менший gap після установки; tie → SD; якщо жоден не вміщує — roomier (`free_sd >= free_nand`). Reserve лишається у callers (`free - reserve`).
2. `yati::ChooseInstallTarget` і `App::GetInstallSdEnable` (Auto) рахують usable так само і делегують `PlanPickSd`.
3. USB/local DBI install loops: для `InstallTarget::Auto` перед стартом пакета `RefreshAutoInstallTarget` робить свіжий poll і переобирає dest; pinned Sd/Nand не чіпаються; `override.sd_card_install` лишається, щоб yati не переобирав зі stale snapshot.
4. Під час `State::Installing` header bars: жовтий сегмент = remaining поточного пакета на його dest; решту черги не проєктуємо (прибирає хибний overflow-red). ReviewQueue повний plan без змін.
5. Version `0.13.733`. Тести `test_install_plan` оновлено (balance cases); бінарник не ганяли.

## Попередній delivery: v0.13.732 — File Browser folder split

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout. Поведінка File Browser без змін — лише розкладка файлів (architecture slice 2).

1. Існуючі TU `filebrowser_ops` / `filebrowser_assoc` / `filebrowser_forwarder` перенесено `git mv` у `source/ui/menus/filebrowser/`. Публічні headers (`filebrowser.hpp`, `filebrowser_assoc.hpp`, `filebrowser_forwarder.hpp`) лишилися на місці.
2. Спільні helpers винесені в `filebrowser/filebrowser_internal.{hpp,cpp}` (uevent, FS_ENTRIES, network/SMB helpers, IdentifyPayload, MakeLauncherLabel, metadata_thread_func).
3. Решту `filebrowser.cpp` (~3770) розбито: `filebrowser_view`, `filebrowser_scan`, `filebrowser_metadata`, `filebrowser_options` (вкл. живий `DisplayAdvancedOptions`), `filebrowser_share` (MTP/FTP share поточної теки), `filebrowser_sources`; slim `filebrowser.cpp` (~605) тримає Menu + SignalChange + assoc loading.
4. Version `0.13.732`. Черга audit §2 A1–A7 **не** закрита; gray-zone Tools Coming soon / `gc_menu` / dead MTP install не чіпали. Наступні live slices: settings_menu category builders, `game_menu` (не `gc_menu`).

## Попередній delivery: v0.13.731 — Users menu + account domain folder split

Статус: реалізовано в primary checkout. Збірку агент не запускає за політикою checkout. Поведінка Users/restore/NAND без змін — лише розкладка файлів.

1. Account domain зібрано під `sphaira/include|source/account/`: `account_link`, `account_user`, `account_playtime`, `account_restore`, `nand_transfer` (git mv, namespaces без змін).
2. `users_menu.cpp` (~3830) розбито за зразком `save/`: `users_internal`, `users_profile`, `users_manage`, `users_restore` + `users_restore_library` / `users_restore_remote`, `users_nand`; публічний API лишається в `ui/menus/users_menu.hpp`.
3. Restore library / remote packs експортовані через `OpenRestoreLibrary` / `OpenRemoteUserPacks` (+ parse helpers); hand-rolled JSON не переписувався.
4. Version `0.13.731`. Черга audit §2 A1–A7 не закрита; наступні architecture slices окремо (filebrowser, settings builders, game/dbi/haze/file_viewer).

## Попередній delivery: v0.13.730 — NAND dump via TE auto + Ultrahand reopen hint

Статус: реалізовано в primary checkout. Parsing `en`/`uk`/`ru` JSON пройдено; збірку агент не запускає за політикою checkout.

1. **Backup profiles & play hours** спершу Horizon RO `Export`. Якщо 0010+00F0 відкрились — короткий success, без купи шляхів і без `dump.te`. Якщо ні — Hub сам ставить `wait_nand_dump`, пише nand flag + `nand_pack.txt`, і `LaunchTegraRomfs(nand_transfer_dump_auto.te)`. Користувача не шлють у hekate > payloads.
2. Auto-скрипт (без меню/pause): mount за nand flag, `readsave` + `dumpDir` з `listing.files.copy()`/`folders.copy()` для 0010/0011/00F0/0041 у пак, `dumped.ok`, `goHekate`. Наступний запуск Hub: dump done / retry TE.
3. Ultrahand **не вміє** запускати NRO чи HOME-форвардер (`open` лише `.ovl`). Перед TE Hub пише one-shot `/config/kefir/reopen_hub.flag`, JSON notify і вшиває `try:` у `/switch/.packages/boot_package.ini` `[on-boot]`: toast «Щоб завершити, відкрийте Kefir Hub.» Після відкриття Hub прапорці чистяться.
4. i18n en/uk/ru. Version `0.13.730`.

## Попередній delivery: v0.13.729 — Restore profiles & play hours via TE auto

Статус: реалізовано в primary checkout. Parsing `en`/`uk`/`ru` JSON пройдено; збірку агент не запускає за політикою checkout.

1. **Backup profiles & play hours** лишається Horizon RO `nand_transfer::Export` у `/config/kefir/nand_transfer/<stamp>/`; при lock 0010/00F0 — як і раніше `dump.te`, без нового формату пака.
2. **Restore profiles & play hours** більше не викликає `nand_transfer::Import` (Horizon write+Commit = та сама стіна, що ApplyLink). Hub пише `restore_pending/nand` (`emu`|`sys`), `nand_pack.txt` (unix шлях пака), best-effort сирі знімки `8000000000000010`/`00F0` для Undo, `phase=wait_nand_restore`, і `LaunchTegraRomfs(nand_transfer_restore_auto.te)`.
3. Новий auto-скрипт (без меню/pause): REQUIRE SD/KEYS/MINERVA/VER 4.0.0, mount за nand flag, `readsave` + `writeDir` (`listing.files.copy()` / `folders.copy()`) для 0010/0011/00F0/0041 з пака, `commit()`, `nand_restored.ok`, `goHekate`. Сирі blob на BIS не копіює. Меню-`restore.te` лишається для ручного запуску.
4. `OfferPendingRestore` / `HasUnfinishedRestore` знають `wait_nand_restore` (не віддають фокус link-prompt). ok → success; без ok — чесне попередження про SYSTEM restore / Undo. Той самий текст у pre-TE діалозі. i18n en/uk/ru. Version `0.13.729`.

## Попередній delivery: v0.13.728 — Manage Backups у Tools → Users

Статус: реалізовано й прийнято після senior-review у primary checkout. Пройдено `git diff --check` і parsing усіх 14 i18n JSON; збірку агент не запускає за політикою checkout.

1. `Tools → Users → Manage Backups` показує валідні local user packs з canonical `/config/kefir/account_backups` і legacy `user_packs`; чинний Restore Backup лишився окремим receiver/source flow.
2. У менеджері є restore, single/multi-delete, один зрозумілий Duplicate, rename лише filename та Send to another console. Duplicate не залишає `.part`/invalid archive на fail або cancel; rename/copy не перезаписують пакети й валідовують результат. Legacy directory packs зберігають restore/delete, а duplicate/rename чесно кажуть, що формат не підтримується.
3. Sender ділиться всіма discoverable backup roots через наявний Console Transfer server; для lone legacy root збережена встановлена v0.13.727 migration-семантика, а за наявності обох roots share монтує обидва.
4. Спільний `Sidebar` тримає рядок заголовка безпосередньо перед focus-row у viewport. Generic `List` не змінено: перехід до першої сторінки не скидає offset і не створює screen jump.
5. `START` у Tools повернено до sidebar «Install & Share»; плитка Console Transfer під `A` лишилась прямим hub. Версію піднято до `0.13.728`; усі 14 i18n JSON містять нові рядки та валідні.

## Попередній delivery: v0.13.727 — Account Backup ZIP і зрозуміла бібліотека

Статус: реалізовано й прийнято після senior-review у primary checkout. `git diff --check` пройдено, змінені `en`/`uk`/`ru` JSON валідовано; збірку агент не ганяє.

1. Новий canonical root — `/config/kefir/account_backups`, не технічний `user_packs`. Якщо нового root немає, а legacy є — його безпечно перейменовано; коли обидва roots на SD існують, Hub читає обидва без автоматичного merge/delete.
2. Backup User створює один атомарний `.kefir-user.zip` на кожен вибраний профіль до завершення ProgressBox/reboot. Ім’я — дата, безпечне ім’я, `linked`/`unlinked`/`link-unavailable`; email, UID і токени не потрапляють у filename. `.part` стає фінальним ZIP лише після успішного close; write/read помилки не створюють неповний backup.
3. ZIP містить перевірювані `manifest.json` (`type=account_backup`, numeric `version=1`) і `profile.json`, поточні avatar/playtime/link entries та безпечні archive paths. Restore, local library, delete і Console Transfer приймають новий ZIP та legacy directory packs; remote ZIP звіряється за точним розміром до rename. Кілька профілів — кілька незалежних архівів.

## Попередній delivery: v0.13.726 — File Browser: видалення та описи KefirHub

Статус: реалізовано й прийнято senior-review у primary checkout; `git diff --check` пройдено, змінені `en`/`uk`/`ru` JSON валідовано. Збірку агент не ганяє.

1. Непорожні теки на native microSD більше не проходять ручний обхід файлового менеджера: `ProgressBox` викликає наявний `Fs::DeleteDirectoryRecursively()`. Це прибирає хибний `FsError_TargetLocked` для SD-артефактів KefirHub на кшталт `account_link_rollback` і старого `account_save_dump`; перед стартом і між вибраними елементами збережені progress/cancel checks. Файли та не-native файлові системи лишаються на попередньому шляху.
2. При перегляді саме `/config/kefir` File Browser використовує наявний приглушений другий рядок для коротких локалізованих пояснень власних папок KefirHub: rollback/dump/restore, user packs, playtime/NAND staging, а також assoc/themes/github/i18n/downloads/packages/logo/cache/avatars. Нового UI або metadata-шару немає.
3. Version `0.13.726`; змінено `en`/`uk`/`ru`, тому інші вбудовані мови штатно отримують англійський fallback. `git diff --check` і JSON parsing пройдено; без компіляції.

## Попередній delivery: v0.13.725 — HTTP-source launch progress lifetime

Статус: реалізовано й прийнято senior-review у primary checkout; `git diff --check` пройдено. Збірку агент не ганяє.

1. Свіжий Atmosphère report `01788272108_054956fb30c19000.log` (`2168-0002`, build ID `b4464776...`, v0.13.724) символізується як `ProgressBox::~ProgressBox` → done callback → `FsView::SetFs` з `App::Update`, не `App::~App`. Лог о 17:14:41 показує `Menu::~Menu` → `UmountAllNeworkDevices`, а вже потім прихований ProgressBox викликає `SetFs` для знищеного view.
2. Причина — `Menu(u32, launch_location)` викликає `ConnectToLocation` у конструкторі. Вкладений `App::Push<ProgressBox>` виконується до того, як зовнішній `App::Push<Menu>` додасть меню, тому progress опиняється під меню: користувач бачить порожній teal background і може закрити owner раніше callback.
3. Початковий connect тепер один раз відкладається до першого `Menu::OnFocusGained`: pending closure копіює `location::Entry` за значенням, очищається до виклику й одразу повертає керування після `ConnectToLocation`. Menu на цей момент уже в widget stack, тому ProgressBox стає над owner menu; після його pop другий focus запускає звичайний scan та assoc loading. Root-view connect, v0.13.724 shutdown guard і HTTP/JSON код не змінено. Version `0.13.725`; `git diff --check`, без компіляції.

## Попередній delivery: v0.13.724 — Console Transfer HTTP-source

Статус: реалізовано в primary checkout; `git diff --check` пройдено. Збірку агент не ганяє.

1. Atmosphère report `01788265802_054956fb30c19000.log` для build ID `7392d995...` символізувався як `ProgressBox::~ProgressBox` → completion callback → `FsView::SetFs` → alignment fault `PC=0x81` під час `App::~App`. `App::IsExiting()` і ранній `m_quit=true` відсікають callback/warning push тільки під час teardown; cancel, join і cleanup ProgressBox лишаються безумовними.
2. HTTP/HTTPS mount один раз перевіряє JSON `/list?path=...`; лише валідні Sphaira `path` + `entries(name,type,size)` вмикають Sphaira-режим. Каталоги тоді читаються через `/list`, файли, HEAD/range і streaming read — через URL-encoded `/download?path=...`; invalid JSON дає log + контрольований `-EIO`.
3. WebDAV/WebDAVS, FTP і plain-HTML HTTP не отримують нового протоколу: WebDAV не має навіть probe `/list`; failed/non-Sphaira HTTP probe повертається до чинного PROPFIND/HTML fallback. Version `0.13.724`; пройдено `git diff --check`, без компіляції.

## Попередній delivery: v0.13.723 — Console Transfer: IP:port only

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. `StartConsoleTransferShare` після `App::SetMountedFolders` запускає web server з порожнім шляхом для кожного з шести пунктів Console Transfer: Entire microSD, Save Backups, User Backups, Screenshots & Videos, switch Folder і Choose Folder.
2. Progress box і QR показують тільки `http://IP:port`; непотрібний `plain_root` прибрано. `WebShareFolder` та File Browser не змінено, тому його явне розшарювання теки як і раніше відкриває саме цю теку.
3. Version `0.13.723`; пройдено `git diff --check`. Компіляцію не запускали.

## Попередній delivery: v0.13.722 — Restore Backup: TE apply link (no Horizon 0010 write)

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Other-console Restore: Create + Horizon `OpenAccountSaveWritable`/ApplyLink ламав `ns` (2011-0301); kill ns ламав `am`. Same-console Replace працював, бо не інжектив новий nas у живий 0010.
2. `StartRestoreBackup` більше не викликає `ApplyLinkPackages` / `TerminateAccountDaemons`. Create з валідним baas/nas: remap UID → SD `/config/kefir/restore_pending/link/{baas,nas}/`, `phase=wait_link`, `LaunchTegraRomfs(account_0010_apply_link.te)`. Replace (доказаний nas): лише name/avatar. Create без лінку: reboot без відкриття 0010.
3. TE `account_0010_apply_link.te`: mount за `nand`, `readsave` живого 0010 (raw blob не замінює), пише staged baas/nas у `/su/baas|/baas` і `/su/nas|/nas`, `commit()`, `link_applied.ok`. Hub: `wait_link`+ok → applied; без ok — попередження про unlinked профіль / Undo. Version `0.13.722`.

## Попередній delivery: v0.13.721 — TE dump SnapshotOk + auto-continue restore

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Dest SD (Hub v0.13.720): TE успішно скопіював raw `8000000000000010` + `dumped.ok`, але `SnapshotOk()` повернув false → `OfferPendingRestore` показав «dump is not on SD yet», Create/ApplyLink не стартували.
2. `SnapshotOk`: ок через OpenFile; інакше `FileGetSizeAndTimestamp`; інакше listing `restore_pending` з файлом `8000000000000010` size≥0x200; лог exists/size/dir на fail. `dumped.ok` враховується в логах/пробах.
3. `wait_dump`→`ready` після успішного дампу: `SavePending(ready)` + auto `StartRestoreBackup` (користувач уже підтвердив Restore перед TE). Live Horizon-дамп теж одразу продовжує restore в тій самій сесії. Create коли dest nas ≠ pack nas; без playtime; ns не вбиваємо; Replace лише при доказаному nas. Version `0.13.721`.

## Попередній delivery: v0.13.720 — Restore Backup: one user + NA; no playtime; proven Replace only

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Restore Backup знову лише один юзер: nickname, avatar, official Nintendo Account (baas/nas). Не чіпає playtime / 00F0 / `playtime_restore.te` (окремий майбутній «clone all + hours»).
2. Revert евристики v0.13.719: Replace лише коли pack nas **доказаний** (IPC nas == pack nas, або baas file nas == pack nas). Якщо Query fail і 0010 closed → Create дозволений. Якщо 0010 відкритий і baas має інший nas → Create; той самий nas → Replace. Nickname не є критерієм збігу.
3. Після ApplyLink — негайний reboot (BCAT/ACCOUNT/OLSC; ns/friends не чіпаємо). Без ApplyLink — success UI. Confirm/sidebar більше не обіцяють години. Version `0.13.720`.

## Попередній delivery: v0.13.719 — Restore Backup: Replace when NA unproven; do not kill ns/friends

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Dest H: з horizon-linked профілем + закритим 0010: `FindLiveUidByNasId` не бачив pack nas → Create + ApplyLink на новий uid → fatal `am` 2011-0301 і bootloop `account` 2168-0006.
2. Якщо QueryNintendoAccountId не доводить інший nas, а 0010 закритий — Replace існуючий linked uid (ім'я/аватар/години + ApplyLink), не Create. Якщо Query повертає *інший* nas — Create лишається дозволеним.
3. `TerminateAccountDaemons` більше не вбиває ns (0015/001F) і friends (000E) з Hub applet (ламало `am`). Лишаються BCAT/ACCOUNT/OLSC; після ApplyLink — негайний reboot. Логи: Query rc/nas, Create vs Replace, чи знімали baas. Version `0.13.719`.

## Попередній delivery: v0.13.718 — Raw 0010 snapshot for Undo (no unpack)

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Знімок перед Restore Backup — сирий `SYSTEM:/save/8000000000000010` у `/config/kefir/restore_pending/8000000000000010`, не дерево `0010/su/`.
2. Прапорець `restore_pending/nand` (`emu`|`sys`) пишеться під час дампу (Horizon через `App::IsEmummc()`, TE через `emu()`). Undo без меню NAND монтує той самий розділ і кладе файл назад через `copyfile`.
3. Horizon-дамп іде через `FsNativeBis`; якщо зайнято — TE `/startup.te` з тим самим сирим `copyfile` + hekate. `user_packs` не змінювались. Version `0.13.718`.

## Попередній delivery: v0.13.717 — One Nintendo Account per baas; other-console restore no bootloop

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Рестор на іншій консолі впав: `ns` 2011-0301 під час запису 0010, потім `account` 2168-0006. Дублікат — той самий nas_id у двох baas (живий профіль цілі + новостворений), не UID з джерела.
2. `FindLiveUidByNasId` дивиться IPC і файли baas; якщо цей Nintendo Account уже є — Replace, не Create. `ApplyLinkPackages` перед записом стирає всі baas з цим nas_id і пише один файл. Перед записом 0010 зупиняє також ns (0015 і 001F).
3. Undo стирає baas/nas у сейві, потім повертає знімок. Version `0.13.717`.

## Попередній delivery: v0.13.716 — Receive User Backup from another console

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Приймальна консоль за введеним IP отримує список user packs від увімкненого `Share User Backups`, показує його повноекранно та дає вибрати один пак.
2. Вибраний пак завантажується до налаштованого user-packs root через наявні `/list`, `/list-recursive` і `/download`; далі запускається чинний safe Restore Backup flow без дублювання перевірок.
3. Маніфест жорстко обмежений обраним паком, шляхи й розмір кожного файла перевіряються, колізії не перезаписуються, а скасування/помилка прибирає тільки нову неповну теку. PIN не потрібен.
4. Version `0.13.716`.

## Попередній delivery: v0.13.715 — Short User Backups server address

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. `Console Transfer → Share User Backups` показує лише коротку адресу `http://IP:port`, без `?path=` та encoded шляху.
2. За цією адресою корінь сервера одразу віддає один розшарений user-packs mount; generic root/source selection лишається для нуля або кількох mount-ів.
3. Приймальна консоль просить лише IP відправника й сама шукає порт у діапазоні 8080–8090. Це ще connection probe, не передавання паків.
4. Version `0.13.715`.

## Попередній delivery: v0.13.714 — Restore Backup sources and console probe

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. `Restore Backup` відкриває повноекранний список джерел: стандартна локальна бібліотека, вибір теки через File Browser або інша консоль.
2. Вибрана тека може бути одним user pack або містити кілька паків; для такого зовнішнього джерела видалення вимкнено. Сам restore, його перевірки та rollback лишаються наявним шляхом.
3. Інша консоль приймає IP або HTTP URL, шукає HTTP-відповідь на портах 8080–8090 для голого IP і показує чесний результат. Це ще не передає і не відновлює віддалені дані; перевірку можна скасувати.
4. Version `0.13.714`.

## Попередній delivery: v0.13.713 — Console Transfer Share sources over HTTP

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Console Transfer відкривається як повноекранне меню, не як sidebar. Є рівно шість робочих Share-дій: microSD, save backups, user backups, album, switch/homebrew і вибрана папка.
2. Дії перевикористовують наявний HTTP-сервер, QR/progress flow та file-browser picker. Шляхи save/user/album/homebrew беруться з їхніх власних resolver-ів/налаштувань; QR не-root папки відкриває саме її.
3. Version `0.13.713`.

## Попередній delivery: v0.13.712 — Console Transfer hub placeholder

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. У Tools є дев'ята плитка `Console Transfer` з наявною network-іконкою; плитка й START відкривають один sidebar.
2. Sidebar має заглушки CONNECT / SHARE / PHONE та після них лишає наявні робочі Web Server, MTP і PC Install. Адреси, мережевий протокол, читання файлів і стрим встановлених ігор тут не реалізовані.
3. Версія `0.13.712`.

## Попередній delivery: v0.13.711 — Same-console restore matches live UID, never clones

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Бекап+рестор на тій самій консолі пізнає юзера за UID з пака (не лише nas IPC). Replace існуючого, без другого аватара без лінку.
2. Версія `0.13.711`.

## Попередній delivery: v0.13.710 — Same-console restore asks to replace existing profile

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Бекап і рестор на тій самій консолі: якщо цей Nintendo Account уже є — питаємо Replace (ім'я, аватар, години), не створюємо другого юзера.
2. На іншій консолі лишається додавання нового профілю з лінком.
3. Версія `0.13.710`.

## Попередній delivery: v0.13.709 — Restore Backup keeps NA link and play hours

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Якщо цей Nintendo Account уже є — не створюємо розлінкований клон, а пишемо ім'я/аватар/години на існуючий UID.
2. Якщо NA немає на консолі — Create + baas/nas як раніше.
3. Години після рестору самі йдуть у TegraExplorer через `/startup.te` (`playtime_restore.te`), потім hekate. Не лишаємо ручний скрипт.
4. Версія `0.13.709`.

## Попередній delivery: v0.13.708 — Rollback path + restore before link prompt

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Якщо не завантажиться: `hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te`. У TE — кнопки живлення та гучності.
2. Незавершений рестор показується раніше за «прив'яжи профіль».
3. Версія `0.13.708`.

## Попередній delivery: v0.13.707 — After dump/rollback load hekate

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. `startup.te` і `Undo_restore_if_wont_boot.te` після роботи вантажать `bootloader/update.bin` (hekate), без pause/exit у TE.
2. Якщо payload не взяв — пробуємо `payload.bin`, потім `atmosphere/reboot_payload.bin`.
3. Версія `0.13.707`.

## Попередній delivery: v0.13.706 — Readable rollback script name

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. У TegraExplorer Scripts скрипт відкату: `Undo_restore_if_wont_boot.te` (не `account_0010_rollback.te`).
2. Старий файл з Scripts прибирається. Версія `0.13.706`.

## Попередній delivery: v0.13.705 — Dump locked 0010 only via /startup.te

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Зайнятий 0010: Хаб пише скрипт у `/startup.te` і стартує TegraExplorer. Не кладе `account_0010_dump.te` у Scripts.
2. Відкат бутлупу лишається `TegraExplorer/scripts/account_0010_rollback.te`.
3. Версія `0.13.705`.

## Попередній delivery: v0.13.704 — Mount romfs before reading dump.te

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Лог 703: `findTegraExplorerPayload: /bootloader/payloads/TegraExplorer.bin`, потім `dump te missing from romfs`. Файл є в NRO; `fopen(romfs:/tegra/...)` без `romfsInit()`.
2. CopyTe / LaunchTegraDump / playtime / nand-transfer скрипти монтують romfs як account_link.
3. Версія `0.13.704`.

## Попередній delivery: v0.13.703 — Launch TegraExplorer without payload-api marker

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. `Could not start TegraExplorer`: `rebootToPayload` вимагав `/config/kefir/hekate-payload-api.ini`, якого на консолі немає. З Hekate > Payloads файл запускається.
2. Шукаємо `.bin` у `/bootloader/payloads` (будь-який регістр імені). Спочатку one-shot API, якщо ні — `setHekateAutobootPayload` + ребут (як Lockpick). dump.te повертає `hekate_ipl.ini.bak`.
3. Версія `0.13.703`.

## Попередній delivery: v0.13.702 — Path text: hekate > payloads > tegraexplorer

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. У Users шлях до TegraExplorer: `hekate > payloads > tegraexplorer`.
2. Версія `0.13.702`.

## Попередній delivery: v0.13.701 — Say Hekate → Payloads → TegraExplorer, not RCM

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. У Users (рестор 0010 і console-move dump/restore) замість «RCM» — Hekate → Payloads → TegraExplorer.
2. Версія `0.13.701`.

## Попередній delivery: v0.13.700 — Locked 0010 dump via startup.te and TegraExplorer

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Якщо Horizon не віддав 0010 — після ОК Хаб пише `/startup.te` і запускає TegraExplorer.bin. Скрипт сам знімає 0010, видаляє startup.te, повертає CFW.
2. Тексти пояснюють навіщо знімок і що користувач сам знову відкриває Хаб для продовження рестору. Без інструкції «йди в RCM і запускай dump.te».
3. Версія `0.13.700`.

## Попередній delivery: v0.13.699 — Snapshot 0010 before Restore Backup

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Перед рестором — знімок 0010 read-only (ACCOUNT не вбиваємо). Якщо зайнятий — `account_0010_dump.te`.
2. Прапор `/config/kefir/restore_pending/`; скрипт відкату `TegraExplorer/scripts/account_0010_rollback.te`.
3. Наступний запуск Хаба пропонує продовжити рестор (ребут) або скасувати. Якщо бутлуп — TE rollback пише 0010 назад, знімає pending і видаляє себе.
4. Версія `0.13.699`.

## Попередній delivery: v0.13.698 — Warn then auto-reboot; skip duplicate Nintendo Account

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Backup user / Restore Backup: спочатку діалог «консоль перезавантажиться», після успіху — `requestForcedReboot()` без Later і без Refresh.
2. Restore на ту саму консоль, де вже є цей NA: лінк не пишеться (два baas на один nas_id валили `account` на буті, 2168-0006 + bootloop 001e/000c/003e/001f).
3. Лінк застосовується після підготовки годин. Після terminate ACCOUNT UI більше не малює список юзерів.
4. Версія `0.13.698`. Цеглу з 697 знімають бекапом SYSTEM у Hekate.

## Попередній delivery: v0.13.697 — Do not write 00F0 under Horizon; reboot after ACCOUNT stop

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Restore Backup відкривав 00F0 **writable**, поки `ns` тримає сейв → Atmosphere User Break `2011-0301` (процес ns / 010000000000001f), rollback не встиг з'явитися. Тепер лише read-only merge у `/config/kefir/playtime_pending/PlayEvent.dat` + `playtime_restore.te` (RCM).
2. Backup user після Overwrite зупиняв ACCOUNT, щоб прочитати лінк, і Refresh показував порожній список. Після terminate — діалог ребуту.
3. Версія `0.13.697`. sysNAND: не писати 00F0 з Horizon.

## Попередній delivery: v0.13.696 — Review fixes for play-hours Backup Restore

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. `ParseBlob` приймає порожній журнал (`count=0`); раніше падав і не дописував години в порожній/обнулений `PlayEvent.dat`.
2. Backup більше не завжди каже «години в паку»: дивиться, чи реально є `pdm/PlayEvent.dat`.
3. Помилка запису 00F0 більше не маскується під «locked»; текст попереджає, що повторний Restore Backup створить ще один профіль.
4. Версія `0.13.696`.

## Попередній delivery: v0.13.695 — Backup Restore includes per-user play hours

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Identity-only Restore Backup не лишаємо: пак пише зріз `PlayEvent` цього UID (`pdm/PlayEvent.dat`), не весь 00F0.
2. Restore створює новий UID, ремапить account-події на нього і **дописує** в `PlayEvent.dat` цілі з SD rollback. Години інших профілів не замінюються (це не Console Move).
3. UI чесний: список бекапів показує play hours / no play hours; після рестору — скільки годин записано, чи пак старий без годин, чи 00F0 зайнятий. Ребут якщо записано link або години.
4. Версія `0.13.695`. Старі паки без `pdm/PlayEvent.dat` треба перезібрати. Console Move не чіпали.

## Попередній delivery: v0.13.694 — Linked backup export + overwrite/delete/dates

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Restore без reboot = пак Local: `TryOpenAccountSave` відкривав 0010 RW і падав, поки ACCOUNT тримає сейв. Тепер read-only, а якщо зайнято — terminate BCAT/ACCOUNT/OLSC і повтор. LoadUserPackLinkPackage ігнорує зайві nas-файли.
2. Якщо бекап цього UID уже є — запит Overwrite / Keep both / Cancel; overwrite стирає старі паки цього UID.
3. Restore Backup: дата `ДД.ММ.РРРР, ГГ:ХХ` замість імені теки; Minus видаляє пак з SD.
4. Версія `0.13.694`. Старий Local-пак треба перезібрати новим Backup user.

## Попередній delivery: v0.13.693 — Restore Backup link + Icon status dots

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Restore Backup писав baas нового профілю зі старим UID у байтах 0..15 — Horizon CheckAvailability лишав Not linked. `ApplyLinkPackages` тепер копіює `AccountUid` цілі в baas перед записом (як Linkalho).
2. `ExportUserLinkPackage` шукає baas також за іменем UID, якщо IPC nas_id не збігся з файлом.
3. Users → Icon (LayoutType_Grid): зелена/червона крапка в лівому верхньому куті аватарки (linked / not linked). Не в заголовку.
4. Версія `0.13.693`.

## Попередній delivery: v0.13.692 — Backup & Restore User as own Options heading

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. `Backup user` і `Restore Backup` винесено з PROFILE (Create/Rename/Avatar/Delete) під окремий підзаголовок `BACKUP & RESTORE USER`.
2. CONSOLE MOVE не змінювали. Restore Backup лишається на порожньому списку. i18n en/uk/ru.
3. Версія `0.13.692`.

## Попередній delivery: v0.13.691 — Backup user / Restore Backup back in Options

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. L/R Backup/Restore з 690 ховали дії з контекстного меню — користувач їх не бачив. Пункти повернуто в Options під PROFILE, підряд, без окремого підзаголовка Backup and Restore.
2. Restore Backup доступний і коли список профілів порожній. CONSOLE MOVE (profiles & play hours) не чіпали. Фікс стеку 690 лишається.
3. Версія `0.13.691`.

## Попередній delivery: v0.13.690 — Fix user backup crash and move Backup/Restore

Статус: реалізовано в primary checkout. Збірку агент не ганяє.

1. Crash `I:\atmosphere\crash_reports\01788103076_054956fb30c19000.log` (Build ID `C011EEF2…`, v0.13.689): Data Abort, SP нижче стеку ProgressBox. `addr2line`: `CollectInstalledApps` ← `ExportUserPacks:354` ← `ProgressBox` thread. Причина — `NsApplicationControlData` (~144 KiB) на стеку 128 KiB. Тепер структура на купі, як у `title_info.cpp`.
2. `Backup user` / `Restore Backup` прибрано з Options (Create/Rename/Delete). На екрані Users: **L Backup**, **R Restore**. Без окремого підзаголовка Backup and Restore.
3. Якщо галочок немає — бекап лише профілю під курсором (`SelectedUsers` fallback); виділені галочками — усі виділені. UID знімаються в момент L, не пізніше.
4. Версія `0.13.690`.

## Попередній delivery: v0.13.689 — Fix user backup restore build

Статус: реалізовано в primary checkout. `LoadUserPackLinkPackage()` не використовує відсутній у цьому libnx `Result_FsPathNotFound`; відсутня `baas/` лишається local backup, а наявний неповний BaaS/NAS набір — malformed link package. Збірку агент не ганяє.

## Попередній delivery: v0.13.688 — Complete User Backup + Restore Backup

Статус: реалізовано в primary checkout. Пройшли `git diff --check`, парсинг усіх 14 i18n JSON і статичний safety-review; збірку агент не ганяє.

1. `Backup user` бере один поточний або всі виділені профілі через наявний `SelectedUids()` і пише окремий SD-пак із profile/avatar, читабельним playtime export та повним link-набором. Game saves не входять у цей delivery: для них залишається окремий Backup saves.
2. Link export читає підтверджені директорії account save `/su/baas` і `/su/nas`. Файл BaaS визначається за NAS ID у байтах 16..23 little-endian, а не за його ненадійним іменем; до пака потрапляють тільки BaaS і пов'язані NAS-файли. Пак валідний і без link-даних для локального профілю.
3. Замість `Restore user pack` Users показує `Restore Backup`: локальний список валідних папок із `/config/kefir/user_packs`, multi-select та перевірку вільних profile slots. Для кожного пака створюється новий Horizon UID, відновлюється avatar і, за наявності повного валідованого BaaS/NAS набору, link застосовується до нового UID через наявний account-save write path з SD rollback та Commit. Існуючі профілі не замінюються.
4. `0011`/UID-generator state, спільний PDM playtime `00F0` і game saves не переносяться цим шляхом. UI/README мають повідомляти лише фактично відновлені profile/avatar/Nintendo link та прямо вказувати окремий Backup saves. Перед прийняттям: `git diff --check` і перевірка синтаксису всіх i18n JSON; компіляцію не запускати.

## Попередній delivery: v0.13.687 — Users Icon labels above tiles

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. Users → Icon знову малює повнорозмірний avatar у межах своєї плитки; внутрішню caption-смугу та зменшення avatar прибрано.
2. Nickname непоточних плиток тепер clipped і над їхньою рамкою; локально збільшений проміжок між рядами не дає підписам накладатися на avatar іншого ряду.
3. Focus лишає тільки чинну comic-хмаринку; UID поточного профілю збережено під сіткою. Версія `0.13.687`; `git diff --check` пройшов.

## Попередній delivery: v0.13.686 — Account-link safety copy in all locales

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. Англійський launch-текст прив’язки тепер є базовим i18n-ключем: safe, без видалення чи зміни ігрових сейвів, а також пояснення потреби деяких ігор у linked Nintendo Account.
2. Новий ключ і локалізоване повідомлення додано до всіх 14 i18n-файлів; already-linked профілі лишаються без змін.
3. Версія `0.13.686`; JSON parsing і `git diff --check` пройшли.

## Попередній delivery: v0.13.685 — Users Icon caption band

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. Users → Icon: nickname винесено у власну clipped верхню смугу плитки, тому текст більше не зливається з avatar.
2. Avatar зменшено та опущено під смугу; у focus звичайний підпис приховано, лишається лише чинна comic-хмаринка `drawAppLable`.
3. Один UID поточного профілю під сіткою збережено; List і Grid Detail не змінювалися. Версія `0.13.685`; `git diff --check` пройшов.

## Попередній delivery: v0.13.684 — Safe account-link copy

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. Український launch-текст прив’язки пояснює, що вона безпечна для ігрових сейвів: не видаляє й не змінює їх.
2. Збережено наявне пояснення, що деяким іграм для запуску потрібен Nintendo Account; already-linked профілі не змінюються.
3. Версія `0.13.684`; JSON parsing і `git diff --check` пройшли.

## Попередній delivery: v0.13.683 — Readable Users Icon labels

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. У Users → Icon nickname тепер є на кожній іконці як звичайний обрізаний підпис; поточна focus-хмаринка `drawAppLable` лишилась без змін.
2. UID більше не друкується на аватарах: показується один читабельний `ID` профілю у фокусі під сіткою.
3. Макети List і Grid Detail не змінювалися. Версія `0.13.683`; `git diff --check` пройшов.

## Попередній delivery: v0.13.682 — Fix applet suspended-app detection

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. `HasSuspendedApplication` більше не трактує `pmdmntGetApplicationProcessId` failure (немає application) як «є згорнута гра» — це ламало launch prompt у album без гри.
2. Як EdiZon: suspended лише якщо SUCCESS і PID ≠ 0; опційно `pmdmntGetProgramId` для логу program id.
3. Версія `0.13.682`.

## Попередній delivery: v0.13.681 — Fix launch account-link prompt

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. Launch OptionBox більше не Push-иться з конструктора MainMenu (там він опинявся під MainMenu і не отримував input).
2. Показ перенесено на перший `OnFocusGained` MainMenu — OptionBox стає top-of-stack.
3. `CanOfferLaunchLink` пише aggregate log (skip/gated/unlinked counts). На SD `account_link_prompt_skip` не був увімкнений.
4. Версія `0.13.681`.

## Попередній delivery: v0.13.680 — Remove account diagnostics + Unlink

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. Прибрано Users DIAGNOSTICS (усі Probe*) і API `RunDiagnostic` / AdminProbe у `ListUsers`.
2. Додано `UnlinkLinkedProfiles` (Linkalho-стиль): terminate BCAT/ACCOUNT/OLSC → rollback → delete baas цілей → nas лише якщо ніхто більше не посилається → Commit → reboot.
3. Users: Unlink Nintendo Account — вибрані linked, або всі linked якщо selection порожній; той самий applet+suspended gate.
4. Прибрано мертві i18n ключі diagnostics/TE-export/dump; docs §3.3; версія `0.13.680`.

## Попередній delivery: v0.13.679 — MainMenu B back + launch account-link warning

Статус: реалізовано в primary checkout; збірку агент не ганяв.
1. На вкладці Tools кнопка B повертає на Homebrew (`Back`); повторне B на Homebrew виходить (`Exit`).
2. Launch OptionBox для нелінкованих профілів: повне пояснення (що/навіщо/переваги + reboot); 3 кнопки як у оновленні — Later (B), Don't remind again (Minus), Link and reboot (+).
3. Offer лише коли є unlinked і не (applet + suspended game); `account_link_prompt_skip` у config вимикає нагадування; після Minus — підказка про ручний шлях Tools → Users → Link Nintendo Account.
4. i18n en/uk/ru, docs §3.3, версія `0.13.679`.

## Попередній delivery: v0.13.678 — Safe unlinked-only Nintendo Account donor link

Статус: прийнято сеньйором (Gemini junior, chat `Official vs Fake Nintendo Account status in Kefir Hub (Sphaira)`).
1. Hardware probes не дали надійного локального доказу для розрізнення official і Linkalho: cached resource/token IPC однаково недоступні, Administrator flags однакові, а read-only save `0010` заблокований. `Offline` більше не означає «Fake» у UI.
2. Users показує будь-який Horizon-linked профіль зеленим `Linked`; red `Not linked` лишається лише для профілю, який Horizon прямо повернув як unlinked. Внутрішній disk-token evidence і diagnostics збережено, aggregate log перейменовано на `linked_unverified`.
3. `LinkAllFromRomfsDonor`, launch prompt і Users confirm обирають тільки `linked_known && !horizon_linked`. Уже прив'язані профілі не модифікуються; romfs donor, rollback, BaaS-per-target, NAS copy і один Commit збережені.
4. Link flow пише aggregate-only stage logs для donor load, кількості цілей, writable save, Commit і completed count без UID, Nintendo/NAS ID, filename чи токенів. Версію піднято до `0.13.678`; `git diff --check` і JSON parsing пройшли. Збірку й тести не запускали, потрібна hardware-перевірка на Switch.

## Попередній delivery: v0.13.677 — Manual Nintendo Account diagnostics

Статус: прийнято сеньйором (Gemini junior, chat `Official vs Fake Nintendo Account status in Kefir Hub (Sphaira)`).
1. Users > Nintendo Account отримує окремі ручні, read-only diagnostic actions для безпечних IPC-перевірок: lock/save після suspend daemon, cached Nintendo profile resource, token-cache update state, administrator registration/link state.
2. Кожна дія запускається лише кнопкою користувача, не змінює `LinkKind`, токени, профілі, save або мережевий стан і пише у log лише aggregate counts/booleans/Result без UID, NAS ID, email, filename чи payload.
3. Сумнівний fingerprint IPC не викликається, доки не підтверджено його ABI; network refresh/reauth/debug commands не входять у діагностику.
4. Версію піднято до `0.13.677`; `git diff --check` і JSON parsing пройшли. Збірку й тести не запускали, потрібна перевірка кожної кнопки на Switch.

## Попередній delivery: v0.13.676 — Administrator official-link probe

Статус: прийнято сеньйором (Gemini junior, chat `Official vs Fake Nintendo Account status in Kefir Hub (Sphaira)`).
1. Для кожного Horizon-linked профілю `acc:su` → `IAdministrator::IsLinkedWithNintendoAccount` читається без доступу до мережі, save або зміни UI-класифікації.
2. Лог — лише aggregate `AdminProbe: linked/true/false/failed`, без UID, NAS ID, email, filename чи токенів.
3. Версію піднято до `0.13.676`; значення bool має бути підтверджене на Nintendo/Kefir/Linkalho до використання як product classification.
4. Збірку й тести не запускали; потрібна перевірка на Switch.

## Попередній delivery: v0.13.675 — Disk token Official detection

Статус: прийнято сеньйором (Gemini junior, chat `Official vs Fake Nintendo Account status in Kefir Hub (Sphaira)`).
1. System save `0010` відкривається SystemSaveData API; scan не пише, не видаляє й не commit-ить save.
2. Official лише за парою `_id.token` + `_refresh.token` для `nas_id` на диску; при відкритому save цей доказ сильніший за LoadIdTokenCache.
3. Короткий hex `nas_id` не може збігтися з початком чужого filename; версію піднято до `0.13.675`.
4. Збірку й тести не запускали; потрібна перевірка на Switch.

Наступне, не входить у v0.13.675: optional Icon/Grid labels — завжди показувати nickname, UID лише для focus і не давати йому переповнювати Grid Detail.

## Попередній delivery: v0.13.674 — Official vs Fake classification + status colours

Статус: прийнято сеньйором (Gemini junior, chat `Romfs official link`). Збірку не ганяли.
1. `QueryIdTokenCache` (LoadIdTokenCache cmd 4/3) + fallback RO-скан `/su/baas`+`/su/nas` для Official; Fake лишається без token proof.
2. Кольори статусу в картках Users: Official зелений, Fake жовтий, Not linked червоний.
3. Версія `0.13.674`.

## Попередній delivery: v0.13.673 — Direct paste and explicitly confirmed replacement

Статус: прийнято сеньйором. Агент не компілює.
1. Натискання Paste одразу запускає операцію, якщо конфліктів немає; попередній діалог `Paste file(s)?` прибрано.
2. `HasPasteConflicts()` зберігає перевірку файлів у вибраних теках і cut/rename. За конфлікту показується лише наявний Replace/Cancel prompt.
3. `OnPasteCallback(bool replace_existing)` отримує дозвіл лише з Replace; видаляє destination-файли перед копіюванням або rename лише за цим прапорцем. Sphaira піднято до `0.13.673`; static review і `git diff --check` виконано, збірку не запускали.

## Попередній delivery: v0.13.672 — Conservative DBI translation-file label

Статус: прийнято сеньйором. Агент не компілює.
1. File Browser показує `DBI translation file` лише для точного шляху `/switch/DBI/translation.bin`, коли в цій самій файловій системі є `/switch/DBI/DBI.nro`.
2. Перевірка виконується до відкриття й bounded content-scan payload; однойменні файли в інших місцях, вкладених теках або без `DBI.nro` лишаються з фактичною назвою.
3. Sphaira піднято до `0.13.672`; проведено static review і `git diff --check`; збірку не запускали.

## Попередній delivery: v0.13.671 — Safe payload labels and paste replacement prompt

Статус: прийнято сеньйором. Агент не компілює.
1. Прибрано ненадійний single-string detector `Lockpick_RCM`: DBI `translation.bin` більше не отримує фальшиву мітку, а нерозпізнаний payload лишається з фактичною назвою.
2. Перед paste, що може перезаписати destination-файл із тим самим шляхом, виявляються конфлікти також усередині обраних тек. Відмова в окремому confirm скасовує весь paste до запуску transfer; без конфліктів потік не змінюється.
3. Sphaira піднято до `0.13.671`, додано i18n prompt і проведено static review; збірку не запускали.

## Попередній delivery: v0.13.670 — Launch/Users Link Nintendo Account

Статус: прийнято сеньйором (Gemini junior, chat Romfs official link). Збірку не ганяли.
1. Launch OptionBox (раз за сесію): unbound/fake → Later tip або Link and reboot → LinkAllFromRomfsDonor → soft reboot лише на success.
2. Users: один пункт Link Nintendo Account; TE export/apply/probe/dump API прибрано.
3. Gate: applet + suspended (або pmdmnt fail) блокує offer і ручний лінк.
4. i18n en/uk/ru; docs/account-transfer.md §3.3 оновлено. Версія 0.13.670.

## Попередній delivery: v0.13.669 — LinkAllFromRomfsDonor live apply

Статус: прийнято. Live apply з safety filters; версія 0.13.669.

## Попередній delivery: v0.13.668 — LinkKind classification + Fake linked status

Статус: прийнято. ListUsers/StatusLabel Fake vs Official; версія `0.13.668`.

## Попередній delivery: v0.13.667 — ROMFS Kefir donor + LoadRomfsDonorPackage

Статус: прийнято. Kefir у romfs + LoadRomfsDonorPackage; версія `0.13.667`.

## Попередній delivery: v0.13.666 — Complete 0010 /su dump copy lists

Статус: програмну частину реалізовано. Агент не компілює.
1. У `PrepareAccountSaveDump()` TE-скрипт копіює `listing.files.copy()` і рахує `listed` vs `copied` для `/su`, baas, nas, avators, cache. Екран друкує counts через `.str()`, не зайві аргументи `println`.
2. Horizon-шлях теж пише `*_listed` у `result.txt`.
3. У TegraExplorer (окремий репозиторій): `array.foreach` тримає масив у GC і копіює рядки ітератора; `array.copy()` глибоко копіює StringArray; меню `newMenu` прокручується по колу (перший ↔ останній пункт).
4. Версію Sphaira піднято до `0.13.666`. TE користувач збирає сам.

## Попередній delivery: v0.13.665 — Dumb dump of account save 0010 /su

Статус: програмну частину реалізовано. Агент не компілює.
1. Додано `account_link::PrepareAccountSaveDump(bool& out_rebooted)` і пункт Users «Dump account save 0010»: німий зліпок усього `/su` з сейву `0x8000000000000010` для всіх профілів, без відбору BaaS/NAS за ID.
2. Horizon-first: read-only `TryOpenAccountSave()`, копія файлів `/su`, `/su/baas`, `/su/nas`, `/su/avators`, `/su/cache` на `/config/kefir/account_save_dump/<stamp>/`. Якщо сума `su+baas+nas+avators` дорівнює 0 — не успіх, fallthrough у TegraExplorer.
3. TE-шлях: one-shot `/startup.te`, NAND з `App::IsEmummc()`, чотири послідовні `foreach` без вкладеності, без `saveObj.create` / `write` / `delete` / `commit`. `result.txt` — лише counts і `method=horizon|tegra`.
4. UI показує «dumped to SD» лише коли Horizon уже скопіював файли; після handoff у TE тост успіху немає.
5. `PrepareOfficialLinkExport` / `Apply` / layout probe не змінювались.
6. Версію Sphaira піднято до `0.13.665`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.664 — Read-only official-link save layout probe

Статус: програмну частину реалізовано. Агент не компілює.
1. Додано `account_link::PrepareOfficialLinkLayoutProbe()`: one-shot `/startup.te` для TegraExplorer, той самий handoff і `cleanup()`, що в експорті, NAND фіксується з `App::IsEmummc()` під час генерації.
2. Скрипт лише читає `bis:/save/8000000000000010`: `readdir("/su")`, `/su/baas`, `/su/nas`, за потреби `/su/avators`. Жодних `saveObj.create` / `write` / `delete` / `commit`.
3. `has_registry` і `has_profiles` визначаються через listing save-object (`files.contains` / `folders.contains`), не через FatFS `fsexists`.
4. Звіт `result_probe.txt` містить лише counts/booleans (`baas_file_count`, UID-name counters для RFC/Linkalho/raw у обох регістрах, NAS content-match якщо відомий Nintendo Account ID, `has_registry`, `has_profiles`). Імена файлів, UID і Nintendo Account ID у звіт не пишуться.
5. `PrepareOfficialLinkApply()` не змінювався і досі не готовий до запуску.
6. Версію Sphaira піднято до `0.13.664`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.663 — Match BaaS export by verified Nintendo Account ID content

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/account_link.cpp` у функції `PrepareOfficialLinkExport()` вилучено використання списку імен-кандидатів `BaasCandidateNames(uid)` та масиву `cands` для експорту (хелпер залишено для застосування лінку).
2. Реалізовано однопрохідний обхід списку файлів `/su/baas` у скрипті TegraExplorer: для кожного файлу розміром щонайменше 24 байти виконується пряме побайтове порівняння байтів `bbytes[16]..bbytes[23]` (зсув 0x10) з 8 байтами вибраного `nas_id` у little-endian порядку.
3. Усі перевірки рівності та заперечення явно взято в дужки (наприклад, `if (!(bbytes[16] == <byte>))`) для гарантування коректності за лівоасоціативної семантики інтерпретатора TegraExplorer.
4. Додано підрахунок збігів `baasMatchCount` та збереження індексу `selectedIndex`: експорт вимагає рівно одного збігу за вмістом (`if (!(baasMatchCount == 1))`), а повторне зчитування обраного файлу перевіряє `if (!(bbytes.len() >= 24))`. У разі 0 або >1 збігів, або невідповідності довжини відбувається безпечне аварійне завершення на етапі `stage=find_baas` без переходу до NAS чи запису пошкодженого файлу. Після підтвердження єдиного збігу файл зчитується з `baasListing.files[selectedIndex]`, перевіряється та записується як `baas/link.dat`.
5. Версію Sphaira піднято до `0.13.663`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.662 — Fix TegraExplorer NAS export condition precedence

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/account_link.cpp` у функції `PrepareOfficialLinkExport()` виправлено пріоритет операторів у генерованому скрипті TegraExplorer для перевірки довжини імені NAS-файлу: умову змінено з `!match && nfile.len() >= <length>` на `!match && (nfile.len() >= <length>)`.
2. Запобігли некоректному лівоасоціативному обчисленню виразу `((!match && nfile.len()) >= <length>)` інтерпретатором TegraExplorer, що призводило до хибного результату перевірки для реальних файлів NAS.
3. Збережено всі механізми захисту: прапорець `match`, вилучення префікса, пряме порівняння літералів, зчитування, копіювання, очищення та обробку помилок.
4. Версію Sphaira піднято до `0.13.662`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.661 — Avoid nested NAS prefix loop in export

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/account_link.cpp` у функції `PrepareOfficialLinkExport()` повністю видалено генерацію та використання списку `pfxList` і вкладеного циклу `pfxList.foreach()`, щоб запобігти нестабільній поведінці TegraExplorer при вкладених ітераціях.
2. Єдиним циклом для обробки файлів NAS залишено зовнішній `nasListing.files.foreach("nfile")`.
3. Усередині зовнішнього циклу генерується послідовність прямих перевірок для кожного префікса з вектора `NasPrefixes(nas_id)` із захистом довжини `nfile.len() >= <literal-length>`, виділенням префікса через `namePrefix = nfile - (nfile.len() - <literal-length>)`, прямим порівнянням із рядковим літералом префікса та захистом `!match`, що гарантує одноразове копіювання знайденого файлу.
4. Версію Sphaira піднято до `0.13.661`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.660 — Draw System Tools focus text above border

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/ui/menus/tools_menu.cpp` у функції `DrawToolsList()` (System Tools menu) додано третій прохід `List::Draw()` після малювання рамки виділення.
2. У першому проході малюються фони неактивних елементів (`DrawElement(v, ThemeEntryID_GRID)`), заголовки та описи пунктів; у другому проході малюється рамка фокусу `gfx::drawRectOutline(vg, theme, 4.f, v)`; у третьому проході поверх непрозорої заливки рамки малюється заголовок (`ThemeEntryID_TEXT_SELECTED`) та опис (`ThemeEntryID_TEXT_INFO`) активного вибраного пункту.
3. Забезпечено повну видимість тексту активного рядка над контуром та заливкою фокусу при збереженні правильного шарування рамки над сусідніми рядками.
4. Версію Sphaira піднято до `0.13.660`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.659 — Fix live DBI queue compatibility and metrics

Статус: програмну частину виправлено та верифіковано.
1. У `sphaira/source/ui/menus/dbi_menu.cpp` виправлено дедлок у фінальному звіті про відхилені через нестачу місця пакети: збір імен відхилених файлів виконується під м'ютексом `m_mutex`, після чого м'ютекс звільняється і виклики `AddLog()` відбуваються безпечно без блокування черги.
2. У `sphaira/source/yati/source/usb.cpp` у методі `DbiWaitForConnection()` виправлено первинну обробку SPHQ: до початкового списку черги `out_names` додаються лише записи з `selected != 0`, що виключає зайвий початковий аналіз невибраних ігор, а для протоколу SPHQ дозволяється початково порожній список черги з подальшим live-додаванням. Для SPHA та звичайного списку збережено повну сумісність.
3. Версію Sphaira піднято до `0.13.659`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.658 — Sync live DBI queue additions and metrics

Статус: програмну частину реалізовано (збірку WSL скасовано користувачем).
1. У `sphaira/source/yati/source/usb.cpp` розширено `FetchLiveSelection()`: розмір файлу з відповіді SPHQ тепер оновлює внутрішній кеш `m_file_sizes`, забезпечуючи точний `source_size` для динамічно доданих ігор.
2. У `sphaira/include/ui/menus/install_plan.hpp` додано чистий хелпер `PlanEvaluateCandidate()` для перевірки місткості та вибору SD/NAND для нових пакетів з урахуванням політики розташування та резервів пам'яті. Додано модульні тести у `tests/test_install_plan.cpp`.
3. У `sphaira/include/ui/menus/dbi_menu.hpp` та `sphaira/source/ui/menus/dbi_menu.cpp`:
   - `ApplyLiveSelection()` розширено підтримкою динамічного додавання раніше невідомих файлів з `selected=1`: виконання аналізу (`AnalyzeSource`), автоматичний вибір цілі (`InstallTarget::Auto`), безпечне додавання до черги без дублювання та негайний перерахунок плану (`RecomputePlan()`). Невідомі файли з `selected=0` ігноруються до їх вибору.
   - У стані `State::Installing` між пакетами реалізовано прийом нових обраних ігор: валідація місткості щодо залишку вільного місця SD/NAND після виконання всіх запланованих пакетів черги; якщо місця недостатньо, пакет відхиляється (`install_selected = false`, `rejected_no_space = true`), а після завершення всієї черги виводиться повідомлення про помилку `Not installed: <file> — not enough free space`.
   - Зняття вибору з ще не розпочатих пакетів під час інсталяції вимикає їх із плану встановлення.
   - Перебудова `m_plan_total_bytes` із зафіксованого `m_plan_done_bytes` та решти запланованих пакетів для точного динамічного оновлення загального прогресу та ETA.
   - Збережено поведінку пропуску кнопки B, reconnect та сумісність зі старими бекендами.
4. Версію Sphaira піднято до `0.13.658`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.657 — Fix TimeStamp update method & successful WSL build

Статус: успішно скомпільовано в WSL (ReleaseWithInstall).
1. У `sphaira/source/ui/menus/dbi_menu.cpp` у фоновому опитуванні `State::ReviewQueue` виправлено виклик таймера: замінено неіснуючий `last_poll.Reset()` на коректний метод `last_poll.Update()`.
2. Здійснено повну успішну збірку проєкту (`sphaira_nro`, RomFS) у WSL за пресетом `ReleaseWithInstall` без помилок.
3. Версію Sphaira піднято до `0.13.657`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.656 — Opt-in live DBI Backend Qt queue-selection synchronization

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/include/yati/source/usb.hpp` та `sphaira/source/yati/source/usb.cpp` додано підтримку розширення запиту списку `'SPHQ'` (`0x51485053`), парсинг трипольного формату (`filename|size|selected`), збереження прапорця переговорів `m_dbi_selection_sync` та метод `FetchLiveSelection()`.
2. У `sphaira/include/ui/menus/dbi_menu.hpp` та `sphaira/source/ui/menus/dbi_menu.cpp` реалізовано фонове опитування live-вибору бекенда під час `State::ReviewQueue` (з інтервалом ~300 мс без busy loop), фінальне оновлення перед запуском інсталяції (`State::Installing`) та перевірку перед стартом кожного окремого пакета.
3. Скасування вибору пакета в DBI Backend Qt під час очікування або виконання черги автоматично вимикає його в Sphaira без відправлення `InstallFromCollections` або запитів `FileRange`. Локальний вибір користувача на консолі захищено від примусового увімкнення бекендом.
4. Збережено повну сумісність зі старими хостами (двопільний `SPHA` або звичайний список вимикають синхронізацію без повторних запитів), ручним пропуском (кнопка B) та reconnect-логікою.
5. Версію Sphaira піднято до `0.13.656`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.655 — Continue DBI USB installation queue on package skip

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/ui/menus/dbi_menu.cpp` у гілці обробки пропуску пакета користувачем (`if (user_skipped)`) в `Menu::ThreadFunction()` (USB install queue) видалено виклик `ReestablishUsbLink()` та аварійне переривання сесії (`session_failed = true`).
2. Збережено успішне логування `AddLog("Skipped: "_i18n + name, LogKind::Success)` та перехід до наступного обраного пакета в черзі зі скиданням стану пропуску (`m_skip_requested = false`) на початку нової ітерації.
3. Повністю збережено поведінку повторних спроб і відновлення зв'язку при справжніх помилках транспорту (`usb::IsLinkError`) або критичних помилках DBI сесії (`IsDbiSessionError`).
4. Версію Sphaira піднято до `0.13.655`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.654 — Improve DBI installation queue scrolling & screen-off options

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/include/ui/menus/dbi_menu.hpp` та `sphaira/source/ui/menus/dbi_menu.cpp` додано метод `Menu::SetIndex(s64 index)`, який забезпечує збереження контексту одного видимого рядка зверху та знизу від фокусу (`m_list->EnsureVisible(m_index + 1, count)` та `m_list->EnsureVisible(m_index - 1, count)`), повторюючи патерн із `filebrowser.cpp`.
2. Оновлено обидва шляхи переміщення фокусу в черзі встановлення (Review Queue): стандартну навігацію D-pad (`m_list->OnUpdate`) та автоматичний перехід на наступний елемент при виборі кнопкою X («Select»). Скролінг списку починається за один рядок до досягнення верхньої/нижньої межі.
3. У `Menu::UpdateActions()` додано дію кнопки START («Options») під час активного процесу встановлення (`State::Installing`), що дозволяє відкривати сайдбар налаштувань без зупинки та скасування інсталяції. При цьому кнопки X («Cancel queue») та B («Skip package») збережено без змін.
4. У сайдбар налаштувань черги (`Menu::DisplayQueueOptions`) додано підменю «Screen off (Minus)» (`SidebarEntryCallback` з `SetHasSubmenu(true)`), що дозволяє змінювати режим кнопки Minus («Lower brightness», «Turn off backlight», «Screensaver»), таймаут неактивності (Off, 30 s, 1 min, 2 min, 5 min, 10 min), яскравість (1%, 5%, 10%, 20%, 30%, 50%), режим OLED та всі 11 перемикачів полів відображення на скрінсейвері (Clock, Status, Package counter, Current file, Progress bar, Average speed, Time remaining, Elapsed time, Battery, Errors, Speed graph) безпосередньо під час встановлення з негайним збереженням у глобальні налаштування `App`.
5. Версію Sphaira піднято до `0.13.654`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.653 — Fix System Tools list focus border rendering layer

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/ui/menus/tools_menu.cpp` оптимізовано порядок малювання списку `DrawToolsList()` (System Tools menu): розділено відмальовування вмісту та рамки виділення на два проходи `List::Draw()`.
2. У першому проході малюються фони неактивних елементів (`DrawElement(v, ThemeEntryID_GRID)`), текстові мітки та описи всіх пунктів.
3. У другому проході поверх усіх елементів малюється виділена рамка фокусу `gfx::drawRectOutline(vg, theme, 4.f, v)` для обраного пункту (`selected == i`).
4. Забезпечено повну видимість нижньої межі рамки виділення активного рядка над фоном наступного рядка при навігації вниз. Збережено геометрію, колірну схему, прокручування та інтерактивність.
5. Версію Sphaira піднято до `0.13.653`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.652 — Extended bounded payload content scan in File Browser

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/ui/menus/filebrowser.cpp` збільшено обмеження зчитування при розпізнаванні назв RCM `.bin` payload (`IdentifyPayload`) із 64 KiB до 256 KiB (`kMaxScanSize = 256 * 1024`).
2. Забезпечено надійну ідентифікацію корисних навантажень (зокрема `Lockpick_RCM.bin`, `TegraExplorer.bin`), у яких двійкові сигнатури розташовані за межами перших 64 KiB.
3. Збережено консервативне обмежене читання: файли, більші за 256 KiB, не зчитуються повністю; збережено мінімальний поріг 512 байт, список двійкових маркерів, кешування у `FileEntry::title_label` та виведення імені файлу для невідомих payload.
4. Версію Sphaira піднято до `0.13.652`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.651 — Unified manual firmware file & folder picker

Статус: програмну частину реалізовано. Агент не компілює.
1. Уніфіковано вибір ручного джерела встановлення прошивки: при натисканні «Install manually» більше не показується проміжне меню вибору типу («Folder» / «ZIP archive»), а відразу відкривається провідник файлів `filebrowser::Menu`.
2. У `filebrowser::Menu` реалізовано автоматичне визначення вибору:
   - Якщо користувач обирає папку (рядок 0 «Select current folder» або файл всередині папки з прошивкою), вибір підтверджується як папка.
   - Якщо користувач обирає `.zip` файл (наприклад, `firmware.zip`), вибір підтверджується як архів і викликає діалог «Install firmware from this archive?».
3. `kefir_menu` автоматично ідентифікує розширення та направляє потік на розпакування ZIP або безпосередню валідацію/встановлення з папки.
4. Додано переклад нового ключа `"Install firmware from this archive?"` у `en.json`, `uk.json` та `ru.json`.
5. Версію Sphaira піднято до `0.13.651`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.650 — Build fixes and downgrade warning timing correction

Статус: успішно скомпільовано в WSL (ReleaseWithInstall).
1. Виправлено порядок відображення діалогу при ручному встановленні прошивки нижчої версії (`PromptInstallFirmware`): попередження про пониження версії (`DowngradeWarningBox`) показується одразу після валідації вмісту, і лише після підтвердження користувачем виводиться стандартне запитання на встановлення.
2. Виправлено відносний шлях включення заголовка QR-коду `#include "../../web_qr.hpp"` у `kefir_menu.cpp`.
3. Виправлено помилку компіляції в `IdentifyPayload` (`filebrowser.cpp`), де використовувався неоголошений ідентифікатор `FsFileOpenMode_Read` замість `FsOpenMode_Read`.
4. Здійснено повну успішну збірку проєкту (`sphaira_nro`, RomFS) у WSL без помилок.
5. Версію Sphaira піднято до `0.13.650`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.649 — Visual downgrade warning dialog with guide QR code

Статус: програмну частину реалізовано. Агент не компілює.
1. Замінено текстовий `OptionBox` попередження про пониження прошивки (`PromptDowngradeAck`) на виділене модальне діалогове вікно `DowngradeWarningBox` із підтримкою форматованого виводу та векторного рендерингу.
2. Відображено чіткі напівжирні мітки (`Current:`, `Target:`) з вирівнюванням значень у стовпчик, текст ризиків, напівжирну мітку `Fix path:` із візуально вторинним/приглушеним шляхом виправлення, напівжирну мітку `Guide:`, а також сканований QR-код з URL `https://bit.ly/fw_downgrade` через вбудований енкодер `QrCode`.
3. Збережено всі таймінги та семантику перевірки: попереднє попередження перед завантаженням для відомої цільової версії, відкладене попередження після валідації для ручного встановлення (папка/ZIP) або невідомої завантаженої прошивки, уникнення дублювання попередження через `acked_downgrade_fix`, а також безпечне очищення staging-папки manual ZIP при скасуванні.
4. Додано переклади всіх нових фрагментів інтерфейсу в `en.json`, `uk.json` та `ru.json`.
5. Версію Sphaira піднято до `0.13.649`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.648 — Manual firmware ZIP install & staging cleanup safety

Статус: програмну частину реалізовано. Агент не компілює.
1. У Kefir Updater пункт «Install manually» розширено вибором джерела через `PopupList`: вибір існуючої папки з прошивкою або вибір ZIP-архіву через `filepicker::Menu` з фільтром `.zip`.
2. Для ZIP-архівів реалізовано розпакування у фіксовану тимчасову директорію програми `/config/kefir-updater/firmware_manual` через `thread::TransferUnzipAll()`.
3. Виправлено критичний баг рекурсивного видалення вибраної користувачем папки у `CleanupFirmwareFiles`: очищення тепер суворо обмежене виключно шляхами програми (`/firmware`, `/config/kefir-updater/firmware.zip`, `/config/kefir-updater/firmware_manual`), користувацькі папки ніколи не видаляються.
4. Тимчасовий каталог розпакування ZIP тихо видаляється на всіх гілках завершення (скасування користувачем, помилка видобування, помилка валідації, помилка встановлення, успішне встановлення), а також очищається перед кожною новою екстракцією.
5. Вихідний ZIP-архів ніколи не видаляється тихо і зберігається у разі помилок/скасування. Після успішного встановлення та очищення тимчасових файлів користувачеві пропонується діалог «Keep» / «Delete» для оригінального ZIP-файлу.
6. Версію Sphaira піднято до `0.13.648`, оновлено локалізацію та плани.

## Попередній delivery: v0.13.647 — Safe one-shot Hekate payload handoff

Статус: програмну частину реалізовано. Агент не компілює.
1. У Kefirosphere підготовлено окремий Hekate patch для читання й валідації одноразового запиту `/config/kefir/hekate-payload-request.ini` (API v1) та capability marker `/config/kefir/hekate-payload-api.ini`.
2. У Sphaira спільний `utils::rebootToPayload()` переведено з небезпечної підміни `/payload.bin` на валідацію capability marker `[api]` `version=1`, нормалізацію SD-relative шляху, перевірку існування файлу, атомарний запис request-файлу та штатний reboot.
3. У File Browser реалізовано консервативне розпізнавання відомих `.bin` payload за двійковими сигнатурами вмісту з відображенням мітки назви та додано підтверджену дію запуску payload у context menu з перевіркою працездатності API.
4. У `HoldConfirmBox` реалізовано сенсорне утримання екранної кнопки A з тим самим хітбоксом і логікою скидання прогресу при відпусканні або виході за межі кнопки.
5. Версію Sphaira піднято до `0.13.647`, актуалізовано документацію та плани.

## Попередній delivery: v0.13.646 — Fix NAS filename matching in export script

Статус: програмну частину реалізовано. Агент не компілює.
1. Виправлено порівняння префіксів файлів NAS у згенерованому скрипті TegraExplorer (`account_link::PrepareOfficialLinkExport`): небезпечний зріз `ByteArray.slice` замінено на рядкове порівняння префікса `namePrefix = nfile - (nfile.len() - pfx.len())` після перевірки довжини `nfile.len() >= pfx.len()`.
2. Список префіксів `pfxList` генерується як звичайні рядкові літерали на основі C++ помічника `NasPrefixes(nas_id)` без `.bytes()`.
3. Логіка застосування прив'язки, запис у системний сейв, шляхи `/su/baas` і `/su/nas` та вихідний SD-пакет збережені без змін.

## Попередній delivery: v0.13.645 — Pair TegraExplorer save-directory iterator fix with account-link probe

Статус: програмну частину реалізовано. Агент не компілює.
1. У TegraExplorer виправлено час життя позицій ітератора каталогу системного сейву: `save_data_directory_ctx_t` тепер зберігає позиції за значенням, а не вказівники на локальний стек `open_directory`.
2. Виправлення потрібне для достовірного `saveObj.readdir()` під час безпечного лістингу `/su/baas` і `/su/nas` у сейві 0010.
3. Генератор Sphaira та операція експорту в цьому delivery не змінювались: спочатку необхідно повторити лише read-only probe з новим TegraExplorer.

## Попередній delivery: v0.13.644 — Read account link data from su directory in export script

Статус: програмну частину реалізовано. Агент не компілює.
1. Виправлено шляхи доступу до системного сейву 0x8000000000000010 у згенерованому скрипті `PrepareOfficialLinkExport()`: за апаратними даними директорії розташовані за шляхами `/su/baas` та `/su/nas` (замість кореневих `/baas` та `/nas`).
2. Оновлено діагностичні повідомлення скрипта для коректного відображення `/su/baas` та `/su/nas`.
3. Структура вихідного SD-пакета (`baas/link.dat` та `nas/`) та маніфест збережені без змін.

## Попередній delivery: v0.13.643 — Fix account transfer script diagnostics string concatenation

Статус: програмну частину реалізовано. Агент не компілює.
1. Виправлено типи у згенерованих скриптах TegraExplorer: додано `.str()` до всіх числових значень `Int` (`rc`, `baasListing.result`, `nasListing.result`, `nasCopied`, `nasWritten`, `commitRc`, `errors`), які конкатенуються з `String`.
2. Усунено помилки парсера/рантайму TegraExplorer `String + Int`, забезпечено коректний вихід і очищення `/startup.te` на всіх гілках виконання.

## Попередній delivery: v0.13.642 — Auto-select NAND and fix ByteArray writefile in TegraExplorer scripts

Статус: програмну частину реалізовано. Агент не компілює.
1. Автоматичний вибір NAND: скрипти TegraExplorer більше не запитують користувача про вибір `emuMMC/sysMMC`. Контекст монтування фіксується під час генерації скрипту за допомогою `App::IsEmummc()`.
2. Виправлено виклики `writefile()` у згенерованих скриптах: рядкові дані та діагностичні звіти тепер перетворюються на `ByteArray` за допомогою `.bytes()`.
3. Початковий запис діагностики `result.txt` / `result_apply.txt` зі станом `stage=starting` до спроби монтування SYSTEM, фіксація `source_nand` у маніфесті та `target_nand` у результаті застосування.

## Попередній delivery: v0.13.641 — Restore account link status semantics

Статус: програмну частину реалізовано. Агент не компілює.
1. Виправлено регресію статусу прив'язки Nintendo Account: у `QueryHorizonLinkStatus()` відновлено перевірку результату виклику команди 0 (`CheckAvailability`) замість читання IPC bool.
2. `R_SUCCEEDED(rc)` позначає наявність прив'язки (`out_linked = true`), `rc == ResultNetworkServiceAccountRegistrationRequired` позначає відсутність прив'язки (`out_linked = false`), усі інші помилки прокидаються далі для встановлення `linked_known = false`.

## Попередній delivery: v0.13.640 — Build fixes and verification

Статус: програмну частину зібрано та верифіковано.
1. Виправлено компіляцію `account_link.cpp`: замінено неіснуючі константи `Result_FsAlreadyExists`/`Result_FsInvalidPath` на `FsError_PathAlreadyExists`/`FsError_PathNotFound`.
2. Замінено `try...catch` у `ValidateLinkPackage` на `std::strtoull` (сумісність із `-fno-exceptions`).
3. Виправлено екранування змінної `nfile` у генераторі TegraExplorer скриптів для офлайн-експорту прив'язки.
4. Відновлено визначення `ExportAccountSave(std::string& out_dir)`.
5. Успішно зібрано повноцінний NRO `ReleaseWithInstall` у WSL та пройдено всі тести `tests/run.sh`.

## Попередній delivery: v0.13.639 — Prepare offline official link transfer

Статус: програмну частину реалізовано. Агент не компілює.
1. Повністю прибрано небезпечний прямий запис у системний сейв `0x8000000000000010` з живої ОС Horizon та вилучено механізм завершення системних процесів (`pmshellTerminateProgram`, `ShutdownAccountServices`, `OpenAccountSave`, `LinkOneOffline`, `UnlinkOne`, `LinkUsers`, `UnlinkUsers`, `ImportOfficialLink`).
2. Вилучено застарілий заголовок `<switch/services/pm.h>` та виклик `account_link::ImportOfficialLink` із `account_user::ImportUserPack()`. Видалено параметр `terminate_if_needed` із шляхів бекапу акаунтів (`ExportAccountSave`, `ExportUserPack`, `ExportUserPacks`).
3. Реалізовано безпечне опитування Nintendo Account ID (`QueryNintendoAccountId`) через IPC `acc:su` (команда 102 -> команда 120) із гарантованим закриттям сервісів через `ON_SCOPE_EXIT`.
4. Реалізовано підготовку офлайн-експорту прив'язки (`PrepareOfficialLinkExport`):
   - Отримує `NintendoAccountId` обраного профілю через IPC.
   - Створює структуру бандлу `/config/kefir/account_links/<stamp>_<uid_suffix>/` з підкаталогами `baas/` і `nas/`, файлом маніфесту `manifest.txt` (версія 2, `system_save=8000000000000010`, `idgen_0011_included=false`, `source_uid`, `nintendo_account_id`, `baas_file=baas/link.dat`) та `README.txt`.
   - Генерує безпечний одноразовий скрипт `/startup.te` для TegraExplorer із запитом вибору розділу (`emuMMC SYSTEM` / `sysMMC SYSTEM` / `Cancel`), вилученням файлів через `saveObj.read()`, копіюванням супутніх файлів `nas/`, записом `result.txt`, відновленням `payload.bin`, видаленням `/startup.te` і ланцюговим завантаженням `bootloader/update.bin`.
   - Виконує перезавантаження в `TegraExplorer.bin` через `utils::rebootToPayload`.
5. Реалізовано валідацію пакетів прив'язки (`ValidateLinkPackage`) та підготовку перенесення (`PrepareOfficialLinkApply`):
   - Працює суворо в режимі «один донор -> один локальний профіль» на неприв'язаному профілі (`user.linked_known && !user.horizon_linked`).
   - Суворо перевіряє розташування пакета, маніфест v2, цілісність `baas/link.dat` та безпечні імена файлів `nas/`.
   - Дедупліковано кандидатні імена `BaasCandidateNames()`.
   - Генерує скрипт `/startup.te` для TegraExplorer, який створює бекап відкату `rollback_<stamp>/`, виконує видалення старих кандидатних файлів baas цілі та цільових файлів nas лише після успішного запису бекапу відкату з обов'язковою перевіркою кодів помилок `drc`/`wrc`/`crc`, записує `/baas/<target_uid_rfc>.dat` і супутні файли `/nas/`, та фіксує зміни через `saveObj.commit()` лише за умови `!errors && nasWritten > 0` з контролем `commitRc`.
   - Виконує перезавантаження в `TegraExplorer.bin`.
6. Оновлено меню Tools → Users:
   - Додано нові пункти сайдбару «Prepare official-link export» та «Prepare official-link apply».
   - Вилучено небезпечні старі дії «Import official link», «Unlink Nintendo Account», «Offline stub (Linkalho)», «Export official link», «Import official link to all», «Unlink all».
7. Оновлено файли локалізації `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json`, `assets/romfs/i18n/ru.json` та документацію `docs/account-transfer.md`.
8. `kForceUpdateForTest` залишається `false`.

## Попередній delivery: v0.13.638 — Respect system timezone after clock sync

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/ntp.cpp` та `sphaira/include/ntp.hpp` повністю видалено локальне процесне зміщення часу (`g_display_offset`) та публічну функцію `ntp::GetDisplayOffset()`. NTP та системні годинники Horizon використовують POSIX UTC таймстемпи; зміщення часового поясу та літнього часу застосовується виключно нативним шаром libnx / Horizon `localtime_r()`.
2. У `sphaira/source/ui/menus/menu_base.cpp` (`MenuBase::GetPolledData()`) годинник формується безпосередньо з `std::time(NULL)` з подальшим викликом `localtime_r(&t, &data.tm)` без додавання ручних зміщень процесу. Вилучено застарілий заголовок `ntp.hpp`.
3. У `sphaira/source/ntp.cpp` (`RunSync()`):
   - Розрахунок різниці часу виконується прямим порівнянням мережевого та системного часу: `offset = network_time - current_time`.
   - Функція `SetSystemTime(timestamp)` спрощена: повертає тільки `Result`, вилучено параметр `used_fallback`.
   - І прямий запис через `time:su`/`time:s`, і fallback через `set:sys` зводяться до єдиного шляху успішного завершення `RunSync()`, який ставить у чергу виклик `evman::push` з `__libnx_init_time()` та показом сповіщення «Clock synced».
4. Збережено поведінку fallback через `set:sys`: налаштування `NetworkSystemClockContext` на основі UTC NTP таймстемпу та увімкнення `UserSystemClockAutomaticCorrection`.
5. `kForceUpdateForTest` залишається `false`.

## Попередній delivery: v0.13.637 — Export selected official account link (read-only, no kill)

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/include/fs.hpp` виправлено конструктор `FsNativeSave`: параметр `read_only = true` тепер безумовно викликає `fsOpenReadOnlySaveDataFileSystem(&m_fs, save_data_space_id, attr)` для всіх типів сейвів (включно із `FsSaveDataType_System` та `FsSaveDataType_SystemBcat`), замість примусового виклику `fsOpenSaveDataFileSystemBySystemSaveDataId` на запис.
2. У `sphaira/source/account_link.cpp` функцію `TryOpenAccountSave()` переведено на передачу `read_only = true` в `FsNativeSave`. Тепер зонд та читання системного сейву акаунта `0x8000000000000010` справді використовують read-only режим Horizon FS без завершення системних процесів (`account`, `ns`, `pdm`, `bcat`, `olsc`) і повертають фактичний Result, якщо Horizon не надає доступ.
3. Реалізовано функцію `account_link::ExportOfficialLink(uid, out_dir)`:
   - Відкриває системний сейв `0010` у режимі read-only через `TryOpenAccountSave()`.
   - Знаходить `baas/<UID>.dat` обраного користувача через існуючу логіку `FindBaasPath`.
   - Зчитує `NintendoAccountId` (nas_id) за допомогою `NasIdFromBaas`.
   - Створює каталог `/config/kefir/account_links/<stamp>_<uid_suffix>/`.
   - Копіює `baas/<source UID filename>.dat` та всі регулярні файли `nas/`, назви яких відповідають префіксу `NintendoAccountId` (включно із `_id.token`, `_refresh.token`, `_user.json` тощо).
   - Створює текстовий `manifest.txt` (метадані, UID, nas_id, перелік скопійованих файлів, `system_save=8000000000000010`, `idgen_0011_included=false`) без секретів і токенів.
   - Створює `README.txt` із застереженням щодо конфіденційності, поясненням неприпустимості публікації та роз'ясненням щодо консолеспецифічності `0011` (`context.bin`).
   - Системний сейв `0011` (`idgen:/context.bin`) свідомо не включається до бандлу прив'язки.
   - При відсутності даних або помилці чесно повертає Result та логує помилку.
4. У `sphaira/source/ui/menus/users_menu.cpp`:
   - Дію сайдбару оновлено: «Export official link» доступна для обраного профілю (`if (!m_items.empty())`).
   - Якщо Horizon повідомляє, що профіль не прив'язаний (`user.linked_known && !user.horizon_linked`), виводиться зрозуміла помилка без створення бандлу.
   - Якщо статус невідомий, спроба дозволяється за вмістом сейву.
   - Після експорту показується діалог із шляхом до бандлу та явним поясненням, що ця доставка лише експортувала дані без зміни налаштувань акаунтів.
5. Оновлено файли локалізації `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json`, `assets/romfs/i18n/ru.json`.
6. Оновлено `docs/account-transfer.md`.
7. `kForceUpdateForTest` залишається `false`.

## Попередній delivery: v0.13.636 — Query Horizon account link status via read-only IPC

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/account_link.cpp` додано приватний IPC-хелпер `QueryHorizonLinkStatus(uid, out_linked)`, який тимчасово відкриває `acc:su` через `smGetService`, викликає команду 102 (`GetBaasAccountManagerForSystemService`) для отримання `IManagerForSystemService` відповідного UID, та опитує команду 0 (`CheckAvailability`). Успішний результат встановлює `out_linked = true` та повертає успіх, штатний `ResultNetworkServiceAccountRegistrationRequired` (`MAKERESULT(124, 200)`) встановлює `out_linked = false` та повертає успіх, а всі інші помилки прокидаються як збій IPC без змін.
2. Обидва системні сервісні об'єкти гарантовано закриваються на кожному шляху виконання через `ON_SCOPE_EXIT(serviceClose(...))`.
3. `account_link::ListUsers()` переведено на пряме опитування Horizon через новий хелпер без монтування та відкриття Account system save (`0x8000000000000010`) та без зупинки Horizon-процесів. При збої IPC помилка логується разом із UID і Result.
4. Структуру `account_link::User` розширено полем `horizon_linked`.
5. У `sphaira/source/ui/menus/users_menu.cpp` оновлено `Menu::StatusLabel()`: відображається «Linked» коли Horizon повідомляє про доступність Network Service Account (`horizon_linked == true`), «Not linked» коли NSA недоступний (`horizon_linked == false`), та «Link status unavailable» лише у випадку реального збою IPC (`!linked_known`).
6. `kForceUpdateForTest` залишається `false`.

## Попередній delivery: v0.13.635 — Clarify Nintendo Account link status in Users UI

Статус: програмну частину реалізовано. Агент не компілює.
1. У `sphaira/source/ui/menus/users_menu.cpp` оновлено `StatusLabel()`: відображається «Linked» виключно для офіційної прив'язки з токенами (`LinkKind::Official`), «Not linked · FakeLink» для Linkalho/офлайн-заглушки (`LinkKind::Offline`), «Not linked» для локального профілю (`LinkKind::None`), та «Link status unavailable» коли стан сейву акаунта не вдалося безпечно перевірити (`linked_known == false`).
2. Додано нові локалізовані рядки в словники `en.json`, `uk.json` та `ru.json`.
3. Збережено незмінними безпечний зонд Account save без зупинки Horizon-процесів, розмітку списків та `kForceUpdateForTest` (false).

## Попередній delivery: v0.13.634 — Native Horizon user creator, clean avatar action tiles, SGDB explicit game selection

Статус: програмну частину реалізовано. Агент не компілює.
1. Create user відкриває офіційний системний апаратний інтерфейс створення користувача Horizon через нативний libnx API `pselShowUserCreator()`. Після повернення викликається `App::ResetTouchAfterApplet()` та оновлюється список користувачів.
2. У Change avatar виправлено макет плиток дій: «From SD» використовує тему `ThemeEntryID_ICON_FILE` з виділеною нижньою зоною підпису (без накладання тексту на іконку), а плитка SteamGridDB виводить назву рівно один раз.
3. SteamGridDB: спільний двокроковий інтерфейс вибору іконки (в аватарах та редакторі форвардерів). Пошук `/search/autocomplete/` формує повноцінний список ігор `GameSelect`, після вибору конкретної гри завантажуються іконки саме для неї.

## Попередній delivery: v0.13.633 — Remove embedded avatars; create real empty Horizon profile

Статус: програмну частину реалізовано; WSL `ReleaseWithInstall` успішно зібрано.
1. Видалено вісім `#embed` JPEG-пресетів і romfs-аватари; вони не були системними іконками та ламали GCC-збірку.
2. Create user одразу викликає `acc:su`-шлях без підставленого `ImageGetDefaultIcon()`: Horizon створює профіль без довільної картинки.
3. Change avatar лишає аватари реальних профілів, SD/`config/kefir/avatars` і SteamGridDB.

## Попередній delivery: v0.13.632 — Correct unique full-UID user-pack directories

Статус: програмну частину реалізовано. Агент не компілює.
1. Повний UID у назві теки (`<stamp>_<name>_<full_uid>`), а не 8-символьний префікс: навіть профілі з однаковими першими 32 бітами UID не можуть розділити пак.
2. Формат пака, TSV, вибір і сейви не змінено.

## Попередній delivery: v0.13.631 — Hardening: unique UID-suffixed user packs, untruncated TSV, accurate no-save copy

Статус: програмну частину реалізовано. Агент не компілює.
1. Унікальні назви папок паків з суфіксом UID (`<stamp>_<name>_<uid_suffix>`): усунено колізію директорій при мультиселекті профілів з однаковими або схожими іменами.
2. Необмежена довжина рядків `playtime.tsv`: заміна фіксованого буфера snprintf на динамічну збірку `std::string` без обрізання довгих назв ігор з NACP.
3. Точний текст завершення Backup user: повідомлення про наявність сейвів показується лише коли сейви дійсно були обрані.

## Попередній delivery: v0.13.630 — Phase 1: Readable per-user SD backup pack (profile, link, playtime TSV, saves)

Статус: програмну частину реалізовано. Агент не компілює.
1. Backup user створює повноцінний пак профілю на SD: profile.json, avatar.jpg, Nintendo link (baas/nas), README.txt та читабельний playtime.tsv.
2. Вибір ігрових сейвів через SavePickMenu з подальшим бекапом у сумісному ZIP форматі всередині пака під saves/.
3. Безпечна робота з PDM статистикою (pdmqry) без terminate сервісів та ізольований бекап сейвів для кожного окремого профілю. Відновлення сейвів/часу та ремапінг UID у цій фазі не виконуються.

## Попередній delivery: v0.13.629 — NAND dump never kills services; TE dump.te fallback

Статус: програмну частину реалізовано. Агент не компілює.
1. Бекап профілів і годин більше не terminate account/pdm (чорний екран). TryOpen, зайняте пропускаємо.
2. Прогрес словами (Profiles / Play hours). Якщо години зайняті — dump.te в паку і в TegraExplorer/scripts.
3. UI пояснює різницю: Backup user = один профіль, новий UID, без годин.

## Попередній delivery: v0.13.628 — vector folder tiles; avatar search; presets actually load

Статус: програмну частину реалізовано. Агент не компілює.
1. Макет Icon: папка малюється nanovg-контуром (не 50px PNG); прев’ю без чорних плиток; назви по центру, не з’їжджають вліво.
2. Пікер аватарів: 8 пресетів убудовані через #embed; SteamGridDB питає назву (напр. Castlevania).

## Попередній delivery: v0.13.627 — create avatar picker; delete must not kill account

Статус: програмну частину реалізовано. Агент не компілює.
1. Create user: після імені сітка аватарів (вбудовані + поточні профілі + SD + SteamGridDB).
2. Delete user більше не викликає UnlinkUsers/OpenAccountSave (вбивало `account` → порожній список і fatal AM 2011-0301). Бекап пака без terminate.

## Попередній delivery: v0.13.626 — Icon layout: folder silhouette around previews

Статус: програмну частину реалізовано. Агент не компілює.
1. Макет Icon (файловий менеджер і пікер): контур папки завжди видно, прев’ю всередині кишені. Іконки типів (відео тощо) без розтягування.

## Попередній delivery: v0.13.625 — image picker A selects; Fit only after zoom

Статус: програмну частину реалізовано. Агент не компілює.
1. Пікер: A за замовчуванням обирає зображення. «Вписати» лише якщо зум змінено; наступний A знову обирає.

## Попередній delivery: v0.13.624 — compile fix: OptionLong::Get is not const

Статус: зібрано `ReleaseWithInstall` (NRO ок).
1. `IsIconLayout()` більше не `const`: `OptionLong::Get()` мутабельний.

## Попередній delivery: v0.13.623 — do not kill ns during play-hour dump

Статус: програмну частину реалізовано. Агент не компілює.
1. Backup/restore 00F0 більше не зупиняє `ns` (чорний екран / повне вимкнення). Лише pdm, і тільки якщо сейв зайнятий.

## Попередній delivery: v0.13.622 — UID in all user layouts, icon mosaics, avatar crop

Статус: програмну частину реалізовано. Агент не компілює.
1. Users: UID у списку та іконках, не лише в сітці.
2. Icon layout файлів: прев’ю вмісту папки (до 24, далі …), картинки без зміни пропорцій.
3. Вибір аватарки: той самий квадрат-кроп, що для іконок homebrew.

## Попередній delivery: v0.13.621 — Backup user does not delete

Статус: програмну частину реалізовано. Агент не компілює.
1. Backup user лише пише пак на SD, не зупиняє account і не видаляє профіль.
2. Delete user: Hold A, потім опційний бекап, потім видалення.

## Попередній delivery: v0.13.620 — Users layout, create, avatar picker

Статус: програмну частину реалізовано. Агент не компілює.
1. Users: прибрано макет HB Menu; у списку ім’я по центру по вертикалі.
2. Create: Store профілю (ім’я + дефолтна аватарка) **до** CompleteUserRegistration.
3. Аватар з SD: `ImageNormalizeAvatar` (без ліміту 1024px). Пікер зображень — мініатюри (Icon), A = передперегляд, Y / контекст = взяти, L/R гортає.

## Попередній delivery: v0.13.619 — compile fix: ImageConvertToJpg returns ImageResult

Статус: зібрано `ReleaseWithInstall` (NRO ок).
1. Users avatar з SD: `ImageConvertToJpg` повертає `ImageResult`, не `vector<u8>`. Аватар нормалізується через `ImageNormalizeIcon`.

## Попередній delivery: v0.13.618 — Hub encrypts restore; TE is fallback

Статус: програмну частину реалізовано. Агент не компілює.
1. Restore profiles & play hours на цілі: FS-запис у існуючі 0010/00F0, Horizon шифрує ключами цієї консолі. Сирий blob не копіюємо.
2. TegraExplorer `restore.te` лишився запасним, якщо pdm не віддає 00F0. BIS-копія контейнера — не рестор.
3. Бекап як у 0.13.617 (розшифрований пак на SD).

## Попередній delivery: v0.13.617 — NAND transfer: decrypt on Hub, encrypt via TegraExplorer

Статус: програмну частину реалізовано. Агент не компілює.
1. Tools → Users → Backup profiles & play hours: розшифрований дамп 0010/0011/00F0/0041 у `/config/kefir/nand_transfer/<stamp>/`.
2. Рестор не з Horizon: `restore.te` у паку (і romfs). TegraExplorer `readsave` + `write` + `commit()` підписує ключами цілі. Сирий blob не копіюємо.
3. Restore user pack лишається окремо (новий UID, без годин). Інструкція `docs/account-transfer.md`.

## Попередній delivery: v0.13.616 — Users: grid, avatars, create/rename/delete/backup

Статус: програмну частину реалізовано. Агент не компілює.
1. Tools → Users: сітка як ігри (List/Icon/Grid/HB Menu), аватар, бейдж лінку, мультивибір (X/Y).
2. Створити / перейменувати / аватар (SD або SteamGridDB) / видалити через `acc:su` без мережі. Видалення: опційний бекап акаунта, список сейвів з галочками, потім DeleteUser і чистка сейвів.
3. Пак користувача в `/config/kefir/user_packs/` (ім’я, jpg, baas/nas). Restore створює нового юзера.

## Попередній delivery: v0.13.615 — compile fix: system_save_data_id

Статус: зібрано `ReleaseWithInstall` (NRO ок).
1. `FsSaveDataAttribute` у поточному libnx не має `save_data_id`. Відкриття account save `0x8000000000000010` йде через `system_save_data_id`, як у haze/filebrowser.

## Попередній delivery: v0.13.614 — Users: official account import instead of Linkalho-only

Статус: програмну частину реалізовано.
1. Чутка підтверджена: Linkalho пише випадкові baas/nas без `id.token` / `refresh.token`, тож OLSC/BAAS ретраять Nintendo. Офіційна прив’язка на sysNAND + перенос `0x8000000000000010` має справжні токени.
2. Tools → Users: Import official link + Export на SD. Офлайн-заглушка Linkalho лишилась з попередженням. Статус: Linked / Offline stub / Local.

## Попередній delivery: v0.13.613 — Users: offline Nintendo Account link (Linkalho method)

## Попередній delivery: v0.13.612 — Module Manager explains sys-patch / FunControl toggles

## Попередній delivery: v0.13.611 — web-server overlay is translucent again

## Попередній delivery: v0.13.610 — Module Manager counter and filter

## Попередній delivery: v0.13.609 — After reboot same green/grey as the status dot

## Попередній delivery: v0.13.608 — Module Manager on/off colours

## Попередній delivery: v0.13.607 — drop Overlay memory, keep Sysmodule RAM

## Попередній delivery: v0.13.606 — Overlay memory + retry per-module RAM via debug

## Попередній delivery: v0.13.605 — Module Manager shows System pool used/free

## Попередній delivery: v0.13.604 — Module Manager shows per-module RAM, no Task Manager

Статус: програмну частину реалізовано. Агент не компілює.
1. Task Manager прибрано з Tools і з Options Module Manager.
2. Спроба показати RAM кожного модуля — не спрацювала без debug у title-режимі.

## Попередній delivery: v0.13.603 — Task Manager lists every process using RAM

Статус: програмну частину реалізовано. Агент не компілює.
1. Три пули Horizon: Application / Applet / System. Зелений шматок на System — сума listed sysmodules.
2. У списку кожен запущений модуль показує свій розмір (heap+code+stack, не вся mapped memory).
3. Options → Where RAM goes: усі процеси за спаданням RAM.

## Попередній delivery: v0.13.601 — Software without Network Downloads, Tools icons match

Статус: програмну частину реалізовано. Агент не компілює.
1. Tools → Software: рядок Network Downloads прибрано. Custom Link лишається.
2. Іконки Tools знову з прозорим фоном (сіра плитка UI), однаковий cyan. Game Tools / Tools без скругленого чорного квадрата і без іншого відтінку.

## Попередній delivery: v0.13.600 — menu icons, Games options, Tools tiles

Статус: програмну частину реалізовано. Агент не компілює.
1. Undo/Redo у текстовому редакторі дивились не в той бік — стрілки поміняні.
2. ActionIcon на всіх ключових сайдбарах (ігри, файли, homebrew, сейви, редактор). Опції Games: VIEW / LIBRARY / SELECTED, dump settings у Advanced.
3. Плитки Tools і Game Tools перемальовані в одному стилі (cyan на чорному, без підписів). Advanced Options і Tools Tools — наступний крок.

## Попередній delivery: v0.13.599 — nxlink exit must not UAF the widget stack

Статус: програмну частину реалізовано. Агент не компілює.
1. Після прийому NRO через nxlink процес виходить з будь-якого екрана. `m_widgets.clear()` знищував стек знизу вгору: file viewer тримає сирий `Fs*` на file browser під ним → `File::Close` → `IsNative()` по звільненому об’єкту (2168-0001 PC=0x60 / 2168-0002 addr 0).
2. Знищувати віджети згори (`pop_back`). `envSetNextLoad` лише на головному потоці. Нативний файл закривати без віртуального виклику в `Fs`.

## Попередній delivery: v0.13.598 — svcGetSystemInfo takes a Handle

Статус: зібрано. nxlink після збірки.
1. libnx: `svcGetSystemInfo(out, type, handle, pool)` — передаємо `INVALID_HANDLE`.

## Попередній delivery: v0.13.597 — Module Manager RAM uses svcGetSystemInfo

Статус: програмну частину реалізовано. Агент не компілює.
1. `InfoType_TotalPhysicalMemorySize` немає в поточному libnx. Замість нього `svcGetSystemInfo` + `SystemInfoType_*`.

## Попередній delivery: v0.13.596 — Remote editor shows when the session ended

Статус: програмну частину реалізовано. Агент не компілює.
1. Закриття зі Switch або кнопки Close: повноекранна завіса, редактор read-only.
2. Кнопка Close; втрата зв’язку теж показує, що сесія скінчилась.

## Попередній delivery: v0.13.595 — Remote editor Save does not close the session

Статус: програмну частину реалізовано. Агент не компілює.
1. Save to Switch / Ctrl-S лише записує файл, сесія на консолі лишається.
2. Save and Close — зберегти і закрити редактор.

## Попередній delivery: v0.13.594 — Tools: Game Tools, stubs, Module Manager

Статус: програмну частину реалізовано. Агент не компілює.
1. Games / Saves / Cheats — тека **Game Tools**.
2. Tools: Module Manager + заглушки (Wi-Fi, Users, System info, SD zeros, parental, junk).
3. Module Manager прибрано з Kefir Settings. RAM used/free, пам’ять модуля, сортування, Options і Info з GitHub.

## Попередній delivery: v0.13.593 — L3 launches the focused game from the list

Статус: програмну частину реалізовано. Збірка і деплой у цьому ж delivery.
1. L3 у списку ігор запускає поточну гру, як у картці гри.

## Попередній delivery: v0.13.592 — MTP Games: drop Unmerged, localize folders, Readme

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Unmerged прибрано (дубль Merged). Both: Merged, Separate, Forwarders.
2. Назви тек мовою інтерфейсу (укр. Злиті / Окремі / Форвардери). English aliases лишаються.
3. У корені і в кожній теці — Readme.txt (Прочитай.txt) з поясненням.

## Попередній delivery: v0.13.591 — MTP Games: Forwarders folder, Unmerged, no mix

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Форвардери (TID 0x05… і HBL 0x03) більше не в Merged/Unmerged/Separate. Окрема тека Forwarders.
2. Both: Merged, Unmerged (усі BASE/UPD/DLC одним списком), Separate, Forwarders.

## Попередній delivery: v0.13.590 — no dynamic_cast (Switch builds with -fno-rtti)

Статус: програмну частину реалізовано (SW-DONE). Агент не компілює.
1. `App::Draw` і `CloseFileBrowsersOnUsbMount` не можна кастити через `dynamic_cast` (`-fno-rtti`). Замість цього віртуальні `Widget::BlocksDrawUnder` і `OnUsbMountRemoved`.

## Попередній delivery: v0.13.589 — Games list uses the same badge pills as icon layouts

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Макет список більше не пише `[S|N|b|u]` літерами збоку. Ті самі кольорові бейджі, що на обкладинках (GC / Base / DLC / Update / LayeredFS), плюс SD і NAND повними підписами; розмір лишається справа.

## Попередній delivery: v0.13.588 — NAND/SD move UI stays alive (Cancel works)

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Під час ProgressBox не малювати сітку ігор (GPU чекав кадр, B не доходив).
2. Move без CPU FastLoad (глушить GPU). YieldType_ToAnyThread.

## Попередній delivery: v0.13.587 — Move to NAND/SD shows the current NCA

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. CreatePlaceHolder на гігабайти тримає бар на 0%. Тепер рядок: Allocating/Copying · Application · Program (1/4) + розмір; бар 0–100% по поточному NCA.

## Попередній delivery: v0.13.586 — flash plug/unplug without file-browser magic

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Після MTP-кабеля Poll більше не ігнорує usb:hs (ранній return usbds&&!haze). PC unplug = usb:ds Detached → haze::Exit одразу.
2. UMS (vid/pid) → попап відкрити саме цю флешку. Витяг → закрити файловий браузер, якщо він на ній.

## Попередній delivery: v0.13.585 — USB flash while MTP is on must not crash

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. MTP у налаштуваннях більше не хапає порт як gadget, поки немає ПК (LowPower). Інакше флешка (теж device) валить usb:ds Instruction Abort.
2. Без ПК — usbhsfs (хост), флешка монтується і пропонується файловий браузер. ПК — MTP.

## Попередній delivery: v0.13.584 — Games storage bars: blue only where the title lives

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. NAND/EmuNAND і microSD синіють лише якщо на цьому носії є дані поточної гри (або виділення). Гра лише на SD — синя SD; лише на NAND — синій NAND; спліт — обидва.

## Попередній delivery: v0.13.583 — MTP Games dumps: compatible / separate / both

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Диск Games був порожній: haze відкриває `/games`, парсер чекав `/Merged`. FixPath як у Saves.
2. Settings → MTP storages → Dump format: сумісний (один NSP), окремі файли, обидва (Merged/ + Separate/).

## Попередній delivery: v0.13.582 — USB plug: flash → file browser, PC → MTP

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Флешка (usbhsfs): попап «відкрити у файловому браузері».
2. Кабель у ПК (LowPower VBUS, немає mass-storage): одразу MTP; після від'єднання порт повертається USB storage.

## Попередній delivery: v0.13.581 — GitHub release; restore only-if-newer auto-update

Статус: SW-DONE. Деплой на rashevskyv/kefir-hub.
1. `kForceUpdateForTest = false`: оновлення лише якщо latest новіший.
2. Реліз 0.13.581 (changelog з 0.13.565).

## Попередній delivery: v0.13.580 — Ask dialog: Skip on its own row, Minus shortcut

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Skip на всю ширину над Пізніше/Оновити, текст по центру. Minus — скіп, Plus — оновити, B — пізніше.

## Попередній delivery: v0.13.579 — Ready — restart actually restarts

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Auto-update → Update now, коли «Ready — restart»: тап перезапускає Kefir Hub.

## Попередній delivery: v0.13.578 — Drop On demand; Ask has Later / Skip / Update

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Режими: Off / Silent / Ask. On demand прибрано (старий 3 → Silent).
2. Ask: Пізніше (знову спитає), Пропустити це оновлення (до наступного релізу), Оновити. У папці лишається рядок пропущеної версії.

## Попередній delivery: v0.13.577 — Auto-update is a folder in General

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Автооновлення — папка зверху в Загальний, не окрема категорія зліва.

## Попередній delivery: v0.13.576 — Header counter no longer jitters 1px

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. «2 / 13» у хедері прив’язаний правим краєм; слот як «13 / 13», щоб 1 і 2 не зсували лічильник.

## Попередній delivery: v0.13.575 — About: parse ### headings, split Update vs notes

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Changelog: `###` / `##` рендеряться як заголовки, без решіток.
2. About: **X** — оновити список змін; **A** — оновити Kefir Hub, якщо є реліз.

## Попередній delivery: v0.13.574 — Saves settings: filters, backup defaults, WebDAV

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Settings → Збереження: показ (встановлені / видалені / бекапи), джерело за замовчуванням, стиснення, автобекап при restore, шляхи, auto-sync і WebDAV.

## Попередній delivery: v0.13.573 — Network: FTP/MTP toggles then folders

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Мережа: FTP і MTP — увімк/вимк на самій сторінці; під кожним папка (логін/порт і сховища).

## Попередній delivery: v0.13.572 — Auto-update category at the top of Settings

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Settings: перша категорія «Автооновлення» — режим (Off / Silent / Ask / On demand) з окремим описом кожного, Update now, пропущена версія.
2. Ask: попап Skip / Update; Skip запам'ятовує цей реліз і більше не питає.

## Попередній delivery: v0.13.571 — Y toggles boolean lines

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. У редакторі **Y** — Toggle (`=0`/`=1` / true/false / u8!0x0). **A** знову лише Edit line.

## Попередній delivery: v0.13.570 — Toggle 0/1 on A; fix swkbd overflow

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. У редакторі **A** / подвійний тап на `=0`/`=1`/`true`/`false`/`u8!0x0` перемикає буль, не відкриває клавіатуру. Підказка A: Toggle.
2. swkbd: буфер = max length, `swkbdClose`, скидання тачу після аплету.

## Попередній delivery: v0.13.569 — Silent update ToFileAsync needs StopToken

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Silent `ToFileAsync` передає `StopToken{}` — інакше static_assert.

## Попередній delivery: v0.13.568 — Non-silent update uses the download icon

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Ask / On demand: прогрес апдейта в тому ж ProgressBox, що й скачування (іконка, L3 згортає). Silent лишає «Оновлення» в хедері.

## Попередній delivery: v0.13.567 — Force auto-update for mode testing

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Тимчасово: latest з GitHub завжди вважається оновленням, навіть якщо встановлена версія вища. Режими Silent / Ask / On demand / Off лишаються. Після QA — повернути порівняння версій.

## Попередній delivery: v0.13.566 — Auto-update modes, header progress, no EmuNAND badge

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Тихе оновлення в фоні; тег `v0.13.x` порівнюється коректно. У хедері під час завантаження — «Оновлення» + прогрес замість смуг NAND/SD.
2. Settings: Off / Silent / Ask / On demand + Update now.
3. Бейдж EmuNAND прибрано: підпис смуги вже EmuNAND.

## Попередній delivery: v0.13.565 — File open menu for text + correct expand-range glyphs

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. **A** на текстовому файлі (до відкриття): View / Edit / Edit on PC / phone. Те саме в Options, вище в списку.
2. Легенда розширення діапазону: **L/R + Up/Down**, не SL/SR.

## Попередній delivery: v0.13.564 — Stop auto-forwarder from stalling launch/exit

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Скан HOME-іконів більше не читає NACP кожної гри — лише 0x05 / відомі HBL Title ID.
2. Запуск з Album / nxlink більше не ставить другий Kefir Hub, якщо ікон уже є (інший path-hash).

## Попередній delivery: v0.13.563 — Ask to save when closing the remote editor from the Switch

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. **B** на консолі, якщо в браузері були незбережені правки: «Зберегти зміни?» Не зберігати / Зберегти.

## Попередній delivery: v0.13.562 — Remote editor fills the browser window

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Поле CodeMirror більше не лишається смугою ~300px: CSS після CDN і flex розтягують редактор на всю висоту вікна.

## Попередній delivery: v0.13.561 — Edit on PC / phone from the file browser

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Options файлового браузера: для текстового файлу, який можна редагувати (не read-only, ≤ 4 МБ), є «Редагувати на ПК / телефоні» поруч із Edit.

## Попередній delivery: v0.13.560 — Full-page remote file editor (CodeMirror)

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. «Редагувати на ПК / телефоні» відкриває повноекранний редактор, не картку з textarea.
2. CodeMirror 5 з CDN: номери рядків, підсвітка за розширенням, Ctrl+S. У NRO лише HTML-оболонка.
3. Вставка кількох рядків лишається маленьким вікном Send.

## Попередній delivery: v0.13.559 — Text editor: expand an existing line range

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Коли вже є виділення рядків: **Y** і Дії → «Розширити діапазон».
2. **L** + вгору/вниз і лівий стік рухають верхню межу; **R** + вгору/вниз і правий стік — нижню. Вгору від верхньої межі розширює, вниз звужує; вгору від нижньої піднімає край (звужує), вниз розширює.
3. У легенді: верхня межа `L` / лівий стік, нижня `R` / правий стік. **A** Готово, **B** скасовує зсув меж.

## Попередній delivery: v0.13.558 — Text editor: one outline around the whole line selection

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Виділені через Дії рядки підсвічуються однією рамкою, розтягнутою на весь діапазон, як рамка поточного рядка.

## Попередній delivery: v0.13.557 — Reuse remote input for file edit (no extra editor page)

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Прибрано окрему JS-сторінку редактора. І «редагувати файл», і «вставити під курсор» — той самий `/input` + `RequestRemoteText`, лише `multiline`.

## Попередній delivery: v0.13.556 — Paste from PC / phone at the cursor

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. У діях текстового редактора: «Вставити з ПК / телефона» — багаторядковий remote input, текст лягає під поточний рядок.

## Попередній delivery: v0.13.555 — Text editor on PC / phone

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. У файловому редакторі Options → «Редагувати на ПК / телефоні»: той самий QR/HTTP, простий JS-редактор (підсвітка, undo/cut, Ctrl+S). Save надсилає файл назад і пише на SD.

## Попередній delivery: v0.13.554 — Picker Create Folder defaults to the archive name

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. «Створити теку» в пікері розпакування підставляє ім'я архіву (без .zip). Користувач може стерти або перейменувати.

## Попередній delivery: v0.13.553 — Direct Download: Enter sends the URL

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Enter у полі Direct Download надсилає адресу на консоль (як кнопка Send).

## Попередній delivery: v0.13.552 — Picker Options: Create Folder + Close picker

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Контекстне меню пікера: «Створити теку» і «Закрити вибір папки».

## Попередній delivery: v0.13.551 — Folder picker: Create Folder, minus returns to extract

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Options пікера — лише «Створити папку», без copy/cut.
2. «−» / B у корені закриває лише пікер і повертає до варіантів розпакування.

## Попередній delivery: v0.13.550 — Zip extract: stable row lines, selection off the stripes

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Роздільники в дереві архіву більше не їздять разом із курсором.
2. Рамка вибору не наїжджає на золоті смуги зверху/знизу списку дій — відступ з обох боків.

## Попередній delivery: v0.13.549 — Zip extract: smaller checks, X/Y like file browser, no "new folder" row

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Чекбокси дерева — той самий `drawCheckbox`, трохи менші (16px).
2. **X** — позначити поточний рядок, **Y** — інвертувати вибір (як у файловому браузері).
3. Прибрано «Розпакувати в нову папку…». Нову папку створюєш у Options огляду («Extract files to…»), перший рядок обирає поточну.

## Попередній delivery: v0.13.548 — Auto-forwarder deletes our previous HOME icon

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Новий Kefir Hub форвардер більше не сідає поруч зі старим: інші ікони Kefir Hub/Sphaira з префіксом 0x05 прибираються, щойно є поточний.
2. Запуск зі старого нашого ікона — це не «вже новий»: ставиться поточний Title ID; той, з якого зайшли, зніметься наступного разу (його не можна видалити, поки він запущений).
3. args більше не дублюються — hash Title ID збігається з тим, що ставить install_forwarder.

## Попередній delivery: v0.13.547 — After installing an app from zip: launch, don't open the folder

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Після «Встановити програму» лишається Keep/Delete zip, друге питання — запустити додаток, не файловий браузер.

## Попередній delivery: v0.13.546 — Full-screen zip extract: tree, checkboxes, named folder

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Після Direct Download zip — повноекранне меню, не смужка знизу. Зверху дерево архіву з галочками (Y — усі/жоден).
2. Якщо в архіві один `.nro`: пояснення, що **програму** буде встановлено в `/switch/<назва>`; дія «Встановити програму в папку switch».
3. Розпакування: файли в `/downloads`; у нову папку `/downloads/<ім'я архіву>`; файли в обрану папку; у нову папку всередині обраної (ім'я за замовченням — як у zip).

## Попередній delivery: v0.13.545 — Forwarder NACP video capture Manual, not Auto

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. `nacp.video_capture` 0x1 (Manual), не 0x2 (Auto). Auto валив `am` 2128-0007 при запуску ікона з HOME.

## Попередній delivery: v0.13.544 — OptionBox: glyph is part of the caption

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Гліф знову в одному рядку з підписом, весь напис по центру. Шрифт 26px, менше лише якщо не влазить.

## Попередній delivery: v0.13.543 — Center OptionBox buttons, honest forwarder notices

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Кнопки OptionBox знову по центру; гліф фіксований; шрифт 26, зменшення лише якщо не влазить.
2. Повідомлення автофорвардера пояснюють навіщо і говорять, що іконка/видалення вже виконуються, не «якщо треба».

## Попередній delivery: v0.13.542 — Forwarder capture, URL scheme collapse, friendly download errors

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Автофорвардер знову дозволяє скріншоти і відео (для дебагу).
2. Подвійні `http://`/`https://` у Direct Download згортаються (поле в браузері і обробка на консолі).
3. Поганий або недоступний URL — звичайне пояснення і кнопка «Виправити URL» (клавіатура консолі або знову з ПК).

## Попередній delivery: v0.13.541 — Zip: "Install NRO to /switch" label

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Якщо в zip один `.nro`, перша дія називається «Встановити NRO в папку switch», а не довгим шляхом.

## Попередній delivery: v0.13.540 — OptionBox: wrap long button labels, keep + glyph fixed

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Довгі підписи кнопок OptionBox (наприклад «Відкрити у файловому браузері») більше не вилазять за край.
2. Іконка + / B лишається зліва без прокрутки; текст зменшується і переноситься на два рядки.

## Попередній delivery: v0.13.539 — Zip extract: one NRO → /switch/stem, no root option

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Якщо в архіві рівно один `.nro` (будь-яка глибина тек) — перша дія: покласти **цей файл** у `/switch/<назва без .nro>/<файл.nro>`. Рядок показує шлях у zip і куди ляже.
2. Завжди: «Extract all to /downloads» (зі списком кореня архіву: `atmosphere/, switch/, …`) і «Extract to...».
3. Окремої «Extract to root» немає. Корінь SD лише якщо користувач сам обере його в огляді.

## Попередній delivery: v0.13.538 — Remote input: no Paste on desktop

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. **Desktop**: кнопку Paste прибрано. Під полем інструкція «Paste or type the address, then Send».
2. **Phone**: кнопку залишено — фокус поля і long-press Paste (Clipboard API на HTTP усе одно не працює).

## Попередній delivery: v0.13.537 — GameCard row, dead install screens, zip extract defaults

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. **Games / GameCard**: картридж як окремий рядок у списку ігор, синя обводка на іконці, підпис `[GC]`.
2. **A3 — мертві екрани**: видалено IRS, `firmware_menu`, екрани FTP/MTP Install. FTP/MTP лишились сервісами.
3. **Header**: версія Kefir над IP/Wi-Fi; повні підписи SysNAND/EmuNAND і microSD, бейджі по центру.
4. **Auto-forwarder**: план за джерелом запуску (новий / старий / Album); не чіпати title, з якого зайшли; системний TID більше не «завжди старий».
5. **Remote input Paste**: на HTTP LAN Clipboard API недоступний — без червоної помилки, фокус поля і Ctrl+V/Cmd+V.
6. **Custom Links / Direct Download zip**: після завантаження — огляд структури архіву, дефолтний шлях, Browse / Cancel, Keep/Delete zip, відкрити файловий браузер. Голе `.nro` → `/switch/<stem>/<file>`; NRO в теці → `/switch` (тека вже `switch` → `/`); кілька кореневих тек → `/downloads/<stem>`; інакше `/downloads`. Zip скачується в `/downloads`.
7. **Тести**: `tests/test_game_list_info.cpp`, `tests/test_zip_extract_plan.cpp`, оновлені forwarder/header тести. Агент їх не запускав.

## Попередній delivery: v0.13.536 — Fix: nacp_util::GetName in forwarder_auto_install.cpp

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Виправлення звернення до функції `nacp_util`**:
   - Замінено неіснуючий `nacp_util::GetTitle` на `nacp_util::GetName(control_data->nacp)`.
2. **Верифікація**:
   - Пройдено всі 25 наборів unit-тестів у WSL (all green).

## Попередній delivery: v0.13.535 — Auto-Forwarder: Fast-Path Check & User Files Protection

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Миттєва перевірка наявності форвардера KefirHub (Fast Path)**:
   - Спочатку виконується виклик `nsIsAnyApplicationEntityInstalled` для `kefirhub_tid`. Якщо форвардер уже встановлений, потік миттєво завершує роботу (0.1 мс), не зачіпаючи базу ігор і не проводячи жодних операцій.
2. **Захист файлів користувача**:
   - Повністю виключено будь-які операції з папкою `/Games` чи файлами на SD-картці.
3. **Очищення лише за відсутності форвардера**:
   - Лише якщо форвардер KefirHub відсутній, видаляються застарілі форвардери HBL/HBM та встановлюється форвардер KefirHub.
4. **Верифікація**:
   - Пройдено всі 25 наборів unit-тестів у WSL (all green).

## Попередній delivery: v0.13.534 — Auto-Forwarder: Legacy HBL Forwarder Deletion & Native KefirHub Generator

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Видалення застарілих форвардерів та файлів**:
   - Автоматичне видалення старих NSP із `/Games` (`Homebrew menu*.nsp`, `hbmenu*.nsp`, `hblauncher*.nsp`).
   - Видалення застарілих встановлених форвардерів за Title ID (`03DB1280BD84000`, `03DB12780BD84000`, `010000000000100D`, `050000000000100D`) та за назвами через `nsListApplicationRecord` і `nsDeleteApplicationCompletely`.
2. **Внутрішня генерація форвардера KefirHub**:
   - Безшумна генерація форвардера через `owo` із параметрами: 39-бітний адресний простір, вимкнені знімки/відео, вимкнений вибір профілю та вимкнений дебаг.
3. **Unit-тестування**:
   - `tests/test_forwarder_auto_lifecycle.cpp` (47 checks).
4. **Верифікація**:
   - Пройдено всі 25 наборів unit-тестів у WSL (all green).

## Попередній delivery: v0.13.533 — Auto-Forwarder Thread Lifecycle: Guaranteed threadClose & Application Bypass

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Розділення станів життєвого циклу потоку (`sphaira/source/forwarder_auto_install.cpp`)**:
   - `g_thread_created` відстежує необхідність виклику `threadClose()`.
   - `g_thread_active` відстежує активність функції потоку.
   - Встановлено детерміноване очищення ресурсів у `StopCheck()`: `threadWaitForExit()` і `threadClose()` викликаються завжди, коли потік створювався.
2. **Пропуск створення потоку в режимі Application**:
   - У `StartCheck()` додано ранню перевірку `App::IsApplication()`, що запобігає виділенню зайвих 64 KiB пам'яті під стек.
3. **Unit-тестування**:
   - Створено `tests/test_forwarder_auto_lifecycle.cpp` (19 checks).
4. **Верифікація**:
   - Пройдено всі 25 наборів unit-тестів у WSL (all green).

## Попередній delivery: v0.13.532 — HBL Loader: 64-bit Integer-Safe NRO Bounds & Pre-Body Read Validation

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **64-бітний розрахунок меж data+BSS (`hbl/source/main.c`)**:
   - Переведено всі арифметичні операції для `seg2_size + bss_size`, вирівнювання за сторінками та зміщень сегментів на тип `u64`.
   - Додано перевірки переповнення суми, переповнення вирівнювання та перевірку `seg2_off + rw_size <= g_heapSize`.
2. **Рання валідація меж до зчитування тіла NRO**:
   - Усі перевірки валідності заголовка, сегментів та розмірів коду/купи перенесено безпосередньо перед зчитуванням залишку NRO з SD-карти.
3. **Host unit-тести (`tests/test_hbl_nro_reader.cpp`)**:
   - Додано `CheckRwSizeBounds` та перевірки на перехоплення 32-бітного обгортання.
4. **Верифікація**:
   - Пройдено всі unit-тести у WSL.

## Попередній delivery: v0.13.531 — HBL Loader: Validated Contiguous OverrideHeap & Checked NRO Bounds

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Валідація неперервної купи в HBL (`hbl/source/main.c`)**:
   - Додано `findUsableHeapRange`: сканування через `svcQueryMemory` для пошуку найбільшого валідного неперервного діапазону з `MemType_Heap`, `Perm_Rw`, `attr == 0` та вирівнюванням по 4 КБ.
   - Усунуто передачу сирого арифметичного діапазону, що міг включати недоступні сторінки або чужі буфери.
2. **Перевірка меж NRO та BSS**:
   - Замінено застарілий TODO на строгі перевірки `total_size <= g_heapSize`, `rw_size` та захист від цілочисельного переповнення перед читанням та відображенням пам'яті.
3. **Host unit-тести**:
   - Розширено `tests/test_hbl_nro_reader.cpp` з моделюванням дірок у пам'яті та граничних випадків розмірів NRO.
4. **Верифікація**:
   - Успішно пройдено всі unit-тести у WSL, зібрано цільовий бінарник.

## Попередній delivery: v0.13.530 — NRO Launch Handoff: Clean envSetNextLoad & Redundant FS Commit Removal

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Аналіз покрокової бісекції (v0.13.469 -> v0.13.487)**:
   - Встановлено, що у версії `v0.13.469` (`1bb99c4`) запуск дочірніх NRO (включаючи `NX-Activity-Log` та `Pipe NSX`) працював стабільно.
   - У коміті `v0.13.487` (`32f655b`) в `launch_internal` перед `envSetNextLoad` було додано подвійний FS commit: `fsdevCommitDevice("sdmc")` та `fsFsCommit(fs)`.
2. **Очищення handoff-ланцюжка в NRO (`sphaira/source/nro.cpp`)**:
   - Вилучено передчасні виклики `fsdevCommitDevice` та `fsFsCommit` безпосередньо перед `envSetNextLoad` та `evman::push`.
3. **Очищення виходу програми (`sphaira/source/main.cpp`)**:
   - Усунуто дублювання `fsFsCommit` після `fsdevCommitDevice("sdmc")` у `userAppExit()`.
4. **Верифікація**:
   - Успішно пройдено всі 24 набори unit-тестів у WSL (all green).

## Попередній delivery: v0.13.529 — Forwarder Editor: List Null Pointer Safety & D-Pad Focus Transitions

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Аналіз краш-звітів на карті пам'яті (`F:\atmosphere\crash_reports`)**:
   - `01787411683_03db12780bd84000.log`, `01787411672_03db12780bd84000.log` та `01787411496_03db12780bd84000.log`.
   - Виявлено розіменування нульового покажчика `x24` (`controller`) у `sphaira::ui::List::OnUpdateGrid` на адресі `PC = sphaira + 0xf08f4`, коли `Forwarder Editor` викликав `m_list->OnUpdate(nullptr, ...)` при фокусі на іконці.
2. **Захист покажчиків у List (`sphaira/source/ui/list.cpp`)**:
   - Додано перевірки `if (controller)` перед зверненням до методів контролера у `OnUpdateGrid` та `OnUpdateHome`.
   - Додано захист `if (!touch)` у `StepFling` та `OnTouchScroll`.
3. **Навігація фокусу D-Pad у Forwarder Editor (`sphaira/source/ui/forwarder_editor.cpp`)**:
   - Додано перехід фокусу `DOWN` / `RIGHT` з іконки до списку налаштувань, та `LEFT` / `UP` (з першого рядка) назад на іконку.
4. **Host unit-тести та верифікація**:
   - Створено `tests/test_list_null_safety.cpp` (6 перевірок).
   - Пройдено всі 24 набори unit-тестів у WSL.

## Попередній delivery: v0.13.528 — HBL Loader Fix: Exact NRO Segment Sizing, Applet/Application Mode Detection & Heap Restoration

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Аналіз краш-звітів на карті пам'яті (`F:\atmosphere\crash_reports`)**:
   - `01787404697_010000000000100d.log` (Album mode) та `01787404688_03db12780bd84000.log` (Forwarder mode).
   - Виявлено: у режимі Альбому передавався хибний `AppletType_SystemApplication`, провокуючи виділення 64.5 МБ у 32 МБ пам'яті аплету; у форвардері функція `calculateMaxHeapSize` безумовно крала 96 МБ купи (`size -= 0x6000000`), що спричиняло збій на адресі `0x25fb8f000`.
2. **Динамічне визначення типу додатку та відновлення розміру купи (`hbl/source/main.c`)**:
   - Реалізовано `getIsApplication()` (запит до ядра через `svcGetInfo(..., InfoType_IsApplication)` та `pm:shell`).
   - Реалізовано `getIsAutomaticGameplayRecording()` (через `nsGetApplicationControlData`), усунуто безпідставне урізання 96 МБ для хоумбрю.
   - Динамічно передається `AppletType_LibraryApplet` (в альбомі) або `AppletType_SystemApplication` (у форвардері/тайтлі).
3. **Детерміноване читання NRO та обнулення BSS (`hbl/source/main.c`)**:
   - Читання `NroStart` (16 байт), `NroHeader` (112 байт) та виключно корисного навантаження `header->size - 0x80`.
   - Явне обнулення пам'яті BSS (`memset(nrobuf + header->size, 0, total_size - header->size)`).
4. **Звільнення графічних ресурсів GPU (`sphaira/source/main.cpp`)**:
   - Додано `nvExit()` до `userAppExit()` для чистого закриття сесій Tegra перед стартом наступного NRO.
5. **Host unit-тести та верифікація**:
   - Створено та розширено `tests/test_hbl_nro_reader.cpp` (528,394 перевірки).

## Попередній delivery: v0.13.527 — Software Menu Visual Separation: Dedicated Bottom Network Section

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Виділення секції мережевих завантажень у Software Menu (`sphaira/source/ui/menus/settings_menu.cpp`)**:
   - `Network Downloads` та `Custom Link` перенесено в самий низ списку.
   - Додано чіткий розділювач `MakeHeader("NETWORK DOWNLOADS")` з горизонтальною лінією (HR).
   - `DrawActionListItem` підтримує відмальовку заголовків, а `SoftwareMenu::SetIndex` пропускає неінтерактивні рядки.

## Попередній delivery: v0.13.526 — Menu Structure: Network Downloads & Custom Link to Software Menu

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Очищення меню оновлень Updater (`sphaira/source/ui/menus/kefir_menu.cpp`)**:
   - Меню `Updater` звільнено від мережевих завантажень GitHub/Custom Link і сфокусовано виключно на KEFIR та FIRMWARE.
2. **Перенесення до Software Menu (`sphaira/source/ui/menus/settings_menu.cpp`)**:
   - Додано `Network Downloads` та `Custom Link` безпосередньо в список додаткових програм `Software`.
3. **Оновлення описів карток у Tools Menu (`sphaira/source/ui/menus/tools_menu.cpp`)**:
   - Актуалізовано підписи карток `Updater` та `Software`.

## Попередній delivery: v0.13.525 — Graceful download cancellation & universal remote text/URL transfer

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Коректна обробка скасування в AppStore (`sphaira/source/ui/menus/appstore.cpp`)**:
   - Реалізовано перехоплення `Result_TransferCancelled` у `InstallApp` та `EntryMenu::UpdateOptions` з виведенням спокійного інформаційного діалогу замість червоної аварійної помилки.
2. **Універсальний модуль дистанційного введення тексту `ui::remote_input` (`sphaira/include/ui/remote_input.hpp`, `sphaira/source/ui/remote_input.cpp`, `sphaira/source/web.cpp`)**:
   - Реалізовано вибір між клавіатурою Switch та передачею з телефону/ПК через QR-код і локальний веб-ендпоінт `/input`.
3. **Пряме завантаження `.nro` та `.zip` (`sphaira/source/ui/menus/ghdl.cpp`, `sphaira/include/path_util.hpp`)**:
   - Розширено валідатор `IsValidDirectDownloadUrl` та реалізовано збереження `.nro` у `/switch/` із можливістю негайного запуску.

## Попередній delivery: v0.13.524 — USB 3.0 indicator, waiting screen anti-overlap layout & screensaver clean title display

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Індикатор USB 3.0 та швидкість з'єднання (`sphaira/include/ui/nvg_util.hpp`, `sphaira/source/ui/nvg_util.cpp`, `sphaira/source/ui/menus/menu_base.cpp`, `sphaira/source/ui/menus/dbi_menu.cpp`)**:
   - Створено функцію малювання векторної іконки USB `gfx::drawUsbIcon(vg, x, y, size, col)` (стандартний тризуб з верхньою стрілкою, квадратною та круглою гілками).
   - Інтегровано перевірку конфігурації `usb30_force_enabled` з `system_settings.ini` та опитування апаратного лінку `usbDsGetSpeed(&speed)`.
   - У глобальному хедері `MenuBase::DrawChrome` над графіками пам'яті виведено бейдж `[ USB 3.0 ]` поряд із MTP та FTP.
   - На екрані очікування встановлення по USB (`dbi_menu.cpp`) розміщено статусний бейдж на `y = 180.f` з іконкою USB та деталізацією швидкості (`USB 3.0 SuperSpeed (5 Gbps)` або `USB 2.0 High Speed (480 Mbps)`).
2. **Динамічна розмітка екрану очікування без перекриттів (`sphaira/source/ui/menus/dbi_menu.cpp`)**:
   - Основні інструкції розміщено на `y = 250.f`, розрахунок висоти тексту виконується динамічно через `nvgTextBoxBounds`.
   - Попередження про Applet Mode винесено в окрему виділену плашку з м'яким бордером і гарантованим відступом строго нижче основного тексту (`std::max(text_bounds[3] + 35.f, 470.f)`), що виключає будь-яке налізання елементів.
3. **Чисте відображення назви гри у скрінсейвері (`sphaira/source/ui/menus/dbi_menu.cpp`, `sphaira/source/ui/screensaver.cpp`)**:
   - Усунуто додавання технічних хешів/імен NCA/NCZ файлів (`.nca`/`.ncz`) до назви гри, залишено лише змістовні статуси (наприклад, "Updating ncm database").
   - У `screensaver.cpp` розширено ширину блоку до 840 пікселів (з безпечним 50px OLED burn-in запасом) та додано плавне адаптивне зменшення шрифту з лівим прив'язуванням для дуже довгих назв, гарантуючи, що перші слова назви гри ніколи не зрізаються.
4. **Зменшення шрифту пам'яті та усунення налізання на рядок прошивки/Кефіру (`sphaira/source/ui/menus/menu_base.cpp`)**:
   - Розмір шрифту `storage_font` для міток `NAND`, `SD` та чисел пам'яті зменшено з 19.05px до 15.5px.
   - Позицію `badge_y` піднято до 17.f, що забезпечило комфортний вертикальний відступ і ліквідувало налізання верхніх країв літер пам'яті на рядок системної версії/Kefir.
5. **Статусний бейдж EmuNAND/SysNAND та 3-сторонній симетричний розподіл у хедері (`sphaira/source/ui/menus/menu_base.cpp`, `sphaira/source/hats_version.cpp`)**:
   - Створено статусний бейдж режиму NAND з адаптивним розгортанням (`EmuNAND` зеленого кольору в EmuNAND при наявності місця, `E` при обмеженому просторі з USB 3.0; аналогічно `SysNAND`/`S`).
   - Усунуто дублювання `|E`/`|S` наприкінці системного рядка версії Atmosphere/Kefir.
   - Інформацію згруповано у два блоки: Блок 1 (бейджі MTP, FTP, USB 3.0, EmuNAND/E) та Блок 2 (версія Кефіру та ОС).
   - Реалізовано динамічний розрахунок рівних інтервалів: відстань від лівого краю сховища до Блоку 1, відстань між Блоком 1 і Блоком 2, та відстань від Блоку 2 до правого краю сховища абсолютно однакові ($M = (W_{span} - (W_1 + W_2)) / 3$).
6. **Unit-тести та верифікація**:
   - Створено `tests/test_screensaver_title.cpp` (11 checks), `tests/test_usb3_indicator.cpp` (12 checks) та оновлено `tests/test_header_service_indicators.cpp` (31 checks).
   - Пройдено всі 22 набори host unit-тестів у WSL (`tests/run.sh`).
   - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.523 — Header Kefir version, System OS Firmware & EmuNAND/SysNAND indicator

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Зчитування та форматування системної версії (`sphaira/include/hats_version.hpp`, `sphaira/source/hats_version.cpp`)**:
   - Реалізовано функцію `getKefirVersion()`, що перевіряє `/switch/kefir-updater/version` або `HATS_VERSION.txt`.
   - Реалізовано `getSystemVersionString()`, що формує рядок за форматом системних налаштувань Switch: `<Kefir> · <FW>|AMS <AMS>|<E/S>` (наприклад `Kefir 802 · 19.0.1|AMS 1.8.0|E` або `19.0.1|AMS 1.8.0|E`).
2. **Відображення у хедері (`sphaira/source/ui/menus/menu_base.cpp`)**:
   - Одноразово кешовано результат у `MenuBase::GetPolledData` (нульовий оверхед).
   - У `MenuBase::DrawChrome` рядок виводиться на висоті `y = 19.f` з правим вирівнюванням по осі `storage_right`, гармонійно доповнюючи бейджі MTP/FTP зліва.
3. **Unit-тести та верифікація**:
   - Оновлено `tests/test_header_service_indicators.cpp` (28 checks) з тестуванням усіх варіацій (Kefir + FW + AMS + EmuNAND/SysNAND).
   - Пройдено всі 20 наборів host unit-тестів у WSL (`tests/run.sh`).

## Попередній delivery: v0.13.522 — Exact NAND-edge boundary calculation & conditional anti-overlap marquee

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Точне позиціонування зони скролінгу від правого краю блоку NAND (`sphaira/source/ui/menus/menu_base.cpp`)**:
   - Розрахунок геометрії сховища перенесено перед відмальовуванням першого рядка (Wi-Fi/IP).
   - Обчислюється точна координата правого краю тексту пам'яті NAND: `nand_right = value_x + nand_val_w`.
   - Ліва межа доступного вікна мережі встановлюється як `net_left = nand_right + 12.f`, що дає максимально можливий простір до `bar_right`.
   - Якщо довжина рядка мережі `net_text_w > (bar_right - net_left)` (є нахльост на блок NAND), вмикається плавний біжучий рядок через `m_scroll_network.Draw` з відсіканням строго на `net_left`.
   - Якщо нахльосту немає (`net_text_w <= bar_right - net_left`), рядок відображається статично вирівняним праворуч без скролінгу.
2. **Unit-тести та збірка**:
   - Оновлено [**`tests/test_header_network_layout.cpp`**](tests/test_header_network_layout.cpp) з моделюванням точного розрахунку відступу від правого краю тексту NAND.
   - Пройдено всі 20 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.521 — Header MTP and FTP background service indicators above NAND/SD

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Інтеграція статусу фонових служб (`sphaira/include/ui/menus/menu_base.hpp`, `sphaira/source/ui/menus/menu_base.cpp`)**:
   - У структуру `PolledData` додано прапорці `bool mtp_running{}` та `bool ftp_running{}`.
   - Метод `MenuBase::GetPolledData` щосекунди оновлює статус служб через легкі виклики `sphaira::haze::IsRunning()` та `sphaira::ftpsrv::IsRunning()`.
2. **Геометричне розміщення та колірне кодування в інтерфейсі (`MenuBase::DrawChrome`)**:
   - Індикатори `MTP` та `FTP` розміщено над рядками сховищ NAND/SD на базовій лінії `y = 29.f` (`storage_mid - storage_gap * 1.5f`), що забезпечує ідеальний 20-піксельний вертикальний крок відносно рядка NAND (`y=49.f`) та SD (`y=69.f`).
   - Позиціонування починається від лівої межі блоку пам'яті (`label_x`), утворюючи єдину вертикальну вісь з підписами дисків.
   - Коли служба активна та слухає порт/з'єднання, відповідний напис забарвлюється у яскраво-зелений колір `nvgRGBA(76, 190, 120, 255)`; коли неактивна — у приглушений сірий колір теми `ThemeEntryID_TEXT_INFO`.
3. **Unit-тести та збірка**:
   - Створено набір unit-тестів [**`tests/test_header_service_indicators.cpp`**](tests/test_header_service_indicators.cpp) (20 checks: перевірка розрахунку координат, вертикального кроку, ширини та мапінгу кольорів).
   - Пройдено всі 20 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL з розгортанням на `F:\switch\kefir-hub.nro`.

## Попередній delivery: v0.13.520 — Header network SSID & IP anti-overlap scrolling marquee

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Ізоляція та скролінг мережевого статусу (`sphaira/include/ui/menus/menu_base.hpp`, `sphaira/source/ui/menus/menu_base.cpp`)**:
   - У клас `MenuBase` додано екземпляр `ScrollingText m_scroll_network`.
   - У `MenuBase::DrawChrome` обчислюються точні межі слота мережевого тексту: ліва межа `net_x = start_x` (що гарантує щонайменше 10 пікселів відстані від правого краю індикаторів NAND/SD пам'яті `storage_right`), права межа `bar_right = 1220.f`, ширина `net_w = bar_right - net_x`.
   - Якщо довжина рядка (SSID + IP) перевищує ширину виділеного слота, активується плавний скролінг через `m_scroll_network.Draw` з апаратним scissor-відсіканням. Довгі назви точок доступу більше ніколи не налізають на числа та смуги сховища NAND.
   - Якщо рядок уміщується в слот, він вирівнюється праворуч без скролінгу, зберігаючи естетичне вирівнювання з годинником та батареєю.
2. **Unit-тести та верифікація**:
   - Створено набір unit-тестів [**`tests/test_header_network_layout.cpp`**](tests/test_header_network_layout.cpp) (25 перевірок: форматування SSID/IP/LAN/No Internet, розрахунок меж слота, гарантія відсутності перекриття NAND/SD, активація скролінгу).
   - Пройдено всі 19 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.519 — AppStore EntryMenu layout anti-overlap & instant launch state transition

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Фіксація координат кнопок та блоку метаданих (`sphaira/source/ui/menus/appstore.cpp`)**:
   - Кнопки дій позиціонуються від нижнього краю робочої зони (`bottom_y = 630.f`), залишаючи 16 пікселів відступу від футера.
   - Метадані розміщені з інтервалом 26 пікселів (шрифт 18), що виключає перекриття або налізання на футер.
2. **Миттєве оновлення статусу та відображення версії RetroArch**:
   - У колбеку успішного завершення завантаження `install` прописується `m_entry.installed_version = "Nightly"` та статус `Installed`, що негайно активує кнопку «Запустити» (`Launch`).
   - При відкритті `EntryMenu` версія оновлюється з файлу `info.json` або зчитується з NACP бінарника.
3. **Збірка та деплой**:
   - Пройдено всі 18 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Зібрано версію `v0.13.519` та оновлено `kefir-hub.nro` і `hbmenu.nro` на диску `F:`.

## Попередній delivery: v0.13.518 — RetroArch 7z PhysFS stream extractor & Nightly MD5 bypass

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Підтримка `.7z` та пряме потокове розпакування (`sphaira/source/ui/menus/appstore.cpp`)**:
   - Інтегровано `physfs` (`libphysfs.a`) для обробки 7z-архівів.
   - Реалізовано функцію `ExtractPhysfsArchive`, яка рекурсивно створює каталоги та записує файли на карту пам'яті.
   - Пропущено MD5-перевірку для динамічних релізів RetroArch Nightly.
   - Реалізовано запис метаданих `info.json` (`"version": "Nightly"`).
2. **Збірка та деплой**:
   - Пройдено всі 18 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Зібрано версію `v0.13.518` та оновлено `kefir-hub.nro` і `hbmenu.nro` на диску `F:`.

## Попередній delivery: v0.13.517 — AppStore installed version display, clean network handover & LibRetro Nightly resolver

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Відображення встановленої версії в AppStore (`EntryMenu::Draw`)**:
   - Додано поле `installed_version` у структуру `Entry`.
   - Реалізовано зчитування встановленої версії з `info.json` та з заголовка NACP бінарника (`nacp_util::GetDisplayVersion`).
   - У меню картки додатка виводиться рядок `installed: <версія>` (підсвічується кольором теми, якщо є оновлення).
2. **Підміна джерела для RetroArch на LibRetro Nightly Buildbot (`appstore_util.hpp`)**:
   - Створено функції `IsRetroArchPackageName` та `ResolveAppstoreZipUrl`.
   - Завантаження RetroArch перенаправлено на актуальний офіційний білд `RetroArch.7z` з LibRetro Nightly.
   - Для застарілих версій RetroArch дія `Launch` замінюється на `Update`.
3. **Коректний порядок деініціалізації мережі (`sphaira/source/main.cpp`)**:
   - `socketExit()` тепер викликається строго перед `nifmExit()` у `userAppExit()`.
   - Додано 50 мс паузу перед `appletUnlockExit()` для повного очищення IPC-дескрипторів ядра Horizon OS.
4. **Збірка та деплой**:
   - Пройдено всі 18 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано та скопійовано бінарники `kefir-hub.nro` і `hbmenu.nro` на карту пам'яті `F:`.

## Попередній delivery: v0.13.516 — AppStore EntryMenu launch confirmation guard

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Захист від випадкового запуску NRO у меню AppStore (`sphaira/source/ui/menus/appstore.cpp`)**:
   - Додано діалогове вікно підтвердження `OptionBox` ("Launch [App]? No / Yes") для дії запуску `Launch` в `EntryMenu`.
   - Запобігає автоматичному / миттєвому закриттю Sphaira та запуску сторонніх застосунків при відкритті інформаційної картки вже встановленого додатка у магазині.
2. **Збірка та деплой**:
   - Пройдено всі 17 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано та скопійовано бінарник `kefir-hub.nro` на карту пам'яті `F:`.

## Попередній delivery: v0.13.515 — UPA-13: Confirmed ROM database compatibility aliases

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Розширення таблиці асоціацій баз даних ROM (`sphaira/include/ui/menus/filebrowser_assoc.hpp`)**:
   - Додано базу даних `NEC - PC Engine SuperGrafx` (папки `supergrafx`, `pce-sg`, `pcesg`), підтверджену конфігурацією ядер Mednafen у RomFS.
   - Додано взаємне співставлення для `Nintendo - Family Computer Disk System` та `Nintendo - Famicom Disk System` (папка `fds`).
   - Додано базовий запис `SNK - Neo Geo` (папка `neogeo`) поряд із Pocket/Color/CD.
2. **Unit-тести та збірка**:
   - Розширено unit-тести [**`tests/test_tico_assoc.cpp`**](tests/test_tico_assoc.cpp) (20 checks passed).
   - Пройдено всі 17 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.514 — UPA-11: GameCard theme roles & safe storage ratio

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Безпечне обчислення коефіцієнтів сховища (`sphaira/include/storage_ratio.hpp`)**:
   - Створено функції `CalculateStorageUsedRatio` та `CalculateStorageFreeGb` з гарантованим захистом від ділення на нуль, `total <= 0`, `free < 0` та `free > total`.
2. **Оновлення інтерфейсу меню GameCard (`sphaira/source/ui/menus/gc_menu.cpp`)**:
   - У `Menu::Draw` для фону смуг використано `ThemeEntryID_PROGRESSBAR_BACKGROUND` замість `ThemeEntryID_BACKGROUND`, що забезпечує коректний вигляд у темах зі складним або зображувальним фоном.
   - У `Menu::UpdateStorageSize` додано обнулення змінних перед опитуванням сховищ.
3. **Unit-тести та збірка**:
   - Створено unit-тести [**`tests/test_storage_ratio.cpp`**](tests/test_storage_ratio.cpp) (14 checks passed).
   - Пройдено всі 17 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.513 — UPA-10B: Localized UTF-8 MTP display names

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Збереження Unicode у MTP папках (`sphaira/include/title_export_name.hpp`, `sphaira/source/haze_helper.cpp`)**:
   - Реалізовано UTF-8 санітизацію (`SanitizeUtf8TitleName`) та безпечну транкацію на межі code point (`TruncateUtf8`).
   - `FormatMtpGameDirName` зберігає локалізовані назви (кирилиця, українські/європейські літери, ієрогліфи, емодзі) та гарантує збереження суфікса `[TitleID]`.
   - Забезпечено коректний fallback: Localized name -> English slot 0 -> English slot 1 -> Title ID.
2. **Unit-тести та збірка**:
   - Розширено [**`tests/test_title_export_name.cpp`**](tests/test_title_export_name.cpp) тестами Unicode/кирилиці, емодзі, безпечної UTF-8 транкації та MTP фолбеків (42 checks passed).
   - Пройдено всі 16 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.512 — UPA-10A: Tested usable-title core & ASCII-safe NSP export helper

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Ядро визначення придатної назви та NSP експорт (`sphaira/include/title_export_name.hpp`)**:
   - Реалізовано єдиний помічник `ResolveExportTitleName` із суворою ієрархією: American English (slot 0) -> British English (slot 1) -> Localized/Current name -> Title ID hex fallback (`%016llX`).
   - Додано семантичний валідатор `IsUsableTitleName` (відхиляє порожні та рядки з одних пробілів, крапок або підкреслень після санітизації).
   - Забезпечено коректне скорочення (`TruncateTitleName`) з гарантованим запасом місця під суфікс `[TitleID][vVersion][Type].nsp`.
2. **Інтеграція в експорт NSP (`sphaira/source/title_nsp.cpp`)**:
   - `BuildNspPath` та `BuildMergedNspEntry` використовують єдиний перевірений хелпер.
3. **Unit-тести та збірка**:
   - Створено автономний набір host unit-тестів [**`tests/test_title_export_name.cpp`**](tests/test_title_export_name.cpp) (24 перевірки).
   - Пройдено всі 16 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.511 — UPA-09: Forwarder editor touch/controller focus matrix

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Виправлення матриці фокусу сенсора та контролера (`sphaira/source/ui/forwarder_editor.cpp`)**:
   - Усунуто безумовний вихід з `Update` при `m_icon_focused`: тепер сенсорні події для правого списку обробляються через `m_list->OnUpdate(nullptr, touch, ...)`.
   - Дотик або скролінг списку переносить фокус з іконки на рядок без хибних активацій.
   - Кнопка `RIGHT` переносить фокус на список без виклику дії рядка, кнопка `LEFT` повертає фокус на іконку.
   - Кнопка `A` активує лише активний елемент (іконку або вибраний рядок списку).
2. **Unit-тести та збірка**:
   - Пройдено всі 15 наборів host unit-тестів та shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.510 — UPA-08A/B: Raw FTP mutation adapter & discovery gate

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Дизайн та фіксація точок інтеграції (`UPA-08A`)**:
   - Визначено точки успішного завершення операцій у `src/platform/nx/vfs/vfs_nx_fs.c` (`vfs_fs_close`, `vfs_fs_unlink`, `vfs_fs_rmdir`, `vfs_fs_mkdir`, `vfs_fs_rename`).
   - Підтверджено потокобезпечність викликів сповіщень з worker-потоку ftpsrv (`ueventSignal`).
2. **Адаптер мутацій у ftpsrv (`sphaira/cmake/patch_ftpsrv.cmake`, `sphaira/source/ftpsrv_helper.cpp`)**:
   - Додано C ABI інтерфейс `vfs_nx_set_mutation_callback` для відправлення сповіщень про події створення/видалення/перейменування файлів та папок.
   - Підключено обробник `FtpMutationCallback` у `ftpsrv_helper.cpp` до спільної політики Homebrew (`NotifyFileCreated`, `NotifyFileDeleted`, `NotifyDirectoryCreated`, `NotifyDirectoryDeleted`, `NotifyRename`).
3. **Unit-тести, shape-checks та збірка**:
   - Створено автономний перевірочний скрипт `tests/test_patch_ftpsrv.sh` та підключено його до `tests/run.sh`.
   - Пройдено всі 15 наборів host unit-тестів та обидва shape-checks (libhaze + ftpsrv).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.509 — UPA-07B: MTP delete/rename/directory operations mutation coverage

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Повне покриття мутацій у MTP VFS (`sphaira/source/haze_helper.cpp`)**:
   - `DeleteFile`: після успішного видалення та коміту надсилає `NotifyFileDeleted(routed_path.s)`.
   - `RenameFile`: після успішного перейменування надсилає `NotifyRename(routed_old.s, routed_new.s, false)`.
   - `CreateDirectory`: після створення директорії надсилає `NotifyDirectoryCreated(fixed_path)`.
   - `DeleteDirectoryRecursively`: після рекурсивного видалення надсилає `NotifyDirectoryDeleted(fixed_path)`.
   - `RenameDirectory`: після перейменування директорії надсилає `NotifyRename(fixed_old, fixed_new, true)`.
2. **Точність та детермінізм**:
   - Жодна операція, що завершилася з помилкою, не викликає сповіщення.
   - Усі шляхи оцінюються через спільну політику, захищаючи від непотрібних оновлень поза межами `/switch` та кастомних search roots.
3. **Unit-тести та збірка**:
   - Пройдено всі 15 наборів host unit-тестів та shape-check у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.508 — UPA-07A: MTP upload/final-close shared mutation policy integration

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Інтеграція спільної політики мутацій у MTP VFS (`sphaira/source/haze_helper.cpp`)**:
   - `FsProxy` веде облік відкритих на запис файлів `m_open_write_files` (`std::map<fs::File*, std::string>`), зберігаючи маршрутизований шлях `routed_path.s`.
   - Замінено глобальний прапорець `m_notify_homebrew`: сповіщення `ui::menu::homebrew::NotifyFileCreated(written_path)` тепер надсилається суворо після успішного закриття файлу `CloseFile()` і тільки для файлів, які зачіпають каталог Homebrew.
2. **Підтримка прямих та перенаправлених записів**:
   - Покриваються прямі завантаження в `/switch`, вкладені папки, редиректи з кореня та довільні кастомні search roots.
   - Не-homebrew файли (наприклад, `.mp4`, `.nsp`, `.sav`) ігноруються автоматично без зайвих сканувань меню.
3. **Unit-тести та збірка**:
   - Пройдено всі 15 наборів host unit-тестів та shape-check у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.507 — UPA-06: Shared homebrew mutation policy & complete Web success coverage

Статус: реалізацію виконано та верифіковано:
1. **Спільна політика мутацій Homebrew (`sphaira/include/path_util.hpp`, `sphaira/source/ui/menus/homebrew.cpp`)**:
   - Реалізовано перевірки меж шляхів `path::IsSubpathOf`, розширення `path::IsNroPath` та визначення впливу на каталог `path::PathAffectsHomebrew` (з урахуванням дефолтного `/switch` та кастомних директорій пошуку).
   - Додано функції сповіщення про створення, видалення та перейменування файлів і директорій (`NotifyFileCreated`, `NotifyFileDeleted`, `NotifyDirectoryCreated`, `NotifyDirectoryDeleted`, `NotifyRename`, `NotifyPathChanged`).
2. **Інтеграція у Web сервер (`sphaira/source/web.cpp`)**:
   - `HandleUpload`: викликає `NotifyFileCreated` після фіналізації та коміту файлу.
   - `HandleDelete`: викликає `NotifyDirectoryDeleted` або `NotifyFileDeleted` після успішного видалення файлу/каталогу на SD карті.
3. **Unit-тести та збірка**:
   - Додано 53 перевірки в `tests/test_path_util.cpp` (283 checks passed).
   - Успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.506 — UPA-05: Playtime worker UI-thread isolation & race elimination

Статус: реалізацію виконано та верифіковано:
1. **Ізоляція фонового воркера від UI даних (`sphaira/source/ui/menus/game_menu.cpp`)**:
   - `Menu::LoadPlaytime()` створює знімок `app_ids` та окремий контейнер `PlaytimeResult` перед запуском `ProgressBox`.
   - Воркер взаємодіє виключно з цими структурами, повністю усунувши стан гонитви з рендером та іншими UI-потоками.
2. **Детерміноване застосування результатів**:
   - Результати з буфера воркера записуються в `m_entries` виключно в колбеку `done` на UI thread і тільки у разі успішного завершення операції (`R_SUCCEEDED(rc)`).
   - У разі скасування або помилки зміни не застосовуються до інтерфейсу.
3. **Unit-тести та збірка**:
   - Пройдено всі 15 наборів host unit-тестів та shape-check у WSL (`tests/run.sh`).
   - Успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.505 — UPA-04A: MTP zero-byte upload support & patch shape verification

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Zero-byte payload підтримка в libhaze (`sphaira/cmake/patch_libhaze.cmake`)**:
   - Уточнено умову розрахунку `file_size` під час `SendObject`: змінено `data_header.length > sizeof(PtpUsbBulkContainer)` на `>= sizeof(PtpUsbBulkContainer)`.
   - Забезпечено коректне встановлення нульового розміру файлу замість залишення fallback-розміру `4_GB`.
2. **Ідемпотентність та shape-check падінь**:
   - Патч підтримує повторне застосування та міграцію з проміжних версій патчу.
   - Створено ізольований перевірочний тест `tests/test_patch_libhaze.sh`, що перевіряє застосування патчу до вихідного коду, ідемпотентність при повторному запуску та завершення з очікуваною помилкою при спотвореній формі.
3. **Unit-тести та збірка**:
   - Підключено перевірку форми патча в `tests/run.sh` (всі тести зелені).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.504 — UPA-03: Centralized GitHub and direct URL validation

Статус: реалізацію виконано та верифіковано:
1. **Централізована валідація URL GitHub (`sphaira/include/path_util.hpp`)**:
   - Реалізовано `path::ParseGitHubRepoUrl(url)`: перевіряє схему (http/https), хост (github.com / www.github.com), відсікає `.git` та trailing slash, вимагає валідні ідентифікатори owner/repo без спецсимволів, відхиляє userinfo, порти, параметри запиту, фрагменти та directory traversal.
2. **Валідація прямих посилань та ZIP-файлів**:
   - Реалізовано `path::IsValidDirectAssetUrl(url)` та `path::IsValidDirectZipUrl(url)`.
   - Оновлено `LoadEntriesFromPath`, `Download`, та `OpenDirectLinkPrompt` у `sphaira/source/ui/menus/ghdl.cpp`.
3. **Unit-тести та збірка**:
   - Покрито повним набором тестів у `tests/test_path_util.cpp` (230 checks passed).
   - Успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.503 — UPA-02B: GHDL ZIP type detection & safe non-ZIP destination

Статус: реалізацію виконано та верифіковано:
1. **Комплексне визначення ZIP-архівів (`sphaira/include/path_util.hpp`, `sphaira/source/ui/menus/ghdl.cpp`)**:
   - Реалізовано `path::IsZipAsset(content_type, filename, url)`: визначає ZIP за наявністю підрядка `"zip"` у `content_type`, суфікса `.zip` у назві файлу або в шляху URL (ігноруючи параметри запиту `?` та фрагменти `#`).
2. **Безпечне встановлення не-ZIP ассетів**:
   - Для файлів без явної конфігурації `entry.path` призначається безпечна цільова директорія `/switch/<sanitized-asset-name>` (замість небезпечного перезапису/видалення кореня `/`).
   - Якщо `entry.path` вказує на директорію, до неї коректно дописується ім'я файлу.
   - Додано перевірку валідності імені файлу `path::IsSafeFilename` та вилучення імені `path::ExtractBasename`.
3. **Unit-тести та збірка**:
   - Додано нові тести у `tests/test_path_util.cpp` (193 checks passed).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.502 — UPA-02A: GitHub downloader operation identity, cancel & temp isolation

Статус: реалізацію виконано та верифіковано:
1. **Ізоляція та очищення тимчасових файлів (`sphaira/source/ui/menus/ghdl.cpp`)**:
   - Перед стартом мережевої передачі та при завершенні (включно зі збоями/скасуванням через `ON_SCOPE_EXIT`) детерміновано видаляються тимчасові файли `ghdl.temp` та `direct_link.zip`.
   - Унеможливлено використання застарілих даних від попередніх або перерваних операцій завантаження.
2. **Фазовий контроль скасування (Phase Gates)**:
   - Додано строгі перевірки `pbox->ShouldExit()` перед початком завантаження через curl, після його завершення перед модифікацією файлової системи, а також перед викликом розпакування/перейменування.
   - При скасуванні повертається стандартний `Result_TransferCancelled`, який коректно перехоплюється без показу помилкових діалогових вікон збоїв мережі.
3. **Строгий захист сигналу Homebrew**:
   - Виклик `homebrew::SignalChange()` перенесено строго в блок `if (R_SUCCEEDED(rc))`, усунувши помилкові перезавантаження каталогу при скасуванні або невдалому завантаженні.
4. **Збірка та тести**:
   - Пройдено всі 15 наборів host unit-тестів у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.501 — UPA-01: GitHub downloader callback ownership & selection safety

Статус: реалізацію виконано та верифіковано:
1. **Ліквідація global static стану (`sphaira/source/ui/menus/ghdl.cpp`)**:
   - `static std::vector<GhApiEntry> gh_entries` у `DownloadEntries()` замінено на операційно-локальний `auto gh_entries = std::make_shared<std::vector<GhApiEntry>>()`.
   - Тепер кілька послідовних запитів завантаження не можуть перетерти дані релізів один одного.
2. **Безпека володіння пам'яттю та усунення UAF / висячих посилань**:
   - Усунено небезпечне захоплення за посиланням `&asset_entry` та збереження сирих вказівників `const AssetEntry*` на елементи тимчасових векторів усередині відкладених лямбд (`PopupList`, `OptionBox`, `ProgressBox`).
   - Застосовано `std::optional<AssetEntry>` та безпечне захоплення параметрів за значенням (`[entry, asset_entry, matched]`).
3. **Захист від виходу за межі діапазону (Out-of-Bounds Guards)**:
   - Додано перевірки меж індексів `op_index` для вибору релізу (`!op_index || *op_index < 0 || static_cast<size_t>(*op_index) >= gh_entries->size()`) та ассету (`static_cast<size_t>(*op_index) >= api_assets.size()`).
   - Додано перевірку на порожній список ассетів з показом інформаційного вікна замість спроби розіменування.
4. **Збірка та тести**:
   - Пройдено всі 15 наборів host unit-тестів та dead symbol guard у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.500 — NexLink / DBI return crash

Статус: реалізацію виконано, переглянуто senior-review та прийнято апаратним тестуванням:
1. **Доказова діагностика**:
   - Найновіший report `F:\atmosphere\crash_reports\01787234167_03db12780bd84000.log` точно відповідає WSL ELF за Build ID `29AC83A4D4BFAD5E9014FC1C660A598627CDA520`.
   - У 17/20 звітів allocator виконує валідний split top-chunk і падає на `str x1, [x3,#8]`, причому `Exception Address == X3 + 8`; `ScanThemes`, `opendir` та декодери зображень є лише першими алокаціями, що доходять до недоступної сторінки.
   - Решта 3/20 звітів належать окремому старому stack overflow логера, усуненому у v0.13.491.
2. **Спільний root-boundary fix**:
   - Додати strong `__libnx_initheap()` без алокацій: пройти loader-provided OverrideHeap через `svcQueryMemory`, відкинути успадковані `Perm_None` / `IsBorrowed` діапазони та передати newlib найбільший суцільний `MemType_Heap + Perm_Rw + attr == 0` сегмент.
   - Для запуску без OverrideHeap дослівно зберегти стандартний libnx fallback через `__nx_heap_size` і `svcSetHeapSize`; при помилці query або відсутності придатного сегмента завершуватися контрольовано з `LibnxError_HeapAllocFailed`.
   - Перенести outbound NXLink logger thread із `userAppInit/userAppExit` у межі `App::App/App::~App`, виправити socket sentinel `-1` та обмежити довжину копіювання фактично записаними байтами `buf[512]`.
3. **Реалізація й верифікація**:
   - Додано strong `__libnx_initheap()` у `sphaira/source/main.cpp`: без алокацій він обходить OverrideHeap через `svcQueryMemory` і передає newlib найбільший суцільний `MemType_Heap + Perm_Rw + attr == 0` сегмент; fallback без OverrideHeap відповідає libnx.
   - Lifecycle outbound logger перенесено до `App`, socket sentinel виправлено на `-1`, а довжину повідомлення обмежено фактичним вмістом `buf[512]`; inbound `nxlinkInitialize()` не змінювався.
   - Gemini виконав `tests/run.sh` (усі host-тести) та WSL ReleaseWithInstall build; Build ID нового ELF — `93E0BD21BD490A235A75C52D4DE6ECBC243D0879`.
   - Апаратне тестування прийнято.

## Попередній delivery: v0.13.499 — Tools Menu Layout Reorganization, Software Description Update & 4th Row Expansion

Статус: реалізацію виконано та перевірено:
1. **Реорганізація сітки іконок меню Tools (`sphaira/source/ui/menus/tools_menu.cpp`)**:
   - Оновлено розташування пунктів меню:
     - 1-й ряд: File Browser, Games, Themes.
     - 2-й ряд: Updater, Saves, Software (Додаткові програми).
     - 3-й ряд: Cheats, Kefir Settings, Settings.
     - 4-й ряд (експериментальний): Tools (Інструменти).
2. **Оновлення опису розділу Software**:
   - Задано опис `"Homebrew App Store, DBI and mod utilities."` для точного відображення вмісту каталогу Homebrew App Store.
3. **Експериментальний 4-й ряд (пункт Tools)**:
   - Додано елемент `Tools` з іконкою `advanced-options.png` та дією переходу в менеджер системних модулів `UninstallerMenu`.
   - Забезпечено підтримку вертикального скролінгу сітки та безшовне відображення.
4. **Локалізація та версія**:
   - Синхронізовано нові ключі локалізації у всіх 14 мовних файлах `assets/romfs/i18n/*.json`.
   - Піднято версію до `0.13.499` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.498 — Header Subtitle Top-Row Alignment & Section Title Sizing Fix

Статус: реалізацію виконано та перевірено:
1. **Перенесення довгих описів елементів на верхній рядок хедера (`sphaira/source/ui/menus/tools_menu.cpp`, `save_hub_menu.cpp`, `settings_menu.cpp`, `uninstaller_menu.cpp`, `ftp_menu.cpp`, `appstore.cpp`)**:
   - Замість `SetSubHeading(description)`, що виводило текст у нижньому рядку поруч із заголовком розділу, реалізовано виклик `SetTitleSubHeading(description, true); SetSubHeading("");`.
   - Тепер опис обраного інструмента або налаштування відображається у верхньому рядку хедера праворуч від версії програми (`v0.13.498`), плавно прокручуючись за потреби.
2. **Збереження повного кегля назви розділу (`MenuBase::DrawChrome`)**:
   - Звільнення нижнього рядка від довгих описів гарантує повний простір для назви розділу («Інструменти» / «Tools», «Налаштування» / «Settings» тощо). Заголовок виводиться повним кеглем 28px без стискання до 40% та непотрібної прокрутки.
3. **Збірка, тести та версія**:
   - Піднято версію до `0.13.498` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно скомпільовано та пройдено всі unit-тести.

## Попередній delivery: v0.13.497 — Clean Switch compilation, translation pipeline sync & NX-Link deployment

Статус: реалізацію виконано та перевірено:
1. **Виправлення сумісності компіляції під Nintendo Switch (`sphaira/source/auto_update.cpp`, `sphaira/source/ui/about_box.cpp`, `sphaira/source/ui/menus/install_stream_menu_base.cpp`, `sphaira/source/ftpsrv_helper.cpp`)**:
   - Виправлено виклик `fs.OpenFile(staging_path, FsOpenMode_Read, &file)` з перевіркою розміру через `file.GetSize(&file_size)`.
   - Оновлено `AboutBox::Update` на використання коректних полів `TouchInfo` (`touch->is_touching`, `touch->cur.y`).
   - Виправлено форматні специфікатори `%ld` для `s64` та прибрано зайві попередження компілятора.
2. **Повний переклад через Gemini 3.6 Flash (`tools/i18n-translate/translate.py`)**:
   - Синхронізовано та повністю перекладено всі мовні файли (`uk`, `ru`, `en`, `de`, `fr`, `es`, `it`, `ja`, `ko`, `nl`, `pt`, `se`, `vi`, `zh`) без помилок (0 missing keys).
3. **Успішна збірка та розгортання через NX-Link**:
   - Скомпільовано цільовий двійковий файл `kefir-hub.nro` у WSL середовищі devkitA64 (`ReleaseWithInstall`).
   - Успішно передано бінарник на консоль через `make nxlink` (`192.168.50.69`).

## Попередній delivery: v0.13.496 — Automatic Silent Update, Background Self-Updating & About Changelog Box

Статус: реалізацію виконано та перевірено:
1. **Налаштування автоматичного оновлення (`sphaira/include/app.hpp`, `sphaira/source/app_settings.cpp`, `sphaira/source/ui/menus/settings_menu.cpp`)**:
   - Додано конфігураційну опцію `m_auto_update` (`App::GetAutoUpdateEnable()`, `App::SetAutoUpdateEnable(bool)`).
   - Відображено пункт `Auto-update` у меню `Settings → General` із детальним описом та перемикачем On/Off (увімкнено за замовчуванням).
   - Оновлено словники перекладу у всіх 14 мовних файлах `assets/romfs/i18n/*.json`.
2. **Фонове тихе оновлення виконуваного файлу (`sphaira/include/auto_update.hpp`, `sphaira/source/auto_update.cpp`, `sphaira/source/ui/menus/main_menu.cpp`)**:
   - Оновлено URL репозиторію на `https://api.github.com/repos/rashevskyv/kefir-hub/releases/latest` та `kefir-hub.json`.
   - Реалізовано асинхронне фонове завантаження релізного ассету (`kefir-hub.nro` / `sphaira.nro`) у тимчасовий файл `/switch/sphaira/cache/sphaira_update.temp` без блокування інтерфейсу чи підгальмовувань.
   - Реалізовано атомарне копіювання завантаженого бінарника у шлях поточного запущеного файлу (`App::GetExePath()`), а також синхронізацію з `/hbmenu.nro`, якщо увімкнено відповідний параметр.
   - Процес оновлення відбувається на 100% тихо та прозоро для користувача — без спливаючих діалогових вікон, без вимоги перезапуску чи підтверджень; оновлена версія безшовно запускається при наступному відкритті програми.
3. **Діалогове вікно About та перегляд списку змін (`sphaira/include/ui/about_box.hpp`, `sphaira/source/ui/about_box.cpp`, `sphaira/source/ui/menus/settings_menu.cpp`)**:
   - Створено модальний віджет `AboutBox` для перегляду поточної версії, посилання на репозиторій та списку змін (changelog) останніх релізів.
   - Реалізовано Markdown-форматування тексту, плавне прокручування (стіки, D-Pad, L/R для перегортання сторінок, жест тач-скролінгу) та кнопку оновлення (X: Refresh).
   - Додано пункт `About` у меню `Settings → General`.
4. **Unit-тести та документація**:
   - Створено `tests/test_auto_update_asset.cpp` з 8 перевірками точності вибору ассетів релізу для різних середовищ та назв файлів.
   - Піднято версію до `0.13.496` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Пройдено всі 15 наборів host unit-тестів та перевірку відсутності мертвих символів (`tests/run.sh`).

## Попередній delivery: v0.13.495 — Pixel-balanced split & full-width justified 2-row footer layout

Статус: реалізацію виконано та перевірено:
1. **Попіксельне балансування рядків футера (`sphaira/source/ui/widget.cpp`)**:
   - Алгоритм вибору індексу поділу $k$ оптимізовано для мінімізації різниці реальної піксельної ширини зайнятого контенту між нижнім та верхнім рядками ($|W_{\text{bottom}} - W_{\text{top}}| \to \min$), що забезпечує однакове візуальне навантаження обох смуг незалежно від кількості елементів.
2. **Рівномірний розподіл елементів на всю ширину футера (`sphaira/source/ui/widget.cpp`)**:
   - Реалізовано `LayoutUiButtonsRowJustified`, який розраховує міжкнопковий інтервал $gap = (W_{\text{avail}} - W_{\text{content}}) / (M - 1)$, розтягуючи елементи від `30px` до `1220px`.
   - Сенсорні зони елементів кожного рядка адаптовано так, щоб вони безшовно покривали всю нижню половину екрана без сліпих зон.
3. **Unit-тести, документація та збірка**:
   - Оновлено `tests/test_title_scaling.cpp` (перевірка мінімізації різниці пікселів та точного позиціонування лівого краю).
   - Піднято версію до `0.13.495` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL та пройдено тести.

## Попередній delivery: v0.13.494 — Unified Prev/Next Image button hint in image viewer footer

Статус: реалізацію виконано та перевірено:
1. **Об'єднання підказок навігації по зображеннях через слеш (`sphaira/source/ui/menus/file_viewer.cpp`)**:
   - Дві окремі підказки «Попереднє зображення» та «Наступне зображення» об'єднано в одну єдину дію з гліфами `\uE0ED / \uE0EE` (`◀ / ▶`) та локалізованим текстом `"Prev / Next Image"_i18n` на кнопці `Button::LEFT`.
   - Для кнопки `Button::RIGHT` зареєстровано приховану дію (`m_hint = ""`), що забезпечує збереження повної функціональності перемикання зображень кнопкою D-Pad Right на контролері та звільняє простір у підвалі вівера.
2. **Версія, збірка та перевірка**:
   - Піднято версію до `0.13.494` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно скомпільовано бінарник `sphaira_nro` у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.493 — Image viewer uncluttered header, dynamic title scaling/scrolling & 2-row footer layout

Статус: реалізацію виконано та перевірено:
1. **Приховування смуг пам'яті NAND/SD у вівері зображень (`sphaira/include/ui/menus/menu_base.hpp`, `sphaira/source/ui/menus/file_viewer.cpp`)**:
   - Додано `SetShowStorage(bool)` / `ShowStorage()` у `MenuBase`.
   - У вівері зображень встановлено `SetShowStorage(false)`, що повністю приховує смуги NAND та SD, звільняючи верхню смугу під назву зображення від лівого поля `x = 80` аж до годинника/статус-блоку (`m_status_left_x = start_x`).
2. **Адаптивне масштабування та плавна прокрутка назв файлів і заголовків (`sphaira/source/ui/menus/menu_base.cpp`)**:
   - У `MenuBase::DrawChrome` реалізовано динамічний розрахунок кегля шрифту: якщо назва не поміщається у відведений простір, розмір пропорційно зменшується (базовий 28px, зменшення до 40% / мінімум 16.8px).
   - Якщо навіть при мінімальному кеглі назва файлу перевищує доступну ширину, автоматично активується плавний скролінг `m_scroll_title` (`ScrollingText`).
3. **Автоматичний перенос підказок кнопок футера на 2 рядки (`sphaira/source/ui/widget.cpp`)**:
   - У `Widget::SetupUiButtons` додано інтелектуальне розбиття дій на два рядки, коли для одного рядка масштаб стає меншим за 0.85.
   - Алгоритм вибирає оптимальний поділ $k$, який максимізує масштаб і балансує ширину обох рядків (основні кнопки дій у нижньому рядку, тригери та вторинні дії у верхньому).
   - Забезпечено збереження читабельних шрифтів (17px/22px) та незалежні неперетинні сенсорні зони для кожного рядка ([646, 682] та [682, 720]).
4. **Unit-тести, документація та компіляція**:
   - Створено `tests/test_title_scaling.cpp` з 17 перевірками точності зменшення кегля та розбиття рядків футера.
   - Піднято версію до `0.13.493` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно скомпільовано ціль `sphaira_nro` у WSL та пройдено повний набір хостових тестів.

## Попередній delivery: v0.13.492 — Homebrew App Store restored to Tools > Software

Статус: реалізацію виконано та перевірено:
1. **Повернення магазину додатків у розділ програм (`sphaira/source/ui/menus/settings_menu.cpp`)**:
   - `Homebrew App Store` повернено на першу позицію у `BuildSoftwareItems()` (`Tools → Software`), забезпечуючи швидкий і логічний доступ до каталогу застосунків.
   - З категорії `Settings → Homebrew` (`BuildCategories()`) вилучено пункт запуску `Homebrew App Store`, зберігши розділ налаштувань суто для параметрів конфігурації (`Homebrew Search Paths`, `Forwarders`, `Replace hbmenu on exit`).
2. **Версія, збірка та перевірка**:
   - Піднято версію до `0.13.492` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено повний набір host unit-тестів.

## Попередній delivery: v0.13.491 — Fix flush thread stack overflow

Статус: реалізацію виконано та перевірено:
1. **Усунення переповнення стеку у фоновому потоці логування (`sphaira/source/log.cpp`)**:
   - Масив `batch` (64 КБ) перенесено зі стеку функції `flush_thread_func` у статичну пам'ять `g_flush_batch`.
   - Збільшено розмір стеку потоку `g_flush_thread` з `0x4000` (16 КБ) до `0x8000` (32 КБ), усунувши Stack Overflow (`Data Abort` при старті програми).
2. **Версія та збірка**:
   - Піднято версію до `0.13.491` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL.

## Попередній delivery: v0.13.490 — Fix cstring include in static logger

Статус: реалізацію виконано та перевірено:
1. **Виправлення компіляції `log.cpp`**:
   - Додано заголовок `<cstring>` у `sphaira/source/log.cpp` для повної підтримки `std::memcpy` у статичному неалокуючому буфері.
2. **Версія та збірка**:
   - Піднято версію до `0.13.490` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`).

## Попередній delivery: v0.13.489 — Zero-heap static logging buffer & image load ordering

Статус: реалізацію виконано та перевірено. Повністю усунено алокації кучі у фоновому потоці логування та нормалізовано порядок ініціалізації графіки:
1. **Статичний буфер логування без звернень до heap (`sphaira/source/log.cpp`)**:
   - Замінено динамічний `std::string` та операції `append`/`swap`/`free` на фіксований статичний буфер `g_buffer_data` (64 КБ). Це повністю виключає звернення до `_malloc_r`, `_realloc_r` та `free` під час запису логів з фонових потоків і скидання на диск/сокет, унеможливлюючи пошкодження метаданих чанків кучі (`Data Abort 0x4A8`).
2. **Порядок завантаження ресурсів (`sphaira/source/app.cpp`)**:
   - `InitDefaultImage()` перенесено перед запуском фонових воркерів `ntp::Start()` та `forwarder_auto::StartCheck()`, що гарантує ексклюзивне розкодування системних іконок без конкуренції за пам'ять.
3. **Версія та інтеграція**:
   - Піднято версію до `0.13.489` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.488 — Sysmodule slow SD boot timeout & crash prevention

Статус: реалізацію виконано та перевірено. На основі аналізу патчу SwitchThemeInjector усунено падіння та зависання на повільних microSD картах:
1. **Збільшення тайм-аутів ініціалізації ФС у сисмодулі (`sysmodule/source/main.c`)**:
   - Збільшено ліміт спроб підключення `fsInitialize()` та монтування `fsdevMountSdmc()` зі 100 ітерацій (10 секунд) до 3000 ітерацій (300 секунд / 5 хвилин), що гарантує успішний старт сисмодуля на повільних картах пам'яті.
   - Видалено фатальний аборт `diagAbortWithResult` при помилці `smInitialize()`, що запобігає крашу Atmosphere при затримках сервісів під час завантаження ОС.
2. **Версія та перевірка**:
   - Ітеровано версію програми до `0.13.488` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.487 — SD card FS sync, malloc & NanoVG stability on slow cards / NX-Link handoff

Статус: реалізацію виконано та перевірено. Усунено аварійні падіння (Data Abort 0x4A8 в `_malloc_r`) та збої файлової системи на повільних картах пам'яті під час передачі NRO через NX-Link та запуску:
1. **Фіксація файлової системи та захист від пошкодження microSD (`main.cpp`, `nxlink.cpp`, `nro.cpp`, `log.cpp`)**:
   - Виправлено ім'я монтування пристрою в `fsdevCommitDevice("sdmc")` та `fsdevGetDeviceFileSystem("sdmc")` у `userAppExit()`. Раніше передавався некоректний суфікс `"sdmc:"`, через що системне збереження кешу ФС не викликалося під час виходу з програми.
   - Додано виклики `fsdevCommitDevice("sdmc")` після створення/перейменування файлів у `nxlink.cpp`, перед викликом `envSetNextLoad` у `nro.cpp` (`launch_internal`), а також при записі логів у `log.cpp` (`do_flush`, `log_write_error`).
2. **Усунення race conditions та захист хіпу при логуванні (`log.cpp`)**:
   - Переведено мережеву передачу логів у фоновому потоці з `stdout`/stdio на прямий `send(sock, ...)`. Це усуває конфлікти алокацій у stdio нових потоків newlib, які призводили до пошкодження заголовків чанків heap (`_malloc_r`).
   - Замінено `std::localtime` на реентрабельний `localtime_r` у `log_write_error`.
3. **Безпечне завантаження ресурсів тем та текстур (`app_theme.cpp`)**:
   - У `LoadElementImage` та `LoadElementColour` додано явне створення нуль-термінованих рядків `std::string` перед викликом `nvgCreateImage` та `std::strtoul`, що усуває вихід за межі буфера `std::string_view`.
4. **Версія, збірка та розгортання**:
   - Оновлено версію до `0.13.487` у `CMakeLists.txt`, успішно зібрано `sphaira_nro` у WSL, виконано тести (`tests/run.sh`), бінарник розгорнуто на microSD диск `I:\` (`I:\hbmenu.nro` та `I:\switch\kefir-hub.nro`).

## Попередній delivery: v0.13.486 — Saves menu L/R shoulder button tab navigation

Статус: реалізацію виконано та перевірено. Додано можливість швидкого та безшовного перемикання між категоріями збережень («Встановлені ігри», «Видалені ігри», «Резервні копії») плечовими кнопками L та R:
1. **Реєстрація дій плечових кнопок (`save_menu.cpp`)**:
   - У конструкторі `Menu::Menu` додано дії `Button::L` ("Previous tab"_i18n) та `Button::R` ("Next tab"_i18n) для автономного режиму меню (`!m_app_id_filter`).
   - Кнопки відображаються у футері та підтримують як натискання фізичних кнопок контролера, так і сенсорні натискання по підказках у футері.
2. **Циклічне перемикання категорій (`save_menu.hpp`, `save_menu.cpp`)**:
   - Реалізовано метод `Menu::ChangeCategory(s64 delta)`, який циклічно перемикає категорію за списком `Installed` <-> `Deleted` <-> `Backups`.
   - Реалізовано метод `Menu::SetCategory(Category category)`: змінює `m_category`, оновлює заголовок `SetTitle(...)`, відтворює звуковий ефект зміни фокусу `App::PlaySoundEffect(SoundEffect_Focus)` та перезавантажує список елементів через `ScanHomebrew()`.
3. **Версія, тести та збірка**:
   - Піднято версію до `0.13.486` у `sphaira/CMakeLists.txt`, оновлено `README.md`, успішно скомпільовано ціль `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено перевірки host unit tests (`tests/run.sh`) та `git diff --check`.

## Попередній delivery: v0.13.485 — Screensaver display sleep prevention & OLED user brightness retention

Статус: реалізацію виконано та перевірено. Забезпечено надійну роботу скрінсейвера без вимкнення екрана та оптимізовано яскравість для різних типів матриць:
1. **Запобігання вимкненню екрана та авто-сну (`app.hpp`, `screensaver.cpp`)**:
   - Оновлено `App::SetAutoSleepDisabled(bool enable)`: тепер виклик `appletSetMediaPlaybackState(true)` здійснюється обов'язково разом із `appletSetAutoSleepDisabled(true)`. Згідно зі специфікацією HOS, саме `SetMediaPlaybackState` блокує системне приглушення яскравості (dimming) та вимкнення підсвітки екрана через неактивність.
   - Додано виклики `App::SetAutoSleepDisabled(true)` у `Screensaver::Start()` та скидання у `Screensaver::Stop()`.
   - У `Screensaver::Update(...)` додано виклик `appletReportUserIsActive()`, який періодично передає ОС сигнал про активність користувача на рівні HID, унеможливлюючи спрацьовування таймерів очікування HOS.
2. **Збереження яскравості користувача на OLED та зниження на LCD (`app_settings.cpp`, `screensaver.cpp`)**:
   - Реалізовано апаратне визначення типу консолі `App::IsOledModel()` через опитування `splGetConfig(SplConfigItem_HardwareType, &hardware_type)` (значення `5` відповідає моделі Aula / Switch OLED).
   - У `Screensaver::Start()` для OLED-моделей встановлено збереження поточної виставленої користувачем яскравості `m_saved_brightness` (чистий чорний фон скрінсейвера `#000000` вимикає пікселі OLED і споживає 0W, тому годинник і статистика залишаються яскравими та легко читабельними з відстані).
   - Для LCD-моделей (Switch V1, V2, Lite) яскравість знижується до значення `App::GetBlankBrightness() / 100.f`, що заощаджує батарею та усуває засвітку підсвітки в темряві.
   - Збережено можливість ручного регулювання яскравості правим стіком (Up/Down) на будь-якому типі дисплея та обов'язкове відновлення початкової яскравості користувача при виході зі скрінсейвера.
3. **Попередній перегляд скрінсейвера (`screensaver.cpp`)**:
   - У `SaverPreview::Update` додано виклик `m_saver.Update` для коректного оновлення дрейфу, реакції на стіки та запобігання засинанню консолі в режимі прев'ю.
4. **Версія, документація та збірка**:
   - Піднято версію до `0.13.485` у `sphaira/CMakeLists.txt`, синхронізовано `README.md`, успішно зібрано бінарник `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено перевірки host unit tests (`tests/run.sh`) та `git diff --check`.

## Попередній delivery: v0.13.484 — NX-Link SD commit, path normalization, buffer bounds & forwarder auto-install stabilization

Статус: реалізацію виконано та перевірено. На основі аналізу дампів аварійних збоїв (Atmosphere Crash Reports) усунено причини падіння пам'яті та крашу процесу при роботі NX-Link та старті застосунку:
1. **Безпечна ініціалізація `m_app_path` (`app.cpp`)**: Виправлено нетермінований рядок шляху виконуваного файлу при старті (`argv0` з `sdmc:/`). Раніше `std::strncpy` копіював байти без завершального `\0`, що призводило до читання пам'яті за межами буфера, битих записів у `playlog.ini` та некоректного обчислення SHA256-хешу. Тепер буфер явно обнуляється та гарантовано термінується `\0`.
2. **Захист від повторного встановлення активного тайтла (`forwarder_auto_install.cpp`)**:
   - Додано перевірку режиму виконання: якщо Sphaira вже працює як встановлений Application (forwarder), потік автоматично завершує перевірку без сканування та встановлення.
   - Реалізовано вилучення Title ID із назви знайденого NSP (`Homebrew menu [03DB12780BD84000]...`) та перевірку через `nsIsAnyApplicationEntityInstalled`. Якщо знайдений тайтл вже встановлено на консолі, фонове встановлення пропускається, що усуває конкурентний перезапис активного тайтла та конфлікти алокацій пам'яті/NCM під час завантаження тем і Deko3D.
   - Додано функцію `StopCheck()` та обробку запиту на зупинку `g_stop_requested` у `SilentInstallProgress`, що забезпечує коректну зупинку фонового потоку в деструкторі `App::~App()`.
3. **Нормалізація шляхів та фіксація файлової системи в `nxlink.cpp`**:
   - Шляхи, передані з хоста через NX-Link, нормалізуються (видаляється префікс `sdmc:`, гарантується початковий `/`), що усуває збої нативних викликів `FsFileSystem` (`0x202` / `0x402`).
   - Додано обов'язкові виклики `fs.Commit()` після запису даних у тимчасовий файл та після фінального `fs.RenameFile`, що гарантує збереження таблиці кластерів FAT32/exFAT на карті пам'яті перед запуском NRO та запобігає падінню файлової системи microSD.
   - Забезпечено безпечну роботу з буфером аргументів `args_buf` із гарантованим нуль-термінатором та `strncpy` для колбеків повідомлень.
   - `SocketWrapper` переведено на move-only семантику з коректним закриттям сокетів без подвійного `close`.
4. **Потокобезпечне логування (`log.cpp`)**: Замінено небезпечний `std::localtime` на реентерабельний `localtime_r` у `log_write_arg_internal`.
5. **Паралельний запуск тестів (`tests/run.sh`)**: Скрипт тестів хоста переведено на паралельну компіляцію та запуск усіх тестових наборів.
6. **Версія та збірка**: Піднято версію до `0.13.484` у `sphaira/CMakeLists.txt`, успішно скомпільовано цільовий бінарник у WSL (`[100%] Built target sphaira_nro`), пройдено всі unit-тести.

## Попередній delivery: v0.13.483 — Install queue list layout bounds fix & auto-advance on X button

Статус: реалізацію виконано та перевірено. Виправлено накладання списку пакунків у черзі встановлення на футер та додано автоматичний крок курсора при виборі пунктів кнопкою X:
1. **Геометрія списку черги інсталяції (`dbi_menu.cpp`)**: Виправлено перекриття списком елементів лінії та кнопок футера (`FOOTER_LINE_Y = 646.f`). Зменшено висоту рядка з 82.f до 78.f, скориговано позицію списку `queue_pos` на `{70.f, GetY() + 63.f, 1140.f, 470.f}` (150.f по осі Y). Тепер 6 рядків списку займають висоту 468.f (край на рівні 618.f, рамка фокусу 622.f), що залишає 24px безпечного відступу до розділювача футера.
2. **Геометрія списків журналу та помилок**: Скориговано висоту `log_pos` з 330.f до 310.f (`m_log_list` на 10 рядків по 30.f = 300.f, `m_error_list` на 5 рядків по 55.f = 275.f), усунувши накладання на футер у режимах `Installing`, `Summary` та `Cancelled`.
3. **Автоматичний перехід курсора при виборі кнопкою X**: Оновлено дію `Button::X` ("Select") для стану `State::ReviewQueue`. При натисканні кнопки X перемикається стан виділення `m_queue[m_index].selected`, а потім, якщо це не останній елемент черги (`m_index + 1 < m_queue.size()`), курсор автоматично переходить на наступний рядок (`m_index++`) із забезпеченням видимості через `m_list->EnsureVisible`. Це повністю відповідає логіці вибору в інших меню програми (`game_menu`, `homebrew`, `filebrowser`, `save_menu`).
4. **Захист ножиць кадрування контенту (`layout.hpp`)**: У `PaddedContentClipY` встановлено обов'язкове обмеження `bottom = std::min(bottom, CONTENT_BOTTOM)` для будь-яких блоків контенту, які починаються нижче лінії заголовка (`y >= HEADER_LINE_Y`), що запобігає малюванню контенту поверх футера.
5. **Версія, тести та збірка**: Піднято версію до `0.13.483` у `CMakeLists.txt`, оновлено документацію, успішно виконано прогін усіх тестів та збірку `sphaira_nro` у WSL.

## Попередній delivery: v0.13.482 — Fully silent background forwarder installation without restart prompt

Статус: реалізацію виконано та перевірено. Переведено автоматичне встановлення форвардера при старті програми у повністю тихий режим без запитів на перезапуск:
1. При старті програми фоновий потік перевіряє наявність встановленого форвардера для Homebrew Menu / Sphaira через `nsIsAnyApplicationEntityInstalled`.
2. Якщо форвардер відсутній, у фоні виконується пошук `Homebrew menu*.nsp` у папці `/Games/` та тихе встановлення через `yati::InstallFromFile`.
3. Повністю видалено діалогові вікна `OptionBox` та пропозиції перезапуску: після завершення встановлення потік тихо фіксує успіх у логах і завершує роботу, не перериваючи та не турбуючи користувача.
4. Очищено невикористовувані заголовні файли в `forwarder_auto_install.cpp`.
5. Піднято версію програми до `0.13.482` у `CMakeLists.txt`, оновлено документацію, успішно виконано збірку в WSL та пройдено всі тести.

## Попередній delivery: v0.13.481 — Install queue package skip fix & USB link resynchronization

Статус: реалізацію виконано та перевірено. Виправлено проблему, коли дія пропуску пакунка кнопкою B у черзі встановлення переривала всю чергу:
1. Усунено розсинхронізацію USB-протоколу: при пропуску пакунка користувачем (`user_skipped`) у `ThreadFunction` активний USB endpoint скасовується, через що хост-застосунок на ПК залишався в середині передачі попереднього файлу. Додано автоматичний виклик `ReestablishUsbLink()`, який повторно проводить handshake та переводить хост у режим очікування нової команди перед переходом до наступного пакунка в черзі.
2. Розширено умови повторних спроб (`attempt`) у `ThreadFunction`: тепер при виникненні помилок протоколу/сесії DBI (`IsDbiSessionError`) відбувається спроба повторного підключення замість миттєвого завершення черги.
3. Оновлено діалог `OptionBox` для кнопки `B` ("Skip package") під час встановлення (`State::Installing`): встановлено дефолтний індекс `1` ("Yes"), що дозволяє користувачеві підтвердити пропуск пакунка кнопками `A` або `+`, або скасувати діалог кнопкою `B`.
4. Синхронізовано `LocalThreadFunction`: включено перевірку `Result_UsbCancelled` та уніфіковано встановлення прапорця `m_cancel_requested` при загальному скасуванні черги.
5. Розширено `test_queue_outcome.cpp` тестом багатопакетної черги з пропуском одного пакунка та успішним встановленням наступного, піднято версію до `0.13.481` у `CMakeLists.txt`, успішно скомпільовано реліз у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.480 — Save data deletion mechanism & auto-creation on restore

Статус: реалізацію виконано та перевірено. Додано механізм видалення збережень для встановлених та видалених ігор (а також резервних копій) у меню Tools > Saves, і покращено відновлення сейвів:
1. Додано пункт `"Delete"` до спливаючого списку дій `Save Action` при виборі збереження кнопкою `A` або групи збережень кнопкою `X`, а також у бічне меню `Save Options` (кнопка `+`).
2. Реалізовано бічне меню `Delete Options` з підтримкою фільтрації за обліковими записами (`Accounts`) та типами сейвів (`Save Types`).
3. Додано захисні діалоги підтвердження `OptionBox` із попередженням про незворотність видалення та відображенням іконки гри.
4. Реалізовано метод `Menu::DeleteSaves`:
   - Для встановлених та видалених ігор: видалення сейвів із консолі за допомогою `fsDeleteSaveDataFileSystemBySaveDataSpaceId` та резервного `fsDeleteSaveDataFileSystemBySaveDataAttribute`. Для категорії "Deleted Games" гра повністю зникає зі списку після видалення сейву.
   - Для категорії "Backups": видалення файлів резервних копій (`.zip`, `.disa`) з SD-карти / накопичувача та очищення порожніх каталогів.
5. Покращено функцію `RestoreSaveInternal`: усунуто падіння при відновленні на чистих/відновлених EmuNAND або нових іграх без попереднього сейву. За відсутності файлової системи збереження на консолі вона автоматично створюється через `fsCreateSaveDataFileSystem` на основі метаданих архіву перед розпакуванням файлів.
6. Оновлено файли локалізації (`en.json`, `uk.json`), синхронізовано документацію (`README.md`), піднято версію до `0.13.480`, успішно скомпільовано цільовий `sphaira_nro` у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.479 — Automatic forwarder check, silent install & title mode restart prompt

Статус: реалізацію виконано та перевірено. Додано автоматичну фонову перевірку наявності встановленого форвардера при старті програми, тихе встановлення з microSD та пропозицію перезапуску в Title Mode:
1. При запуску Sphaira у фоновому потоці (`forwarder_auto::StartCheck()`) перевіряється, чи встановлено форвардер для Homebrew Menu або Sphaira (перевірка стандартних тайтлів `010000000000100D`, `050000000000100D`, а також згенерованих ідентифікаторів Sphaira на базі виконуваного NRO через `nsIsAnyApplicationEntityInstalled`).
2. Якщо форвардер відсутній, виконується прозорий фоновий пошук NSP-пакета в директорії `/Games/` на microSD карті за маскою `Homebrew menu*.nsp`.
3. Знайдений NSP встановлюється у фоні за допомогою `yati::InstallFromFile` із застосуванням спеціалізованого `SilentInstallProgress` без блокування інтерфейсу та з прапорцем `skip_if_already_installed = 1`.
4. Розширено інтерфейс `ui::InstallProgress` та механізм `yati.cpp` методом `OnTitleInstalled(u64 title_id)` для точного визначення встановленого ідентифікатора тайтла.
5. Після успішного встановлення через `evman::push` на головний UI-потік виводиться діалогове вікно `OptionBox` із запитом `"Homebrew Menu forwarder installed. Restart into Title Mode now?"`. При підтвердженні ("Restart") викликається `appletRequestLaunchApplication(target_tid, nullptr)` та `App::Exit()`.
6. Додано допоміжну функцію `path::StartsWithIC` у `path_util.hpp`, розширено host unit tests (`test_path_util.cpp`, 162 checks), синхронізовано 14 файлів локалізації.
7. Піднято версію до `0.13.479`, успішно виконано збірку в WSL (`sphaira_nro`), пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.478 — Theme packages download & instant install prompt

Статус: реалізацію виконано та перевірено. Додано автоматичну пропозицію встановлення для готових пакетів тем (Mario BG Dark, Switch 2 Theme by alexwak) у меню Tools -> Themes:
1. Раніше готові пакети тем завантажувалися через `MakePackageAction`, який лише розпаковував zip у `/themes/` та показував сповіщення "Done", не пропонуючи запуск `NXThemesInstaller` (на відміну від завантаження з Themezer та закріплених тем).
2. Реалізовано спільні функції `PromptInstallTheme` та `InstallThemePackage` у `themezer.hpp` / `themezer.cpp`: під час розпакування zip-архіву автоматично відстежуються шляхи до всіх видобутих `.nxtheme` файлів, після чого показується запит `"Theme downloaded, install now?"`.
3. При підтвердженні встановлення запускається `NXThemesInstaller.nro` з передачею аргументів розпакованих тем (`sdmc:/themes/...`). Якщо інсталятор відсутній на консолі, пропонується його швидке завантаження з GitHub.
4. Додано функцію `MakeThemePackageItem` у `settings_menu.cpp` з попереднім запитом `"Download theme?"`, уніфікуючи поведінку для всіх типів тем.
5. Піднято версію до `0.13.478`, успішно виконано збірку в WSL, пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.477 — Game details stat label vertical alignment fix

Статус: реалізацію виконано та перевірено. Виправлено вертикальне зміщення та накладання прокручуваних лейблів статистики гри:
1. При перевищенні довжини локалізованого лейблу (наприклад, "Останній запуск" / "Last played") 1/3 ширини блоку, текст переходить у режим автопрокручування `m_stat_label_scrolls[...].Draw`. Раніше передавалося вирівнювання `NVG_ALIGN_LEFT` без визначення вертикальної площини, через що NanoVG вирівнював текст за базовою лінією (`NVG_ALIGN_BASELINE`) замість верхнього краю (`NVG_ALIGN_TOP`), зміщуючи весь рядок угору (~15px) відносно свого значення.
2. Додано прапорець `NVG_ALIGN_LEFT | NVG_ALIGN_TOP` до `m_stat_label_scrolls` та уніфіковано вирівнювання для `m_language_scroll.Draw` (`NVG_ALIGN_LEFT | NVG_ALIGN_TOP`, `y + 1.f`).
3. Піднято версію до `0.13.477`, успішно виконано збірку в WSL, пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.476 — UTF-16 to UTF-8 decoding & Cyrillic filename fix for MTP

Статус: реалізацію виконано та перевірено. Виправлено критичну проблему зі створенням та перейменуванням файлів/папок з кириличними та іншими не-ASCII назвами (зокрема системними іменами "Нова папка" / "Новая папка" у Windows Explorer):
1. В оригінальній бібліотеці `libhaze` функція `ReadString` у `ptp_data_parser.hpp` некоректно виконувала `static_cast<char>(chr)` над UTF-16 символами, відкидаючи старший байт. Для кириличних символів (наприклад 'Н' = `0x041D`) це призводило до перетворення на неприпустимі керуючі символи (0x1D) замість дійсного UTF-8 (`\xD0\x9D`), через що файлова система Switch відхиляла створення папки з помилкою `FsError_InvalidCharacter`.
2. Реалізовано повноцінне декодування UTF-16 у UTF-8 у `ReadString` (`ptp_data_parser.hpp`) та кодування UTF-8 у UTF-16 у `AddString` (`ptp_data_builder.hpp`).
3. Додано секції 7 та 8 у `patch_libhaze.cmake`, піднято версію до `0.13.476`, успішно зібрано бінарник у WSL та пройдено всі тести.

## Попередній delivery: v0.13.475 — Full read-write support with commit for MTP Saves drive

Статус: реалізацію виконано та перевірено. Перетворено віртуальне MTP-сховище `Saves` з режиму read-only у повноцінний read-write режим:
1. Додано підтримку створення та запису файлів (`CreateFile`, `WriteFile`, `SetFileSize`, `OpenFile` з `FsOpenMode_Write`) з автоматичним комітом змін (`fsFsCommit`) у файлову систему збереження при завершенні запису.
2. Додано підтримку створення піддиректорій (`CreateDirectory`), видалення файлів і папок (`DeleteFile`, `DeleteDirectoryRecursively`) та перейменування (`RenameFile`, `RenameDirectory`) всередині змонтованих збережень.
3. Оновлено монтування сейвів у `FsSaveProxy`: тепер відкриття виконується у режимі читання-запису (з безпечним fallback у read-only для захищених системних сейвів).
4. Оновлено `GetFreeSpace` для відображення доступного місця та змінено відображення назви диска з `Saves (read-only)` на `Saves`.

## Попередній delivery: v0.13.474 — Full MTP property handling, GetObjectPropDesc & SendObjectPropList fixes

Статус: реалізацію виконано та перевірено. Виправлено критичні збої та відмови в MTP-обробнику `libhaze`:
1. У `SendObjectPropList` (0x9808) замінено виклик помилки `ResultUnknownPropertyCode` на безпечне вичитування та обробку всіх типів властивостей об'єктів MTP (U8, U16, U32, U64, U128, String, масиви). Раніше будь-яка стандартна властивість від Windows Explorer (наприклад `StorageID`, `ObjectFormat`, `ParentObject`, `PersistentUniqueObjectIdentifier`), що надсилалася перед ім'ям файлу, спричиняла аварійний викид помилки та відмову створення папки.
2. У `GetObjectPropDesc` виправлено пропущений `break;` у switch після властивості `PersistentUniqueObjectIdentifier`, що спричиняло падіння у наступний `case ObjectSize` та надсилання пошкодженого блоку дескриптора властивості.
3. У `GetObjectPropList` додано підтримку `property_code == 0` (запит усіх властивостей згідно з MTP специфікацією).
4. У `SetObjectPropValue` додано підтримку встановлення імені об'єкта через властивість `PtpObjectPropertyCode_Name`.
5. Усі патчі додано в `patch_libhaze.cmake`, піднято версію до `0.13.474`, збірка та тести успішно пройдені.

## Попередній delivery: v0.13.473 — Installed Games save scanning & category listing fix

Статус: реалізацію виконано та перевірено. Виправлено відображення списку в `Saves -> Installed Games`: тепер меню надійно відображає всі встановлені на консолі ігри (аналогічно Tools Games через `nsListApplicationRecord` + `title::GetMetaEntries`), навіть якщо для них ще не було створено сейв на консолі або активний інший обліковий запис. Для кожної гри прив'язуються наявні активні сейви, або створюється запис гри для швидкого створення чи відновлення бекапів. Категорії "Видалені ігри" та "Резервні копії" при відсутності записів коректно показують стан "Empty...". У `PromptSaveTypeOptions` додано можливість відновлення бекапів безпосередньо для встановленої гри без наявності попереднього сейву.

## Попередній delivery: v0.13.472 — MTP folder and file creation storage_id fix

Статус: реалізацію виконано та перевірено. Виправлено критичний баг у протоколі MTP/PTP бібліотеки `libhaze`, через який Windows Explorer не міг створювати нові папки та файли ("Пристрій припинив відповідати, або його було відключено"). У відповідях `SendObjectInfo` (0x100C) та `SendObjectPropList` (0x9808) поле `storage_id` помилково заповнювалося `parentobj->GetObjectId()` (дескриптором об'єкта) замість `parentobj->GetStorageId()` (ідентифікатора сховища). Додано відповідні патчі в `patch_libhaze.cmake` та виправлено логування в `haze_helper.cpp`.

## Попередній delivery: v0.13.471 — Save categories hub & custom save backup search paths

Статус: реалізацію виконано та перевірено. Додано початкове меню категорій (`SaveHubMenu`) при вході у розділ Saves (Tools -> Saves та Main Menu -> Saves) з 3 пунктами: "Встановлені ігри" (Installed Games), "Видалені ігри" (Deleted Games), "Резервні копії" (Backups). Додано підтримку налаштування додаткових користувацьких папок для пошуку резервних копій у Settings -> Saves -> Save Backup Search Paths із вибором папок через `filepicker::Menu`, збереженням у конфігураційний файл `[save_backup_paths]` та автоматичним скануванням цих папок у `CollectBackups` та `ReadBackupEntries`.

## Попередній delivery: v0.13.470 — raw DISA save restore, save discovery & MTP USER:/save

Статус: реалізацію виконано та перевірено. Додано підтримку відновлення запакованих/сирових DISA/DPFS сейвів (монолітні контейнери `000000000000001e`, `.disa`, `.bin`, дампи з DBI Explorer) шляхом прямого блокового запису у відповідний NAND BIS-розділ (`FsBisPartitionId_User` або `FsBisPartitionId_System`) за шляхом `/save/<save_data_id>` із відображенням прогресу. Розширено `CollectBackups` та `ReadBackupEntries` для виявлення сирових сейвів у каталогах бекапів. Додано дію "Restore save data" у File Browser для швидкого відновлення збережень з будь-якого носія (SD, USB HDD, мережа). Додано MTP-сховища `USER:/save` та `SYSTEM:/save` з підтримкою читання й запису.

## Попередній delivery: v0.13.469 — unified pending UI & updater work

Статус: відновлено збережений WIP поверх повної колишньої mainline. Залишено
новішу спільну реалізацію `gfx::ImageViewport` для Theme Creator і forwarder crop,
покращений File Viewer з вибором діапазону та діями над ним, іконки дій у sidebar/
popup/File Browser, а також Updater focus, Kefir update badge і reconnect. File Picker
використовує стабільний верхній слот header з відновленої mainline.

## Попередній delivery: v0.13.468 — cURL shutdown & shared handle serialization

Статус: інтеграцію відновлено поверх `v0.13.467`. Спільний синхронний дескриптор
`g_curl_single` серіалізовано через `g_mutex_single`; `curl::RequestShutdown()`
викликається на початку виходу з App і не дає почати нові transfer після запиту
зупинки. Очищення handle виконується під тим самим mutex.

## Попередній delivery: v0.13.467 — versioned HTTP User-Agent

Статус: реалізацію виконано та перевірено. Замінено застарілий downloader User-Agent `TotalJustice` на єдине спільне джерело `APP_USER_AGENT` (`Sphaira/<APP_VERSION>`) у `defines.hpp` та додано встановлення `CURLOPT_USERAGENT` до `MountCurlDevice::curl_set_common_options()`. Політику TLS, редиректи, автентифікацію, HTTP-семантику, UI, i18n та залежності залишено без змін. Gemini успішно пройшов `git diff --check` та WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`). Очікується ручний Switch remote-mount smoke check.

1. Додати єдину константу `APP_USER_AGENT = "Sphaira/" APP_VERSION;` у `sphaira/include/defines.hpp`.
2. Видалити локальний `API_AGENT` у `sphaira/source/download.cpp` та використати `APP_USER_AGENT` у `SetCommonCurlOptions()`.
3. Додати `curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, APP_USER_AGENT)` у `MountCurlDevice::curl_set_common_options()` (`sphaira/source/utils/devoptab_curl_device.cpp`).
4. Оновити `upstream_audit.md` (пункти `2eabcec` / `3ef698b`), підняти версію до `0.13.467` у `sphaira/CMakeLists.txt`, синхронізувати living docs, пройти `git diff --check` та WSL `ReleaseWithInstall`.

## Попередній delivery: v0.13.466 — caller-selected header layout

Статус: реалізацію виконано та перевірено. `MenuBase::SetTitleSubHeading` отримав стабільний параметр `top_row`, тому шлях або довільна назва більше не стрибає між рядками залежно від ширини. Шляхи й назви рендеряться після версії у верхньому широкому слоті, а лічильники та короткі статуси лишаються внизу.

1. Додати `bool top_row = false` до `MenuBase::SetTitleSubHeading` та скидати scroll state при зміні слота або очищенні тексту.
2. Рендерити верхній слот після виміряного `v%s` і до межі fixed status area; лишити `ScrollingText` для переповнення.
3. Передати `top_row = true` лише caller-ам шляхів і довільних назв; залишити компактні summary на нижньому рядку.
4. Підняти версію до `0.13.466`, оновити living docs, пройти host tests, WSL `ReleaseWithInstall` та `git diff --check`.

## Попередній delivery: v0.13.465 — text editor multi-line editing

Статус: реалізацію виконано та перевірено. Реалізовано вибір діапазону рядків у режимі редагування через меню дій (`Select range` / `Clear selection`), під час активного вибору A = `Finish selection`, B = `Cancel selection`. Додано процесовий буфер обміну рядками (`s_line_clipboard`) для дій Copy, Cut, Paste below та Delete над виділеним діапазоном або активним рядком. Реалізовано чисті допоміжні функції `CommentIniLine` та `UncommentIniLine` у `text_helper.hpp` із збереженням відступів та додано пункт `Undo` й `Redo` у спливаючий список дій. Пройдено повний набір host unit tests (`tests/run.sh` - 13 suites, 752 declarations) та `git diff --check`.

1. Додати підтримку вибору діапазону рядків у `fileview::Menu` (`StartRangeSelection`, `FinishRangeSelection`, `CancelRangeSelection`, `ClearRangeSelection`) та оновити підказки кнопок футера (`Finish selection` / `Cancel`).
2. Створити процесовий буфер обміну рядками (`s_line_clipboard`) для `CopySelection`, `CutSelection`, `PasteBelow` та `DeleteLine` (із збереженням щонайменше 1 порожнього рядка).
3. Реалізувати чисті функції `CommentIniLine` та `UncommentIniLine` у `text_helper.hpp` з тестами в `test_text_helper.cpp` та підключити їх до дій `Comment` / `Uncomment`.
4. Оновити спливаюче вікно дій рядка (`ShowLineActions`) для динамічного показу дій над виділеним діапазоном або поточним рядком, включно з `Undo` та `Redo`.
5. Малювати виділення напівпрозорою фокусною смугою з alpha 0.35 (`ThemeEntryID_FOCUS`), зберігаючи синтаксичні кольори тексту.
6. Підняти версію до `0.13.465` у `sphaira/CMakeLists.txt`, оновити living docs, пройти перевірку host tests та `git diff --check`.

## Попередній delivery: v0.13.460 — text editor basics

Статус: реалізацію виконано та перевірено. Реалізовано перехід Edit → View на кнопку B
зі збереженням стану та підтвердженням Save/Discard/Cancel при виході з View; блокування
повторного wrap при утриманні Down/Up на межах документа; стабільний рендеринг виділеного
рядка INI з синтаксичним підсвічуванням та перемикання значень 0 ↔ 1 у `ToggleIniBoolean`.
До цього delivery також увійшов ще не закомічений ZL zoom chord: `ZL` + D-pad
або вертикальний стік масштабує текст і не запускає page up після zoom.
Пройдено host test suite (13 suites, 742 declarations), WSL ReleaseWithInstall
(`[100%] Built target sphaira_nro`) та `git diff --check`.

1. Додати `SwitchToViewMode()`: кнопка B в Edit перемикає у View без закриття меню;
   кнопка B у View показує діалог збереження для зміненого документа.
2. В Edit mode блокувати автоповтор wrap при утриманні кнопки на першому чи останньому
   рядку; скидати блокування при відпусканні кнопки.
3. Зберегти синтаксичні кольори та однаковий розмір шрифту для виділеного рядка INI.
4. Розширити `ToggleIniBoolean()` для перемикання 0 ↔ 1 із тестами в `test_text_helper.cpp`.
5. Підняти версію до `0.13.460`, оновити living docs, пройти тести та збірку.

## Попередній delivery: v0.13.458 — Homebrew settings & search paths

Статус: реалізацію виконано та перевірено. Додано виділену категорію
`Homebrew` у Settings одразу після `General`, куди перенесено `Forwarders`,
`Homebrew App Store` та `Replace hbmenu on exit` без дублювання. Додано
менеджер `Homebrew Search Paths` для додавання лише microSD-папок, збереження
користувацьких шляхів у конфіг, їх перегляду та видалення з підтвердженням,
із негайним оновленням переліку Homebrew (системний шлях `/switch` залишається
незмінним та прихованим від конфігу). Оновлено 13 локалізацій (без `ru.json`).
Пройдено валідацію JSON, host test suite (13 suites, 742 declarations) та
`git diff --check`.

1. Додати окрему категорію `Homebrew` у Settings одразу після `General`.
2. Перенести до нової категорії `Forwarders` (з `Install`), `Homebrew App Store`
   (з `Software`) та `Replace hbmenu on exit` (з `General`).
3. Реалізувати `Homebrew Search Paths`: додавання лише SD-папок через FilePicker,
   збереження користувацьких шляхів у конфіг, показ списку та видалення через
   OptionBox із миттєвим `SignalChange()` / оновленням NRO.
4. Оновити 13 файлів локалізації в `assets/romfs/i18n/*.json` (крім `ru.json`).
   Підняти версію `0.13.457 → 0.13.458`, оновити living docs, перевірити валідність
   JSON, пройти host tests та `git diff --check`.

## Попередній delivery: v0.13.457 — text viewer viewport scrolling

Статус: реалізацію виконано та перевірено. У read-only text viewer Up/Down,
D-pad та обидва стіки зміщують вікно на один рядок одразу без затримок курсора;
streamed reader тримає буфер рядків наперед і плавно переходить між сторінками.
Збережено release-based L/R (сторінка), ZL/ZR (10 сторінок), L + right-stick zoom,
pinch zoom та неклікабельні footer hints. Пройдено host test suite (13 suites,
742 declarations), WSL ReleaseWithInstall (`[100%] Built target sphaira_nro`) та
`git diff --check`.

1. У read-only viewer відокремити cursor/editing semantics від прокрутки
   viewport: Up/Down, лівий і правий стіки зміщують вікно на один рядок одразу.
2. Для streamed reader тримати буфер щонайменше на один viewport попереду,
   обчислювати наступний page offset після одного видимого viewport і плавно
   переходити на нього на межі. Не індексувати весь файл і зберегти bounded
   cache.
3. Зберегти release-based L/R, ZL/ZR, L + right-stick zoom, one-finger swipe,
   pinch zoom і неклікабельні footer hints. Підняти `0.13.456 → 0.13.457`,
   оновити living docs і пройти host suite, WSL build та `git diff --check`.

## Попередній delivery: v0.13.456 — text viewer pager

Статус: реалізацію прийнято після Gemini junior-review. Контекстна дія працює
для кожного звичайного файла, а великі файли читаються ліниво малими
сторінками; коротке або помилкове читання завершує viewer через чинний error
box без повторного discovery того самого offset. Gemini пройшов `tests/run.sh`
(13 suites, 742 declarations), WSL `ReleaseWithInstall` (`sphaira_nro`) і
`git diff --check`. Потрібний Switch smoke-test пейджера та жестів.

1. Додати в File Browser один `View as text` для будь-якого звичайного файла.
   Залишити автоматичний View за known text extension, а інсталяцію, image,
   archive, NRO та file associations не змінювати.
2. Зберегти чинний in-memory editor лише для файлів до 4 MiB. Для більших
   відкрити read-only paged reader: тримати лише поточну і кілька наступних
   сторінок тексту, байтові offsets сторінок та невеликий chunk buffer; не
   читати або не індексувати весь файл наперед.
3. У read-only text view: Up/Down і обидва стіки рухаються рядком; `L`/`R`
   перегортають назад/уперед одну сторінку на release; `ZL`/`ZR` — десять.
   Утриманий `L` + правий стік змінює масштаб без випадкової дії від drift;
   release `L` гортає назад лише якщо L не був modifier. Підтримати pinch zoom
   через фактичний two-touch input. Ніякий paging/zoom footer hint не повинен
   спрацьовувати від touch, але scroll і pinch залишаються touch actions.
4. Змінювати масштаб у практичних межах, перебудовуючи viewport/page rows і
   зберігаючи поточну позицію документа настільки точно, наскільки дозволяє
   потоковий offset. Додати одну host-перевірку page boundaries/line stepping,
   підняти версію `0.13.455 → 0.13.456`, оновити task/plan/walkthrough та
   пройти host suite, WSL build і `git diff --check`.

## Попередній delivery: v0.13.455 — INI text viewer spacing

Статус: локальну причину накладання знайдено в `fileview::Menu::DrawText`: номер рядка виставляє NanoVG на 16 px, після чого ключ вимірювався цим самим розміром, але малювався в 18 px. Перед `gfx::textBounds` ключа відновлено 18 px, тому початок `=` і значення відповідає фактично намальованій ширині ключа.

1. Зберегти чинні кольори, INI parser, clipping і компонування gutter без нових UI-механізмів.
2. Встановити 18 px лише перед вимірюванням `key_str` у shared INI draw path.
3. Підняти версію `0.13.454 → 0.13.455`, оновити living docs і пройти `git diff --check`; target WSL `sphaira` успішний, але повний `ReleaseWithInstall` окремо блокується відсутньою Make-ціллю `sphaira/sphaira.elf` під час NRO-пакування.
4. На Switch відкрити `system_settings.ini` з ключем на кшталт `enable_send_rights_usage_status_request`; його значення має починатися після ключа без накладання.

## Попередній delivery: v0.13.454 — NSP install diagnostics

Статус: реалізацію виконано та перевірено. Діагностичні повідомлення встановлення NSP та перевірка версії HOS інтегровані в єдину спільну точку опису помилок `ui::GetResultDescription(Result)`. Gemini успішно виконав host checks (`test_version_compare` 34 checks, `tests/run.sh` all green), JSON parser валідацію всіх 14 мов і WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`).

1. У `sphaira/source/ui/error_box.cpp` додати описи для `MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer)`, `Result_StreamUnexpectedEof` та `Result_NspBadMagic`.
2. Для HOS несумісності відображати мінімальну версію 4.0.0 через `version::FormatPacked`, динамічну встановлену версію через `hats::getSystemFirmware()` та рекомендацію оновити системну прошивку.
3. Для `Result_StreamUnexpectedEof` та `Result_NspBadMagic` додати чіткі інструкції повторного копіювання/завантаження, не зачіпаючи стандартні файлові чи криптографічні помилки.
4. Додати локалізацію 3 ключів до всіх 14 JSON-файлів у `assets/romfs/i18n/` та перевірити їх валідність.
5. Розширити `tests/test_version_compare.cpp` перевіркою форматування 4.0.0 і підтвердити збірку в WSL.

## Попередній delivery: v0.13.454 — Homebrew multi-select actions

Статус: реалізацію прийнято після Gemini junior-review. Shared grid renderer
залишився єдиним джерелом візуальної семантики selection: List малює checkbox
у боковому gutter, а tile layouts — overlay. Gemini пройшов `tests/run.sh`
(усі 13 suite green, 734 header declarations), WSL `ReleaseWithInstall`
(`Built target sphaira_nro`) і `git diff --check`; лишився Switch smoke-test.

1. Повторно використати в `homebrew::Menu` чинну поведінку Games: `X` змінює
   вибір поточного NRO та переходить до наступного, `Y` інвертує вибір, `B`
   очищує вибір до виходу; не створювати окремий checkbox/layout механізм.
2. Передати `selected` до `grid::Menu::DrawEntry` і використати
   `DrawSelectionMark`, який уже малює checkbox у боковому gutter для List та
   позначку/overlay для плиткових макетів.
3. У Homebrew Options показувати число targets і масові `Star`/`Unstar` лише
   коли відповідна операція має роботу. `Delete` мусить вимагати підтвердження,
   обробляти кожен результат і після успіху перезчитувати список. Не діяти на
   синтетичному Kefir Updater stub.
4. Підняти версію `0.13.453 → 0.13.454`, оновити task/plan/walkthrough,
   додати найменшу потрібну перевірку, пройти host suite, WSL build і
   `git diff --check`; вручну перевірити X/Y, List/Grid/HB Menu та всі три
   контекстні дії на Switch.

## Попередній delivery: v0.13.453 — PFS0/NSP parser hardening

Статус: реалізацію прийнято після ручного Gemini junior-review циклу. Деталі
baseline-доказів і межі scope — у
[`pfs0_nsp_hardening_audit.md`](pfs0_nsp_hardening_audit.md). Зафіксовано exact
metadata reads, limits, checked arithmetic, bounded names і known-size bounds у
спільному PFS0 parser; невідомі streams лишаються підтриманими через штатний
`FsError_NotImplemented` size result.

1. `Nsp::GetCollections()` вимагає exact header/file-table/string-table reads,
   перевіряє всі metadata-derived allocation, offsets і `CollectionEntry` до їх
   publication, не змінюючи чинний chunk-aggregation `source::Stream::Read()`.
2. `pfs0.hpp` зберігає binary-layout asserts, caps `0xFFFF` files / 4 MiB
   string table, checked arithmetic, bounded NUL search і parsed known-size
   ends. Common source `GetSize()` передає file/NCA/buffer capacity у parser;
   лише `FsError_NotImplemented` означає unknown-size stream.
3. `tests/test_pfs0_nsp.cpp` покриває valid layout, short reads, hostile
   allocations, invalid/missing-NUL names, overflow і known-size overrun.
4. Gemini фактично виконав focused test (41 checks), `tests/run.sh` (`all green`),
   WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`) і
   `git diff --check`. Senior review охопив parser, всі GetSize adapters і
   PFS0/NCA callers.
5. Версію піднято `0.13.452 → 0.13.453`; зміна parser/test/document-only, тому
   Switch hardware/manual check не потрібний.

## Попередній delivery: v0.13.452 — відновлення loader thread affinity перед NRO

Статус: реалізацію, senior review і програмну верифікацію завершено. У `loadNro()` безпосередньо перед trampoline відновлюється фактична process core mask: `svcGetInfo(InfoType_CoreMask, CUR_PROCESS_HANDLE)` → `svcSetThreadCoreMask(CUR_THREAD_HANDLE, -1, core_mask)`. Будь-яка помилка проходить через `diagAbortWithResult`; `highest_cpu_id = 3` не перетворюється на жорстку mask. Gemini успішно виконав WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`) і `git diff --check`; версію піднято до `0.13.452`. Залишається лише апаратний smoke-test.

1. Перевірити на Switch старт NRO з Homebrew Menu та повернення/перезапуск через `envSetNextLoad()`.
2. Очікуваний результат: NRO запускається і повторно запускається без зависання, крашу або зміни UI/CPU-налаштувань.
3. Якщо запуск переривається, зафіксувати точний Horizon Result з abort screen; це симптом для наступного bounded fix.

## 0. v0.13.451 — custom NRO search paths

Статус: реалізацію та всі перевірки завершено. `/switch` лишається незмінним default root; додаткові native-SD roots зберігаються в `[homebrew_paths]`, валідні absolute paths нормалізуються та дедуплікуються, а сканування custom roots має глибину 2. Пройдено `tests/run.sh` (154 checks у `path_util`), JSON validation, WSL `ReleaseWithInstall` і `git diff --check`. Російську локаль свідомо виключено з цього delivery до окремого i18n pipeline.

1. Повторно використано `minIni`, event-based Homebrew refresh і `path_util.hpp`; не додано subsystem, dependency, network filesystem або конфігурацію для `/switch`.
2. `NormalizeSearchPath` відхиляє не-absolute, `.`/`..`, backslash, `:`, control bytes, root, `/switch` і довжину `>= FS_MAX_PATH` до будь-якого створення `fs::FsPath`; неіснуючі roots пропускаються під час scan.
3. File Browser дозволяє add/remove лише для дозволеного SD-каталогу; remove має підтвердження, а успішна зміна конфігу надсилає `homebrew::SignalChange()`.
4. `/switch` сканується чинним `nro_scan`, кожен custom root — `nro_scan_depth(..., 2)`; NRO entries дедуплікуються за canonical path, а empty Homebrew list безпечний.

## 0.1. v0.13.449 — NFS phase 1 (read-only source)

Статус: програмну реалізацію та senior review завершено; host suite (`nfs_url`: 194 checks), dead-symbol guard, WSL `ReleaseWithInstall` і `git diff --check` пройдено 2026-08-14. Апаратна перевірка на реальній Switch залишається відкритою.

1. Підключено статичний `ITotalJustice/libnfs@65f3e11` через `FetchContent`; dependency documentation, examples і tests вимкнено.
2. Додано read-only `devoptab_nfs.cpp`, що належить спільному `MountNetworkDevice2()`, використовує `nfs_parse_url_dir()`, RAII cleanup та повертає `EROFS` для мутацій.
3. Додано host-testable NFS URL validator із canonical lowercase scheme, hostname/IPv4 і port validation, збереженням nested export path, лімітом `FsPath`, відхиленням credentials, traversal, query/fragment, IPv6 та небезпечного percent encoding.
4. NFS підключено до File Browser, source picker і Settings; на кожному маршруті збережено read-only flag, а невалідні saved URLs відсіюються до копіювання у фіксовані `FsPath`.
5. Оновлено англійську та українську локалізації, додано 194 host checks і завершено software verification. Наступний крок — browse/read/copy-from-NFS smoke test на Switch.

## 0.2. v0.13.448 — очищення екранних NTP-сповіщень

Статус: реалізацію завершено; прибрано тимчасові діагностичні tooltip-и та нелокалізоване сповіщення UI refresh; збережено повне логування `[NTP]` та єдине локалізоване сповіщення "Clock synced" для фактично оновленого User Clock; пройдено WSL `ReleaseWithInstall`, `git diff --check`, оновлено living docs.

1. У `sphaira/source/ntp.cpp` вилучено `SHOW_NTP_PROGRESS_TOOLTIPS` та виклик `App::Notify` із `ReportSyncStage()`, зберігши запис усіх етапів і результатів у `[NTP]` лог.
2. Прибрано нелокалізоване сповіщення `App::Notify("NTP: UI clock refreshed", ...)` з блоку оновлення UI.
3. Збережено виклик локалізованого `App::Notify("Clock synced"_i18n)` як єдиного екранного сповіщення, що чергується в UI-потоці через `evman::push` виключно після успішного live-запису User Clock та `__libnx_init_time()`.
4. Гарантовано відсутність сповіщень на шляху, коли зміщення менше за `MIN_CORRECTION_SECONDS` (час уже точний), та на fallback-шляху `used_fallback` (коли увімкнено automatic correction і діє процесний offset).
5. Піднято `sphaira_VERSION` до `0.13.448`, оновлено `task.md`, `plan.md`, `walkthrough.md`.

## 0.3. v0.13.447 — upstream-equivalence hardening: безпечне ZIP extraction

Статус: реалізацію, валідатор і тести завершено; пройдено `tests/run.sh` (106 checks у `path_util`), WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`), `git diff --check`, враховано senior review (захист `number_entry` overflow та оновлення коментаря санітизації), піднято версію до `0.13.447` і створено сфокусований коміт.

1. Досліджено всі 11 викликів `thread::TransferUnzipAll()` та виправлено root cause у спільній функції, захистивши всі операції розпакування (Appstore, direct-link/GitHub downloads, cheats, firmware, File Browser, save restore, translations).
2. Додано inline helper `path::IsSafeArchiveEntry(std::string_view)` у `sphaira/include/path_util.hpp`, який валідує відносні шляхи й каталоги, відхиляє порожні імена, початковий `/`, backslash `\`, керуючі символи (< 0x20, DEL 0x7F), `:` (захист від device/scheme) та `.`/`..` компоненти шляху, зберігаючи валідні файли з крапками (`.config`, `..data`, `file.name`).
3. У першому проході `thread::TransferUnzipAll()` додано перевірку `info.size_filename` на відповідність буферу та `strlen`, валідацію `path::IsSafeArchiveEntry()`, перевірку сумарної довжини шляху призначення з `base_path` на ліміт `sizeof(fs::FsPath)`, а також захист від переповнення `s64` для сумарного `uncompressed_size` та `ginfo.number_entry`.
4. Збережено чинну HOS character sanitization для безпечних неструктурних символів (`*`, `?`, `"`, `<`, `>`, `|`), чинні filter callbacks і progress semantics; не додавалося SD-специфічних перевірок вільного місця у спільний helper.
5. Розширено host-тести `tests/test_path_util.cpp`, пройдено `tests/run.sh`, WSL `ReleaseWithInstall` та `git diff --check`.
6. Піднято `sphaira_VERSION` з `0.13.446` до `0.13.447`, оновлено `task.md`, `plan.md`, `walkthrough.md`, `upstream_audit.md` і створено один сфокусований коміт.

## 0.1. v0.13.446 — NTP через системну automatic correction

Статус: реалізацію та програмні перевірки завершено (WSL `ReleaseWithInstall` та `git diff --check` успішно пройдено 2026-08-13, версію піднято до v0.13.446). Апаратна перевірка на реальній Switch залишається відкритою.

1. Вилучено некоректний виклик `DisableAutomaticCorrection()`.
2. Залишено спроби запису User Clock та Network Clock через `time:su` і `time:s`. Після спроб запису виконується повторне зчитування User Clock: якщо час збігається з NTP (до 2 с), це вважається негайним live-синхроном (скидається process offset, оновлюється libnx time, виводиться "Clock synced").
3. Якщо User Clock все ще відхиляється, через `set:sys` зберігається `NetworkSystemClockContext` та вмикається `setsysSetUserSystemClockAutomaticCorrectionEnabled(true)`.
4. У tooltip/log для fallback шляху виводиться `automatic correction enabled; reboot required to update HOS User Clock`, при цьому не показується "Clock synced" і не стверджується live-зміна HOS User Clock. Process offset Sphaira зберігається для миттєвого відображення часу в додатку.
5. Пройдено WSL `ReleaseWithInstall`, `git diff --check`, піднято версію до `0.13.446`, оновлено living docs.

## 0.1. v0.13.445 — NTP User Clock через set:sys

Статус: реалізацію та програмні перевірки завершено (WSL `ReleaseWithInstall` та `git diff --check` успішно пройдено 2026-08-13, версію піднято до v0.13.445). Апаратна перевірка на реальній Switch залишається відкритою (hardware verification remains pending: миттєве оновлення годинника Sphaira, NTP trace та збереження часу після перезапуску).

1. У `SetSystemTime()` зберегти чинні спроби `time:su` і `time:s` для live User Clock; після їхньої відмови спробувати штатний `set:sys` IPC: отримати standard steady-clock time point, утворити `TimeSystemClockContext { NTP - steady, steady }`, записати User і Network context та вимкнути automatic correction у `set:sys`. Запис у `errors.txt` виконувати лише при відмові `set:sys`, щоб успішний fallback не залишав хибних записів про помилки.
2. Не вважати `set:sys` live-успіхом без перевірки: оновити часовий display Sphaira на NTP offset одразу в поточному процесі, а persisted context лишити джерелом правильного часу після перезавантаження HOS.
3. Залишити `SHOW_NTP_PROGRESS_TOOLTIPS = true` і показати відкриття `set:sys`, зчитування steady clock, запис кожного context, результат automatic-correction та підсумок.
4. Пройти WSL `ReleaseWithInstall` і `git diff --check`, підняти версію до `0.13.445`, оновити living docs і виконати ручну перевірку на Switch.

## 0.1. v0.13.444 — видимий NTP diagnostic trace

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.444. Потрібна ручна перевірка на Switch.

1. Лог v0.13.443 показав, що NTP-відповідь отримано, але обидва шляхи `time:su` та `time:s` відхилили User system clock з `0x00000274`.
2. Додати тимчасовий `ReportSyncStage`: він записує `[NTP]`-рядок і thread-safe tooltip зліва. Прапор `SHOW_NTP_PROGRESS_TOOLTIPS` залишити `true` до завершення апаратної діагностики.
3. Покрити tooltip-ами кожен етап: мережу, DNS, socket/send/receive, валідну відповідь, читання й offset User Clock, кожну операцію `time:su` і `time:s`, fallback, UI refresh і фінальний Result.
4. Пройдено WSL `ReleaseWithInstall` та `git diff --check`; версію піднято до `0.13.444`.

## 0.1. v0.13.443 — запис NTP-часу через `time:su`

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.443. Потрібна ручна перевірка на Switch.

1. Логи з HOS 20.5.0 показали, що NTP-відповідь надходить, але `time:s` відхиляє і вимкнення automatic correction, і запис User system clock з `0x00000274`.
2. `SetSystemTimeWithService` виконує чинну спробу запису для одного сервісу; `SetSystemTime` спершу викликає її для `time:su`, а потім для `time:s` лише якщо User system clock не було записано.
3. Невдале вимкнення automatic correction лишається best-effort. Успіх визначає лише запис User system clock; Network system clock лишається best-effort.
4. За повної невдачі в `errors.txt` записуються Result обох сервісів, що робить наступний апаратний тест діагностичним.
5. Пройдено WSL `ReleaseWithInstall` та `git diff --check`; версію піднято до `0.13.443`.

## 0.1. v0.13.442 — усунення крашу File Browser при завантаженні асоціацій

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.442.

1. Причина крашу: під час додавання багатьох асоціацій запусків зростання `std::vector<FileAssocEntry>` викликало реалокацію вектора й копіювання великих об'єктів `FileAssocEntry` (кожен з яких містить 0x301-байтний буфер `fs::FsPath`), що призводило до переповнення стеку / крашу в `memset`.
2. Виправлення: додати static-функцію `CountAssocEntriesPath` і перед додаванням асоціацій обчислити максимальну кількість `.ini` файлів-кандидатів у `romfs:/assoc/` та `paths::ASSOC`, після чого підготувати ємність вектора через `m_assoc_entries.reserve(...)`.
3. Успішно виконано збірку WSL `ReleaseWithInstall` та перевірено `git diff --check`. Версію піднято до `0.13.442`.

## 0.1. v0.13.441 — захист звичайного хрому UI Sphaira

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.441.

1. У `App::Draw()` (`sphaira/source/app.cpp`) перенесено виклик `DrawChrome()` після відмальовки всіх немодальних віджетів і контенту, щоб елементи звичайного контенту не перекривали лінії заголовка та футера.
2. Додано метод `IsModal()` у `Widget` та перевизначено для модальних діалогів (`OptionBox`, `PopupList`, `ProgressBox`, `ErrorBox`, `HoldConfirmBox`, `HoldOkBox`, `KefirChangelogBox`), щоб вони малювалися поверху хрому та залишали ефект затемнення екрана.
3. Оновлено `WantsChrome()` у `fileview::Menu` (`file_viewer.hpp`), щоб повертати `!m_fullscreen`, вмикаючи стандартний хром у неповноекранному перегляді та вимикаючи у повноекранному.
4. Оновлено `ImageBounds()` у `file_viewer.cpp` з використанням констант `layout::ContentBand()`.
5. Пройдено збірку WSL `ReleaseWithInstall` та `git diff --check`, піднято версію до `0.13.441`.

## 0.1. v0.13.440 — інтерактивне керування чергою інсталяції (Skip / Cancel)

Статус: реалізацію та тести завершено; WSL ReleaseWithInstall пройшов 2026-08-12, версію піднято до v0.13.440.

1. У стані `Installing` призначити кнопку `B` на пропуск поточного пакета (`Skip package`), а `X` — на скасування всієї черги (`Cancel queue`).
2. Обидві дії показують явний діалог підтвердження через `App::Push<OptionBox>` із варіантами `No` (типовий) та `Yes`.
3. Підтвердження пропуску (`B` -> `Yes`) перериває встановлення лише поточного пакета через `m_skip_requested` та `m_cancel_event`, записує пакет як `Skipped` у статистиці та підсумку (без помилки в error list), після чого автоматично скидає сигнал переривання й переходить до наступного пакета черги.
4. Підтвердження скасування (`X` -> `Yes`) викликає `CancelSession()`, перериває інсталяцію зі збереженням уже встановлених пакетів і завершує сеанс.
5. Логіка уніфікована та працює ідентично для обох режимів черги: USB (`ThreadFunction`) та локальних файлів (`LocalThreadFunction`).
6. Додано переклади EN/UK для нових текстів підтверджень, додано host unit-тест `test_queue_outcome.cpp`, пройдено всі перевірки та піднято версію до `0.13.440`.

## 0.1. v0.13.439 — миттєва NTP-синхронізація

Статус: реалізацію та senior-review завершено; WSL ReleaseWithInstall пройшов 2026-08-10, версію піднято до v0.13.439. Залишилася ручна перевірка на Switch.

1. Залишити один фоновий worker і чинні NTP fallback-сервери. Першу спробу
   виконувати без стартової 10-секундної паузи; `Start()` для вже активного
   worker має лише розбудити його, а не створювати другий thread.
2. Після отримання NTP часу нічого не робити при різниці меншій за чинні 2 с.
   Якщо корекція потрібна, вимкнути live automatic-correction flag і записати
   user clock; успіх network clock не може маскувати помилку user clock.
3. Після успішного запису через чинний thread-safe `evman::FunctionalEventData`
   перейти на UI-потік, повторно ініціалізувати часову базу libnx і показати
   локалізований `Clock synced`. Це має одразу оновити всі чинні виклики
   `std::time()` без окремого offset-cache або змін у кожному caller.
4. Не показувати toast для вже точного годинника, відсутньої мережі чи помилки;
   зберегти чинний retry/backoff і діагностичні логи.
5. Перевірити WSL `ReleaseWithInstall`, підняти версію до `0.13.439`, оновити
   living docs і створити focused commit.

## 0.1. v0.13.438 — перемикач USB 3.0

Статус: реалізацію та senior-review завершено; EN/UK JSON і WSL
`ReleaseWithInstall` пройшли 2026-08-10, версію піднято до `v0.13.438`.
Залишилася ручна перевірка на Switch.

1. У `Tools → Налаштування кефіру` показувати один `USB 3.0` On/Off-рядок.
   Лише точне `u8!0x0` означає Off; відсутній файл або ключ означає типовий On.
2. Після успішного запису `[usb] usb30_force_enabled` commit-ити SD до показу
   діалогу. Помилка запису показує чинний error box і не пропонує reboot.
3. Повідомити, що зміна набуде чинності лише після перезавантаження, та дати
   вибір `Пізніше` / `Перезавантажити` через чинний forced-reboot шлях.
4. Перевірити EN/UK JSON, WSL `ReleaseWithInstall`, підняти версію до
   `0.13.438`, оновити living docs і створити focused commit.

## 0.2. v0.13.436 — незалежний скрінсейвер

Статус: реалізацію та senior-review завершено; host-тести й WSL
`ReleaseWithInstall` пройшли 2026-08-10. Відкрита лише ручна перевірка на Switch.

1. Не створювати окремий render thread: NanoVG/deko3d і HID лишаються в UI
   thread. Натомість зробити активний шлях неблокуючим: семплер графіка не
   потребує `m_mutex`, а prompt/snapshot читаються через `mutexTryLock` із
   поверненням останнього готового `SaverInfo`, якщо worker коротко зайнятий.
2. Прибрати `App::SetBlankBrightness()` з кожного кадру правого стіка. Тримати
   нове значення локально, одразу застосовувати його через `lbl*`, а INI
   записувати один раз тільки після виходу worker зі стану `Installing`; preview
   без активного запису може зберегти значення при закритті.
3. Залишити графік UI-власністю й додавати семпл кожні 0,5 с з атомарних
   `m_total_read`/`m_total_write`. Нульовий приріст є валідним нульовим семплом,
   а не причиною зупинити ані графік, ані скрінсейвер.
4. Додати в `SaverInfo` явний finished-стан. У `Summary` графік не малювати, а
   на його місці незалежно від `saver_fields` показувати локалізоване
   `Finished` / `Finished with errors`.
5. Додати одну INI-опцію timeout у секцію чинних screen-off налаштувань:
   `Off` за замовчуванням і короткий набір практичних preset-ів. Таймер працює
   лише у `State::Installing`, скидається будь-якою кнопкою, touch або рухом
   стіка та після ручного/автоматичного wake.
6. Виділити лише мінімальну чисту timeout-перевірку, потрібну host-тесту; не
   додавати scheduler, thread class чи залежність. Прогнати host-тести та відому
   WSL-збірку.
7. Після senior review підняти версію за чинною схемою, оновити
   `task.md`/`plan.md`/`walkthrough.md` і створити focused commit лише з
   screensaver delivery, зберігши всі наявні незакомічені зміни інших задач.

## 0.3. v0.13.437 — Text Viewer / Editor UX

Статус: реалізацію та corrective senior-review завершено; host-тести й WSL
`ReleaseWithInstall` пройшли 2026-08-10, версію піднято до `v0.13.437`.
Відкрита лише повторна ручна перевірка на Switch.

1. Додати одну спільну перевірку відомих текстових форматів і викликати її з
   головної дії `A` та контекстного меню File Browser. Спеціальні типи
   (`nro`, install, image, zip) залишити пріоритетними.
2. Передати text viewer неволодіючий `fs::Fs*` поточного `FsView` і окремий
   writable-прапорець. Нижній File Browser живе довше за pushed viewer, тому
   pointer безпечний; image-viewer і його `FsNativeSd` не зливати з цим шляхом.
3. Обробляти Open/GetSize/Read як одну fallible операцію: при будь-якій помилці
   не створювати порожній editable buffer, а відкласти показ Result до першого
   Update, коли viewer уже лежить у стеку UI.
4. Зберігати точний baseline останнього успішного Save. Після кожної зміни,
   Undo і Redo обчислювати dirty як `BuildText() != saved_text`; успішний Save
   оновлює baseline і очищає історію.
5. Не писати поверх оригіналу: створити sibling temp, повністю записати його,
   перейменувати оригінал у recovery backup, temp — в оригінал і відновити
   backup при помилці. Невдалий Save повертає failure, не закриває editor і не
   губить buffer; read-only джерела взагалі не отримують write actions.
6. Go to line затискає номер до фактичного діапазону та викликає
   `List::EnsureVisible`. Insert спочатку відкриває keyboard і додає новий
   рядок лише після підтвердження, як у перевіреному upstream UX.
7. Розділити File Viewer на явні `View` і `Edit`: View не має курсора та не
   змінює файл; Edit зберігає чинні undo/redo/save і редагування всього рядка
   через Switch keyboard.
8. Правий стік прокручує viewport незалежно. Лівий стік і D-pad рухають курсор
   лише в Edit; `A` одразу викликає keyboard для вибраного рядка без
   проміжного popup.
9. Для INI мінімально підсвітити section/comment/key/value. Подвійний touch tap
   по рядку з boolean RHS безпечно перемикає лише окремий токен `true` або
   `false`, не чіпаючи коментарі чи частини інших слів, і створює undo snapshot.
10. Залишити обмеження редагування великих файлів, але дозволити їх перегляд без
   безконтрольного читання всього файла в RAM. Додати одну невелику host-перевірку
   чистої логіки розпізнавання/toggle.
11. Після review прогнати host-тести та відому WSL-збірку, підняти версію за
   чинною схемою, оновити `task.md`/`plan.md`/`walkthrough.md` і зробити focused
   commit тільки з цієї функції.
12. Окремими атомарними комітами зафіксувати незалежні стабілізаційні виправлення:
    teardown transfer UI, auto-detect формату HB-іконок і USBDS detach на HOS 22.5.

## 1. MTP Games: merged NSP core

1. Залишити `BuildNspEntries` канонічним шляхом окремого дампу, але прибрати
   припущення, що всі NCA одного `NspEntry` лежать в одному storage.
2. Додати один merged-builder для BASE, останнього встановленого UPD і всіх
   встановлених DLC. DataPatch не включати до merged-пакета без окремо
   погодженої семантики назви.
3. Не дублювати однакові NCA та rights ID; PFS0 має містити всі потрібні NCA,
   CNMT, ticket і certificate та читатися потоком без тимчасового файла.
4. Формувати ім'я `Назва [TitleID][B+U65536+9DLC].nsp`. Відсутні складові
   опускати: `[B]`, `[B+U65536]`, `[B+9DLC]`.
5. Залишити чисте форматування суфікса доступним host-тесту без Switch SDK.

## 2. MTP Games: структура диска

1. Корінь read-only диска містить лише `Merged` і `Separate`.
2. `Merged` містить по одному об'єднаному NSP на встановлену гру.
3. `Separate/<Game [TitleID]>` містить наявні окремі BASE/UPD/DLC NSP через
   чинний `BuildNspEntries`.
4. Усі write/create/delete/rename операції залишаються забороненими; відкриті
   transfer handles мають переживати очищення кешу.
5. Не торкатися наявних незакомічених змін у forwarder-editor і `tests/run.sh`.

## Паралельний запит: TICO launchers

1. Повторно використати чинний механізм `assets/romfs/assoc/*.ini`: окремі
   TICO-асоціації завантажуються лише коли відповідний NRO реально існує у
   `/tico/cores`.
2. Додати одне необов’язкове поле фіксованого аргументу асоціації. Воно потрібне
   лише Gambatte (`gb`, `gbc`) і Genesis Plus GX (`genesis`, `master-system`,
   `game-gear`, `sega-cd`) та має однаково працювати для запуску і форвардера.
3. Один формат підпису використати в обох меню: спочатку RetroArch, потім TICO,
   усередині — назви ядер. Не змінювати загальний `PopupList` і не додавати
   залежностей.
4. Розпізнавати TICO-назви каталогів `sega-cd`, `fbneo`, `naomi`, `naomi2` та
   `atomiswave`; розширення брати з установлених ядер і чинних RetroArch INI.
5. Не перезаписувати незакомічені зміни delivery `0.13.432`, особливо у
   `filebrowser_forwarder.cpp`, `plan.md`, `task.md` та версії.

Реалізовано у `v0.13.433`: 17 конфігурацій покривають 13 установлених ядер,
спільний шлях аргументів працює для запуску, архівів і форвардерів; host-тести
та WSL-збірка пройдені. Залишилась апаратна перевірка на Switch.

## 3. Create repack — окремий етап після MTP

1. Додати `Create repack` у `Tools → Games → Game Actions`.
2. В окремому sidebar-вікні показувати лише фактично встановлені BASE, UPD і DLC;
   доступні компоненти за замовчуванням увімкнені, порожній вибір не запускає запис.
3. Розширити чинний merged NSP-builder прапорами вибору та повторно використати
   `NspSource` і `dump::Dump`, без нового формату чи проміжних файлів.
4. Результат записувати одним NSP у `/games` із погодженою схемою назви.
5. LayeredFS винести в наступний етап: опцію не показувати, доки немає коректної
   перебудови Program NCA, хешів і CNMT.

## 4. Верифікація і delivery

### UI-косметика

Прибрати порожнє посилання Progress з обох шапок вебсервера; на DBI-екрані USB-стан показувати над інструкцією, а Applet Mode — окремим вузьким текстовим блоком; перед NAND/SD-значеннями лишити видимий відступ. Host-тести та WSL-збірку пройдено.
1. Прогнати host-тести у WSL.
2. Зібрати `cmake --build --preset ReleaseWithInstall` у WSL.
3. Підняти версію до `0.13.435`, оновити `task.md`, `plan.md`,
   `walkthrough.md` і створити focused commit лише з прийнятими змінами.
4. Залишити hardware-gate відкритим до копіювання обох типів NSP на ПК і
   перевірки встановлення на Switch.

## 5. Закрити hardware-gates останніх delivery

1. Перевірити керування скрінсейвером у `v0.13.430`: обидва стіки, межі
   екрана, збереження яскравості та пробудження.
2. Перевірити чергу встановлення й скрінсейвер у `v0.13.429`: проєкцію
   NAND/SD без перекриття хедера, R/W-графік і preview.
3. Пройти USB-матрицю: DBI backend, Awoo/TinFoil і GoldLeaf v0.10+.
4. Повторити MTP smoke-test: лістинг телефона, перепідключення кабелю та
   встановлення NSP.

Результати ручних перевірок записувати в `tests.md`.

## 6. Наступні функціональні задачі

1. DBI UI: динамічний рядок журналу та наочний `ReviewQueue` із
   сегментованими NAND/SD-смугами.
2. Games: dump/verify/read-only mount, save integration і ticket details.
3. Network sources: NFS read-only завершено у `v0.13.449`; SFTP — лише після окремого погодження протоколу й UX.

Вбудований player залишається замороженим до окремого рішення.

## Правило завершення

Задача закривається після автоматичної перевірки збірки; hardware-задача —
лише після результату з реальної Switch у `tests.md`.
