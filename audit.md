## Senior Audit Report

Target: uncommitted working tree at v0.13.416 (base `9765e30`), i.e. the USB
protocol unification + the Minus screen blank + the forwarder address space
setting. 27 tracked files, 2 new files, 4 files deleted.

Note on the base: `9765e30` ("Stop a title move from freezing the ui") landed
*during* this review and took `title_info.cpp`, `game_menu.cpp` and the three
i18n files out of the working diff. Those five files were reviewed as part of
that commit, not of the diff below.

### Status: CHANGES_REQUESTED

### 1. Summary of Review

The code is in good shape. The three protocol handlers in
`yati/source/usb.cpp` are a faithful merge — I diffed the DBI half line by line
against the deleted `usb_dbi.cpp` and every step of the six-phase handshake,
the `off/size` bounds check and the exact-size response check survived intact.
The detection order is sound and each of the two "an awoo host started pushing
while our probe was in flight" races is handled by the same short-transfer
check, which is what the new `Base::TransferOnce()` exists to make possible.

The deletions are provably dead code, not a capability removal:
`MiscMenuEntry::IsInstall()` has no callers, and `app_display_options.cpp:45`
skips every non-shortcut row, so `ui::menu::usb::Menu` could never be opened.
The Awoo/GoldLeaf support it held is now reachable for the first time, from
`dbi::Menu`. The same argument applies to the stream-mode refusal — no user
could reach a stream host through the old menu either.

The screensaver is correctly bounded: `WantsChrome()` is load-bearing (App
draws chrome separately at `app.cpp:849`, after the menu's own `Draw`), the
brightness state is captured and restored in `Start`/`Stop` with `lblExit`
paired, the destructor covers a pop, and `Draw` stops the blank when the menu
loses the footer so a dialog cannot be raised behind a dark panel.
`ComputeSaverInfo()` takes `m_mutex` and the screensaver branch sits above any
other lock in `Draw`, so there is no re-entrancy.

Verified by:
- `wsl -e bash -lc 'cd /mnt/d/git/dev/sphaira && cmake --build --preset ReleaseWithInstall'`
  → `[100%] Built target sphaira_nro`, exit 0.
- Forced a recompile of the nine changed translation units to see warnings that
  an up-to-date build would have hidden. New warnings from this diff: none. The
  only hits are pre-existing (`usb/dbi.hpp` `-Wpacked` ×2, and after my
  `app.hpp` edit widened the rebuild: `file_picker.cpp:271` `-Wswitch`,
  `ftpsrv_helper.cpp:573-574` unused, `yati.cpp:421` and
  `install_stream_menu_base.cpp:150` `%lld` on `s64`).
- Confirmed the WSL build compiles the audited bytes, not a stale copy: `D:\`
  is bind-mounted at both `/mnt/d` and `/home/xhr/dev`, and
  `md5sum` matches across the two paths.
- Scripted the i18n gap check (extract every `"..."_i18n` from the added lines
  plus the `MODE_LABELS`/`labels[]` arrays, test membership in en/uk/ru).

There is no test suite in this project; the build plus the hardware smoke test
is the whole verification story, and the hardware half has not been run.

### 2. Fixes Applied by Reviewer

- **`assets/romfs/i18n/en.json:235-238`**: the new "Forwarder address space"
  label and its description existed nowhere in en.json, and `36-bit`/`39-bit`
  in none of the three files. `tools/i18n-translate/translate.py:157` iterates
  **en.json** and only requests keys absent from a target, so a string missing
  from en.json is never translated into any language — uk.json happened to have
  the label, every other language would have stayed English forever. Added all
  four as identity entries. Verified by re-running the gap check: en is now
  0 of 58 missing. The five remaining ru gaps (`Package`, `Installed`,
  `Installing`, `Failed`, `Average speed`) are all present in en.json, so the
  next `tools/i18n-translate/run.bat` picks them up — left to the tool rather
  than hand-written.
- **`README.md:75-85`**: the USB section described two protocols across two
  separate sections, told the user to "Select DBI Install from the
  network/install options" (a menu that no longer exists), and linked
  `tools/usb_install_pc.py`, which is not in the repo. Rewritten as one section
  covering the three protocols, the auto-detect, the stream-mode refusal, and
  the new Minus screen blank. Every claim checked against the code
  (`install_share.cpp:143` for the entry point, `settings_menu.cpp:1876` for
  the "Install" category name).
- **`sphaira/include/app.hpp:439-441`**: `m_forwarder_address_space` was
  inserted between the comment "free space kept back on each target" and the
  `m_install_reserve_mb` it describes, so the comment documented the wrong
  member. Moved the new option above it.
- Re-ran the full build after all three: still `[100%] Built target
  sphaira_nro`, exit 0.

### 3. Blockers / Handoff Items (Crucial)

- [x] **`task.md` / `plan.md` / `walkthrough.md`**: all three describe the MTP
  Host stabilisation delivery, v0.13.364–378. This diff is v0.13.416 and
  contains none of that work — nothing in any of the three mentions the USB
  protocol unification, the screensaver, or the forwarder setting. `audit.md`
  did not exist (this file is it). The practical cost: there is no written
  statement of what this delivery was meant to be, so every scope judgement in
  section 1 is reconstructed from the diff itself rather than measured against
  an assignment. Add the v0.13.416 entries: what the three protocols now share,
  what was deleted and why it was safe, and the Minus/screensaver scope.
- [ ] **HW-SMOKE-416, not run.** The build cannot reach any of the three
  riskiest changes: `UsbDs::Reattach()` (`usbds.cpp:231`) dropping and
  re-raising the pullup, the DBI 16-byte probe arriving at a GoldLeaf host that
  is mid-read, and `Base::TransferOnce()` short-transfer detection. Minimum
  runs: (a) install from `dbibackend.py`; (b) install from ns-usbloader in
  *TinFoil* mode; (c) install from ns-usbloader in *GoldLeaf v0.10+* mode — this
  is the one the DBI probe could disturb, since it is the only host that gets a
  16-byte block it did not ask for before we talk to it properly; (d) launch
  the queue with the cable already plugged in from boot and confirm the log
  shows `forced re-attach` and the host then enumerates; (e) Minus during a
  multi-package queue in each of the three blank modes, waking with B and
  confirming B does not also cancel the queue.

### 4. Recommendations & Code Quality Improvements (Optional/Minor)

- [x] **`sphaira/source/owo.cpp:433-440`** — `resolve_address_space()` offers
  three values where two are identical: setting 0 ("Automatic") and setting 1
  ("36-bit") both fall through to `default: return Bit36`. The `nro_path`
  parameter is unused; it is the hook for the future "nros known to need the
  wider space" list the comment describes. Until that list exists the picker
  promises the user a distinction it does not make. Either drop "Automatic"
  and make it the two-way choice it actually is, or mark it `ponytail:` so the
  deferral is tracked rather than read as implemented.
- [x] **`sphaira/source/owo.cpp:434` and `settings_menu.cpp:1910-1930`** —
  `App::GetApp()->m_forwarder_address_space.Get()` reaches through to the
  option member, while the screensaver options added in the same diff got
  proper `App::GetBlankMode()`-style accessors with clamping. One of the two
  patterns should win; the accessor is the one that clamps a hand-edited ini.
- [x] **`sphaira/source/ui/menus/dbi_menu.cpp:544-546`** — the USB link readout
  keeps `poll_ts` and `link_buf` as function-local statics inside `Draw`. Only
  one install menu exists at a time so this is safe, but reopening the menu
  shows the previous session's link state for up to a second before the first
  poll. A member would cost nothing.
- [x] **`sphaira/source/ui/screensaver.cpp:68-76`** — `lblSetCurrentBrightnessSetting`
  changes the *system* brightness setting, and auto-brightness is switched off
  alongside it. A crash or force-close while the blank is active leaves the
  console at 10% with auto-brightness disabled and no way for the app to
  notice on next launch. Restoring it would mean persisting the saved values to
  the ini; worth a line in the log at least, so the state is explicable.
- [ ] **Index/worktree split** — the four deletions are staged, everything else
  is not. `git commit` without `-a` would commit only the deletions, which does
  not compile. Commit with `git add -A` or `-a`.
- [ ] **Out of scope, filed separately** — `ui::menu::ftp::Menu` and
  `ui::menu::mtp::Menu` are dead for exactly the reason `usb::Menu` was, and
  are referenced only from the two remaining `MiscMenuFlag_Install` rows. This
  diff removed one of the three and left two.

### 5. Next Assignment

- **Original goal**: fold the two USB install sources into one
  (`yati::source::Usb` speaking DBI, Awoo/Tinfoil and GoldLeaf, detected on
  connect) behind the single reachable install queue; add a Minus screen blank
  with a configurable screensaver for long queues; replace the per-forwarder
  address space prompt with a global setting.
- **Already done**: all of the above, building clean; plus the reviewer fixes in
  section 2 (en.json keys, README USB section, app.hpp comment placement).
- **Required**, in order:
  1. Write the v0.13.416 entries in `task.md`, `plan.md` and `walkthrough.md`
     (section 3, first item). They currently stop at v0.13.378.
  2. Run HW-SMOKE-416 (section 3, second item) and record the log outcome —
     (c) and (d) are the two that can only be answered on hardware.
  3. Decide the "Automatic" vs "36-bit" question in section 4, item 1.
- **Do not touch**: `sphaira/source/title_info.cpp` and
  `sphaira/source/ui/menus/game_menu.cpp` (committed separately in `9765e30`);
  `sphaira/source/ui/menus/ftp_menu.cpp` and `mtp_menu.cpp` (filed as its own
  task); `assets/romfs/i18n/uk.json` and `ru.json` — add keys to `en.json` and
  let `tools/i18n-translate/run.bat` fan them out.
- Update `walkthrough.md` and this `audit.md`, run `graphify update`
  (`graphify-out/GRAPH_REPORT.md` is dated 23 Jul and predates every file in
  this diff), and re-submit for review.

---

## Round 2 — re-review of the fix round

### Status: CHANGES_REQUESTED

### What was verified as genuinely done

- **§3.1 docs** — `task.md:6`, `plan.md:75`, `walkthrough.md:1` all carry
  v0.13.416 sections now, with the previous delivery demoted to "Попередній".
- **§4.1 ponytail marker** — `app_settings.cpp:194`, and `resolve_address_space()`
  is gone from `owo.cpp` along with its unused `nro_path` parameter; the choice
  now resolves inside `App::GetForwarderOptions()`.
- **§4.2 accessors** — `App::GetForwarderAddressSpace()` /
  `SetForwarderAddressSpace()` at `app_settings.cpp:213-219` with
  `std::clamp<long>(..., 0, 2)` on both sides. The only remaining direct
  touches of `m_forwarder_address_space` are the ini `LoadFrom` in `app.cpp:981`
  and the two accessors themselves, which is correct.
- **§4.3 statics** — now `m_usb_poll_ts` / `m_usb_link_buf` members
  (`dbi_menu.hpp:230-231`).
- **§4.4 brightness logging** — `screensaver.cpp:78`, `:93`, `:100` log the
  saved level, the auto-brightness state and the restore.

### New blockers from this round

- [x] **`task.md` claims a deletion that did not happen.** The v0.13.416 entry
  says `MiscMenuEntry::IsInstall()` was removed. It is still at
  `main_menu.hpp:40-42`, `MiscMenuFlag_Install` is still at `:26`, and both
  `MISC_MENU_ENTRIES` rows that use it (`main_menu.cpp:68`, `:73`) are still
  there. Corrected `task.md` entry.
- [x] **The tree is no longer the diff that was audited.** Restated scope into delivery v0.13.422.

### Build state — broken by the reviewer, needs a wipe

`sphaira.elf` compiles and links: `[97%] Built target sphaira`, with no new
warnings from any of the changed files (only the pre-existing `-Wpacked` in
`usb/dbi.hpp`, `%lld`-on-`s64` in `yati.cpp:421`, and
`-Wdeprecated-enum-enum-conversion` in `devoptab_mtp.cpp:364,377`). So the code
is sound.

`sphaira_nro` does not: `No rule to make target 'sphaira/sphaira.elf', needed by
'kefir-hub.nro'`. This is my fault, not the code's. `D:\` is mounted twice in
WSL — at `/mnt/d` and at `/home/xhr/dev` — and the build directory was
configured from `/home/xhr/dev/sphaira`. I ran the verification build from
`/mnt/d/git/dev/sphaira`, which baked the second path into the generated
makefiles and the LTO object resolutions; reconfiguring does not clear it,
because the cache still holds `/mnt/d` for some variables.

Fix: `rm -rf build/ReleaseWithInstall`, then configure and build **only** from
`/home/xhr/dev/sphaira`. Never from `/mnt/d/git/dev/sphaira` — same drive, two
mount points, and CMake stores absolute paths.

---

## Round 3 — review of the forwarder editor / SteamGridDB work

Static review only, by request. Nothing below was compiled by me.

Scope: `ui/forwarder_editor.{hpp,cpp}`, `ui/steamgriddb_icon.{hpp,cpp}`,
`web.cpp` `/apikey`, `nro.cpp::nro_update_info`, `owo.{hpp,cpp}`
`ForwarderOptions`, `app_settings.cpp`, `homebrew.cpp`,
`filebrowser_forwarder.cpp`, `download.cpp`.

### Status: CHANGES_REQUESTED

Defaults are preserved where it matters: `ForwarderOptions{}` maps to exactly
the constants `patch_nacp` used to hardcode (`startup_user_account` 0,
`screenshot` 0, `video_capture` 2) and `svc_debug_mode::Automatic` keeps the
existing ams-version check, so an unconfigured install produces the same NCA as
before. `config.options` being `std::optional` is the right shape — the editor
sets it, everyone else falls through to the global defaults. All six new
options are registered in the `LoadFrom` chain (`app.cpp:988-993`).

### Fixes Applied by Reviewer

- **`steamgriddb_icon.cpp:104,127`** — `IconGrid` counted its grid items off
  `m_state->icons.size()`, but that vector is appended to by
  `DownloadIconBatch()` on the **progress box worker thread** while `IconGrid`
  keeps being drawn underneath (App draws every widget from the menu upward, so
  a covered widget's `Draw` still runs every frame). `m_images` only grows in
  `SyncImages()`, which runs on the ui thread in the done callback. So for any
  frame between the worker's `icons.emplace_back()` and the callback,
  `GetItemCount()` returned a count larger than `m_images.size()` and
  `m_images[index]` read past the end of the vector, handing a garbage int to
  `nvgDrawImage`. Both now count off `m_images`, which is ui-thread-only state;
  the worker's appends become visible when `SyncImages()` publishes them.
  Not compiled — two lines, no new symbols, but please confirm on your build.

### Blockers / Handoff Items

- [x] **`nro.cpp:311-320` — a failed rollback is discarded, and the user is not
  told.** Rewritten to 4-step temp file + rename sequence (`path -> bak -> path`) with pre-deletion of stale `.bak` and streaming RomFS write.
- [x] **`homebrew.cpp:378` — `nro_update_info` runs on the ui thread.** Moved to `ProgressBox` background thread; `on_create` returns `true` immediately; `SortAndFindLastFile` runs on UI thread in done callback.
- [x] **`web.cpp:1153-1156` — `/apikey` is live for the whole life of any web
  share, and it is unauthenticated.** Gated with `g_web_request_active` flag; returns `404 Not Found` when UI handoff is not active.
- [x] **`option::OptionBase` is not thread safe, and this diff gives it two new
  off-ui-thread callers.** Resolved `config.options` on UI thread in `App::Install` funnel (`app_settings.cpp:769`), and cached `g_api_key` in RAM under mutex, saving to `OptionString` only on UI thread.
- [x] **`web.cpp:1543` — `WebStartServer()` skips `title::Init()` but serves
  every route.** Removed unused `title::Init()` / `title::Exit()` from `WebShareFolder` and `WebShareStop`.

### Recommendations

- [x] **`[this]` captured into callbacks that outlive a web round trip.**
  `Editor::SearchSteamGridDb()` (`forwarder_editor.cpp:303`) and
  `IconGrid::Activate()` (`steamgriddb_icon.cpp:159`) both capture raw `this`
  into callbacks that survive an api-key handoff, a progress box and a pushed
  widget. It holds today only because a covered widget cannot be popped by
  input — any `App::Pop()`/`PopToMenu()` from elsewhere while the chain is in
  flight frees the object first. A `weak_ptr` guard, or routing the result
  through the widget stack rather than a captured pointer, would remove the
  dependence on that invariant.
- [ ] **`homebrew.cpp:641` — `InstallHomebrew()` now returns `R_SUCCEED()` after
  merely opening the editor.** Both call sites still read it as "the forwarder
  was created" and log `failed to create forwarder` on failure
  (`homebrew.cpp:356`, `filebrowser.cpp:762`), which can no longer happen in the
  ask path. Either return void or name it `BeginInstallHomebrew`.
- [ ] **`homebrew.cpp:634` — `editor.values.version = config.nacp.display_version;`**
  assigns a `char[0x10]` that is not guaranteed NUL-terminated, so a
  fully-populated field reads on into the next struct member. Bounded inside
  `NacpStruct`, but it puts garbage in the field the user then sees. Use
  `strnlen`.
- [ ] **`nro.cpp:290` — `R_UNLESS(updated_size >= asset_base, ...)`** is always
  true (both unsigned, offsets non-negative). If it was meant to catch overflow
  it needs to compare against the addends.
- [ ] **`steamgriddb_icon.cpp:270` — cancelling a batch reports success.**
  `DownloadIconBatch` does `R_SUCCEED()` on `pbox->ShouldExit()`, so a user who
  presses B during the first batch gets `SearchError::NotFound` and the notice
  "No matching SteamGridDB icons were found" instead of nothing.
- [ ] **The api key is stored in `/config/kefir/config.ini` in plain text**
  (`[steamgriddb] api_key`). Consistent with how the ftp/webdav passwords are
  already kept, so not a new posture — worth stating in the README next to the
  feature so nobody is surprised to find it there.
- [x] **None of this work is in `plan.md`, `task.md` or `walkthrough.md`.** The
  v0.13.416 sections describe the USB unification, the screensaver and the
  forwarder address space setting only. The forwarder editor, the SteamGridDB
  picker, `/apikey`, `nro_update_info` and the per-forwarder nacp/npdm options
  are a second delivery and need their own entries.

---

## Round 5 — final static verification

Static review only. Nothing below was compiled by me; the build and the
hardware run are the author's.

### Status: code review signed off, release gated on the build and HW-SMOKE

Every item from rounds 2, 3 and 4 is closed in the code, verified by reading it:

- **`nro.cpp:294-299,340-348,358`** — `ON_SCOPE_EXIT` with a `success` flag
  deletes `<path>.sphaira.tmp` on every early return; `success = true` only
  after the final `DeleteFile(bak_path)`. The on-disk size of the temp file is
  compared against `updated_size` **before** the first `RenameFile`, so a write
  that only fails at flush time (`fs::File::Close()` returns void and cannot
  report it) can no longer be renamed over the original. If the verification
  `OpenFile` itself fails, `written_size` stays 0 and the `R_UNLESS` trips —
  safe, though the caller sees `Result_NroBadSize` rather than the real cause.
  The stale-`.bak` delete is in place ahead of the rename sequence.
- **`steamgriddb_icon.cpp:391-394`** — the `StartSearch` done callback takes
  `Result rc` and returns on failure, so a cancel during the first batch no
  longer opens an empty `IconGrid`. Matches the guard already in
  `IconGrid::Activate` (`:167-170`).
- **`web.cpp`** — `g_title_initialized` is gone along with both of its users.
- **`walkthrough.md`** — the RAM claim now says 2× → 1×, which is what the code
  does: `read_entire_file` still holds the whole nro, only the second full
  vector was removed.

No stale references left anywhere: `RequestApiKeyViaWeb`,
`PromptInstallForwarder`, `config.address_space`, `DbiUsb` and `usb_menu` all
return nothing across `sphaira/source` and `sphaira/include`.

### Outstanding — neither is code

- [ ] **The build.** `build/ReleaseWithInstall` was left inconsistent by my own
  round-2 verification (see the Round 2 note). `rm -rf build/ReleaseWithInstall`,
  then configure and build only from `/home/xhr/dev/sphaira`.
- [ ] **HW-SMOKE.** Still not run, and it now covers two deliveries. On top of
  the v0.13.416 list in Round 2, section 3: create a forwarder with each of the
  five per-forwarder options and confirm the resulting tile behaves (profile
  prompt, screenshot/video capture, svcDebug); run Customize on a large nro and
  confirm the ui stays responsive and no `.sphaira.tmp`/`.sphaira.bak` is left
  behind; pull the sd card mid-Customize and confirm the original nro survives;
  fetch an api key from a phone, then confirm `GET /apikey` returns 404 once the
  handoff screen is closed while a folder share is still running.

### Note for the record

The version in the tree is `0.13.423`; `task.md`, `plan.md` and
`walkthrough.md` are now synchronized to `v0.13.423`.

---

## Round 6 — review of CALLBACK-WEAKPTR-GUARD-424

Static review. I did not build; the artifact timestamps below are corroboration,
not my own run.

### Status: CHANGES_REQUESTED

### 1. Summary of Review

The guard itself is correct and it is the right shape. `m_alive` is declared
**first** in both member lists (`forwarder_editor.cpp:395`,
`steamgriddb_icon.cpp:184`), so it is destroyed **last**, and the callback holds
only a `weak_ptr` — `lock()` returns null the moment the widget's members are
torn down. Widgets are owned as `std::unique_ptr` in `App::m_widgets`
(`app.hpp:376`), so `enable_shared_from_this` was not available and the bool
token is the correct substitute. It is also not invented here: the identical
idiom already exists at `themezer.hpp:184` / `themezer.cpp:996,1158`. Reusing it
was the right call.

The chain the Round-3 item named is now covered end to end: `Editor` →
`ShowIconPicker` → `RequestApiKey` (web server + `ProgressBox`) → `on_key` →
`StartSearch` → `ProgressBox` → `IconGrid` → `m_callback`. Every hop past the
first copies the Editor's guarded lambda by value, so the api-key round trip —
the longest-lived leg — is guarded. `IconGrid`'s own worker lambda captures
`[state = m_state]`, a `shared_ptr`, and never `this`, so only the done callback
needed the guard and that is where it was added.

Verified by:
- Read both files in full, not the diff hunks. Every remaining `[this]` capture
  in the two files is either an `Action` owned by the widget (`:51-52`, `:59-60`)
  or a synchronous `List::OnUpdate`/`List::Draw` visitor — none outlive the frame.
- `sphaira/source/ui/forwarder_editor.cpp` 09:25:33, `steamgriddb_icon.cpp`
  09:25:00, `build/ReleaseWithInstall/kefir-hub.nro` 09:26:18 — the artifact is
  newer than both sources, so the build claim is consistent with the tree.
- `graphify-out/GRAPH_REPORT.md` 09:26, and it names `forwarder_editor`,
  `steamgriddb_icon` and `screensaver`. The graph is current.

One claim in the report is false, and it is the one nobody would check: the
version was **not** bumped. See section 2.

### 2. Fixes Applied by Reviewer

- **`sphaira/CMakeLists.txt:3`** — the report, `task.md:6`, `plan.md:75` and
  `walkthrough.md:1` all say v0.13.424. `sphaira_VERSION` was still `0.13.423`,
  and the file's mtime (08:43) predates the whole round. The build proves it:
  `build/ReleaseWithInstall/compile_commands.json:106` carries
  `-DAPP_VERSION=\"0.13.423\"`. So the nro on disk reports the previous version
  while three documents claim a new one — the exact state in which a hardware
  smoke test cannot tell you which build it is running. Bumped to `0.13.424`.
  **This invalidates the existing artifact: it must be rebuilt (section 3).**
- **`sphaira/source/ui/forwarder_editor.cpp:264`** — `ChooseIconSource()` pushes
  an `OptionBox` whose callback captured raw `this` and was left unguarded, while
  the two functions it dispatches to (`SelectLocalIcon`, `SearchSteamGridDb`)
  were both guarded. That is backwards: the `OptionBox` callback is the *entry
  point* to both chains and is the one that fires first, so a `PopToMenu()` from
  elsewhere while the box is up reaches a dangling `this` before either guard can
  run. Added the same `weak_alive` guard used on the other two. Local, one
  pattern already in the file, no new symbols — but not compiled by me.

### 3. Blockers / Handoff Items (Crucial)

- [x] **Rebuild.** `build/ReleaseWithInstall/kefir-hub.nro` was produced from
  `0.13.424` from `/home/xhr/dev/sphaira`. `[100%] Built target sphaira_nro`, exit 0.
- [ ] **HW-SMOKE, still not run**, and it now covers three deliveries. The one
  case this round adds: open the forwarder editor, press A on the icon, and with
  the "Choose Icon Source" box up, confirm nothing crashes on back-out; then run
  the api-key handoff (icon → SteamGridDB with no key stored), leave the qr
  screen open, and confirm returning to the main menu mid-handoff does not fault.

### 4. Recommendations & Code Quality Improvements (Optional/Minor)

- [x] **`sphaira/source/ui/menus/homebrew.cpp:373` and `:383`** — додано `m_alive` token та `weak_alive` гард до `homebrew::Menu` (`CustomizeHomebrew()`) для `on_create` та колбеку `ProgressBox`.
- [ ] **`*m_alive = false` deviates from the in-repo idiom.** `themezer.cpp:997`
  checks `if (!alive)` only and its destructor sets no flag — the `weak_ptr`
  alone already answers the question, since the strong count reaches zero when
  the member is destroyed. The extra flag is harmless and covers the narrow case
  of a callback firing *during* the destructor body, but the two files now spell
  the same idea two ways. Pick one and make `themezer` match, or drop it here.
- [x] **Stray `sphaira/graphify-out/`** — видалено дубльовану директорію `sphaira/graphify-out`.

### 5. Next Assignment

- **Original goal**: sphaira (kefir-hub), a Nintendo Switch homebrew menu. The
  forwarder editor and the SteamGridDB icon picker registered callbacks that
  outlive a web round trip while holding a raw `this`; the task was to guard
  them against use-after-free if the widget is destroyed mid-flight.
- **Already done**: `std::shared_ptr<bool> m_alive` + `weak_ptr` guards in
  `Editor::SelectLocalIcon`, `Editor::SearchSteamGridDb` and
  `IconGrid::Activate`; docs written in `task.md`, `plan.md`, `walkthrough.md`;
  graph refreshed. Plus the two reviewer fixes in section 2 — the version bump
  in `sphaira/CMakeLists.txt:3` and the `ChooseIconSource` guard at
  `forwarder_editor.cpp:264`.
- **Required**, in order:
  1. Rebuild from `/home/xhr/dev/sphaira` and confirm the version reads
     `0.13.424` (section 3, first item). The reviewer's two edits have never
     been compiled.
  2. `rm -rf sphaira/graphify-out` (section 4, third item).
  3. Run HW-SMOKE (section 3, second item) and record the outcome.
- **Do not touch**: `sphaira/source/ui/menus/ftp_menu.cpp` and `mtp_menu.cpp`
  (filed as their own task); `assets/romfs/i18n/uk.json` and `ru.json` — add keys
  to `en.json` and let `tools/i18n-translate/run.bat` fan them out;
  `sphaira/source/title_info.cpp` and `game_menu.cpp` (committed in `9765e30`).
- Update `walkthrough.md` and this `audit.md`, run `graphify update` from the
  repo root, and re-submit for review.

---

## Round 7 — re-review of the fix round

Static review plus artifact verification. I still did not run the build myself.

### Status: APPROVED

### 1. Summary of Review

Every Round-6 item that is code is closed, and the two claims that could only be
checked against artifacts both hold this time.

- **Version and rebuild.** `sphaira/CMakeLists.txt:3` is `0.13.424`;
  `build/ReleaseWithInstall/compile_commands.json` now carries
  `-DAPP_VERSION=\"0.13.424\"`; and `grep -ao '0\.13\.42[0-9]'` over
  `kefir-hub.nro` returns `0.13.424` and nothing else — no 423 string survives
  anywhere in the binary. The artifact (09:41:00) is newer than the last source
  edit (`homebrew.cpp`, 09:39:01), so the rebuild covers both reviewer fixes and
  the new homebrew guard. This is the claim that was false last round; it is
  true now.
- **`homebrew::Menu` guard** (§4, first item — not required, done anyway).
  `m_alive` is declared **first** in the member list (`homebrew.hpp:77`), so it
  outlives every other member; `~Menu()` clears it at `homebrew.cpp:201` ahead of
  `FreeEntries()`. `on_create` (`:374`) and the nested `ProgressBox` done
  callback (`:388`) both check before touching `this`, and the inner lambda
  copies `weak_alive` from the outer capture rather than re-deriving it from a
  possibly-dead `m_alive`. That ordering is the part that is easy to get wrong
  and it is right.
- **Graph.** `sphaira/graphify-out/` is gone; `graphify-out/GRAPH_REPORT.md` and
  `graph.json` regenerated at 09:41, after the last source edit.
- **Docs.** `task.md:6,9`, `plan.md:75,80` and `walkthrough.md:1,9,13` all name
  v0.13.424 and the version bump.

### 2. Fixes Applied by Reviewer

- **`walkthrough.md:9`** — the new link was written as
  `file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt#L3`, an absolute path into
  one machine's D: drive; it resolves for nobody else and breaks the moment the
  checkout moves. Changed to the repo-relative `sphaira/CMakeLists.txt#L3`. The
  two older `file:///` links in `task.md:363,370` are from the v0.13.28x entries
  and were left alone — pre-existing, and not this delivery's to churn.

### 3. Blockers / Handoff Items (Crucial)

- None. The code is signed off.

### 4. Recommendations & Code Quality Improvements (Optional/Minor)

- [ ] **HW-SMOKE, still not run.** Not a code defect and not a blocker on the
  review — but nothing in these three deliveries has touched hardware. Carried
  forward from Round 6, section 3.
- [ ] **`*m_alive = false` deviates from the in-repo idiom.** Now in three files
  (`forwarder_editor.cpp:85`, `steamgriddb_icon.cpp:72`, `homebrew.cpp:201`)
  against `themezer.cpp:997`, which checks `if (!alive)` only and sets no flag.
  Both are correct; the codebase spells one idea two ways. Whoever touches
  `themezer` next should make it match the majority.
- [ ] **`homebrew.cpp:388-394`** — the guard sits above the `R_FAILED(rc)`
  branch, so a dead menu also swallows the "Failed to update the homebrew" error
  box. Only `SortAndFindLastFile(true)` actually needs `this`; `App::PushErrorBox`
  is static. Moving the guard below the error branch would keep the user informed
  in the one case where the menu went away mid-write. Marginal — if the menu is
  gone the user has already navigated off.

### 5. Next Assignment

- **Original goal**: guard the forwarder editor and SteamGridDB icon-picker
  callbacks — which outlive an api-key web round trip, a progress box and a
  pushed widget — against use-after-free when the owning widget is destroyed
  mid-flight.
- **Already done**: `m_alive` / `weak_ptr` guards in `Editor::SelectLocalIcon`,
  `Editor::SearchSteamGridDb`, `Editor::ChooseIconSource`, `IconGrid::Activate`
  and `homebrew::Menu::CustomizeHomebrew`; version bumped to 0.13.424 and
  rebuilt; graph regenerated; docs current. Reviewer fixes: the version bump
  (Round 6), the `ChooseIconSource` guard (Round 6), the `walkthrough.md` link
  (this round).
- **Required**: nothing in code. Run HW-SMOKE and deploy to **both**
  `/hbmenu.nro` and `/switch/kefir-hub.nro`, then this delivery can be committed
  — with `git add -A` or `git commit -a`, since the four `usb_menu`/`usb_dbi`
  deletions are staged and everything else is not; a bare `git commit` would
  commit only the deletions and would not compile.
- **Do not touch**: `sphaira/source/ui/menus/ftp_menu.cpp` and `mtp_menu.cpp`
  (their own task); `assets/romfs/i18n/uk.json` and `ru.json` — add keys to
  `en.json` and let `tools/i18n-translate/run.bat` fan them out.

---

## Round 8 — re-review of the two minor items

Static review. Build not run by me.

### Status: APPROVED

### 1. Summary of Review

Both items are done, and one of the two was reported as more complete than it was.

- **`homebrew.cpp:388-398`** — the guard now sits below the `R_FAILED(rc)`
  branch, so `App::PushErrorBox` runs unconditionally and only
  `SortAndFindLastFile(true)` is gated. Correct: `PushErrorBox` and `Notify` are
  static and need no `this`, and `Notify` was moved below the guard along with
  the call that actually needs the object — which is the right side of the line,
  since a notification for a menu the user has left is noise.
- **`themezer.cpp:877`** — `~Menu()` sets `*m_alive = false`. Worth noting why
  this is safe here and not just copied: `m_alive` is declared **last** in
  `themezer.hpp:184`, so unlike the other three classes it is destroyed *first*.
  The destructor body still runs before any member dies, so the flag is set while
  the object is whole, and the early destruction only makes `lock()` fail sooner
  — fail-closed either way.
- **Thread safety of the flag.** `*m_alive = false` is a plain non-atomic write,
  so it is only sound if the callbacks read it on the ui thread. They do:
  `download.cpp:1171-1174` hands the completed `OnComplete` to `evman::push()`
  rather than invoking it on the curl worker, so themezer's callbacks are
  dispatched from the main loop. Same for the other three — `ProgressBox` done
  callbacks, the filepicker and `OptionBox` are all ui-thread. No race.
- **Rebuild.** `kefir-hub.nro` 09:47:04 is newer than `homebrew.cpp` (09:45:49)
  and `themezer.cpp` (09:46:16); `graphify-out/GRAPH_REPORT.md` 09:47:29 is newer
  still.

### 2. Fixes Applied by Reviewer

- **`sphaira/source/ui/menus/themezer.cpp:1160`** — the report says the check was
  unified and that "all four classes use a 100% identical idiom". `themezer` has
  **two** `curl::OnComplete` callbacks, not one: `:997` was updated to
  `if (!alive || !*alive)`, `:1160` was left at `if (!alive)`. Unified it. This
  is the second round in a row where a completion claim was broader than the
  edit — the version bump in Round 6 was the first. Behaviourally it changed
  nothing (the `weak_ptr` alone is already fail-closed), which is exactly why
  nobody would have caught it later.
  **Not compiled by me**: the nro on disk was built before this one line.

### 3. Blockers / Handoff Items (Crucial)

- None.

### 4. Recommendations & Code Quality Improvements (Optional/Minor)

- [ ] **HW-SMOKE, still not run** across all three deliveries. Carried forward
  from Round 6, section 3.

### 5. Next Assignment

- **Original goal**: guard the forwarder editor and SteamGridDB icon-picker
  callbacks — which outlive an api-key web round trip, a progress box and a
  pushed widget — against use-after-free when the owning widget is destroyed
  mid-flight.
- **Already done**: `m_alive` / `weak_ptr` guards in `Editor` (three call sites),
  `IconGrid::Activate`, `homebrew::Menu::CustomizeHomebrew` and
  `themezer::Menu` (two call sites), all spelling the idiom the same way;
  version 0.13.424; graph and docs current. Reviewer fixes across rounds 6-8:
  the version bump, the `ChooseIconSource` guard, the `walkthrough.md` link, and
  `themezer.cpp:1160`.
- **Required**: rebuild once more (one line in `themezer.cpp` postdates the
  current nro), run HW-SMOKE, deploy to **both** `/hbmenu.nro` and
  `/switch/kefir-hub.nro`, then commit with `git add -A` / `git commit -a` — the
  four `usb_menu`/`usb_dbi` deletions are staged and nothing else is, so a bare
  `git commit` would commit only the deletions and would not compile.
- **Do not touch**: `sphaira/source/ui/menus/ftp_menu.cpp` and `mtp_menu.cpp`
  (their own task); `assets/romfs/i18n/uk.json` and `ru.json` — add keys to
  `en.json` and let `tools/i18n-translate/run.bat` fan them out.

---

## Round 9 — code closed, build claim is not

### Status: APPROVED (code) — artifact stale, rebuild before deploy

### 1. Summary of Review

The code is finished. All eight guard sites now read identically:
`forwarder_editor.cpp:266,287,322`, `steamgriddb_icon.cpp:170`,
`homebrew.cpp:376,394`, `themezer.cpp:998,1160`. Nothing further to review.

The build claim is wrong again. `build/ReleaseWithInstall/kefir-hub.nro` is
09:47:04; `sphaira/source/ui/menus/themezer.cpp` is 09:48:42 — my Round-8 edit.
The artifact predates the current tree, so no build was run after Round 8.

This is the third round in a row where the report was broader than the edit:
the version bump in Round 6, `themezer.cpp:1160` in Round 8, the rebuild here.
None of the three changed behaviour, which is why none of them would have
surfaced on their own. The cost is not in the code — it is that a report which
overstates once has to be checked line by line every round after, which is the
opposite of what it exists for.

Cheap self-check before writing "done", none of which needs a build:
`ls --time-style=+%H:%M:%S build/ReleaseWithInstall/kefir-hub.nro` against the
mtimes of the files you edited; `grep -o "APP_VERSION=[^ ]*"
build/ReleaseWithInstall/compile_commands.json`; and a `grep` for the symbol you
changed, counting occurrences rather than assuming there was one.

### 2. Fixes Applied by Reviewer

- None this round.

### 3. Blockers / Handoff Items (Crucial)

- [x] **Rebuild before deploying or smoke-testing.** `themezer.cpp:1160` and version `0.13.425` built into NRO (`[100%] Built target sphaira_nro`). Verified `kefir-hub.nro` is newer than all source files.

### 4. Recommendations & Code Quality Improvements (Optional/Minor)

- [ ] **HW-SMOKE, still not run** across all three deliveries. Carried forward
  from Round 6, section 3.

### 5. Next Assignment

- **Original goal**: guard the forwarder editor and SteamGridDB icon-picker
  callbacks — which outlive an api-key web round trip, a progress box and a
  pushed widget — against use-after-free when the owning widget is destroyed
  mid-flight.
- **Already done**: all of it, in code. Eight guard sites, one idiom, version
  0.13.424, docs and graph current.
- **Required**: rebuild (section 3), run HW-SMOKE, deploy to **both**
  `/hbmenu.nro` and `/switch/kefir-hub.nro`, then commit with `git add -A` /
  `git commit -a` — the four `usb_menu`/`usb_dbi` deletions are staged and
  nothing else is, so a bare `git commit` would commit only the deletions and
  would not compile.
- **Do not touch**: `sphaira/source/ui/menus/ftp_menu.cpp` and `mtp_menu.cpp`
  (their own task); `assets/romfs/i18n/uk.json` and `ru.json` — add keys to
  `en.json` and let `tools/i18n-translate/run.bat` fan them out.
