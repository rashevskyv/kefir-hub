# CHANGELOG

Newest first. One heading per version; add 1-3 lines per delivery (see AGENTS.md).
Entries without a detail line are commit titles only; their verification state was not recorded.

## v0.14.051 — Server and typing windows show the console IP address (not kefir.local); SteamGridDB API key window too
- Found with an outside report (SteamGridDB API key: "the QR leads to a local site that goes nowhere") and on the console: the wait screen for typing from a phone showed `kefir.local/input | 192.168.50.69/input` on one line that did not fit and scrolled away, and the Web Sharing Server / SteamGridDB windows still showed `http://kefir.local` (many Android phones cannot resolve `.local`; the QR already used the IP since 0.14.045). The hint line under the address is 12 px from the box edge instead of 30 (`progress_box_draw.cpp`), so the English hint no longer scrolls. Now `remote_input.cpp`, `steamgriddb_icon.cpp` and `WebPushServerProgressBox` callers (`install_share.cpp`, `console_games_transfer.cpp`) show the IP address that the QR opens.
- host tests: pass (quick) · nro: built · switch: verified — on the console the wait screen shows `192.168.50.69/input` and the hint fits (EN; UK hint is longer and still scrolls), the Web Sharing Server window shows `http://192.168.50.69` (EN, UK). Not tried from a phone; the SteamGridDB window uses the same code path. New doc shots taken from the console: `file-browser-add-source`, `file-browser-network-list`, `file-browser-remote-input`, `sharing-web-server` (EN, UK); drafts for 0.14.047-0.14.051 on the File Browser and Sharing pages.

## v0.14.050 — Settings > Sources > Options > Enter/Connect opens every configured protocol (it said "Browsing is not supported for this protocol yet." for FTP, HTTP and WebDAV)
- `settings_menu.cpp`: the menu entry opens the File Browser on the location, like the row itself does. The string stays in i18n (unused keys are not removed here).
- host tests: not run (one branch removed) · nro: not built (change is a deleted branch) · switch: pending.
- Verified on the console with 0.14.049 before this: WebDAV with the password typed by the user: Test Connection ok, root and `sphaira-saves` listed, a backup zip opens (HEAD + ranges, 2 hidden entries), leaving the browser with it open; Saves > Sync with cloud (WebDAV) uploaded the SD backups of Celeste to `sphaira-saves/Celeste/Account/` on the server. Not explained: one Hub crash at 16:59 right after `[FILE] open net_f4f7071a:/nfs-save-backup.zip` while the NFS test server was being removed (no crash report was written).

## v0.14.049 — Scripted input: `text <string>` answers the system keyboard (0.14.048 had it behind DOCS_DEMO, which release builds do not define)
- `demo_cmd.hpp` / `demo_input.cpp` / `swkbd.cpp`: a queued `text <string>` line is returned by the next keyboard (Rename, Create Folder, Go to path) without opening it, so a test run can type; `hub-input.ps1` joins `text name` given as two words. Nothing changes unless "Scripted input" is on.
- host tests: pass (test_demo_cmd) · nro: built · switch: verified — on FTP Rename (folder t1 -> t3) and Create Folder (t2) typed by the script, the server has them; Create File makes no file on FTP (an empty upload is never started, as before). Sources through the form on the console: NFS (nfs://45.134.218.34/srv/kefir-test over the internet: list with sizes and dates, a .zip opens as a folder, files marked read-only) works; SMB could not be tried from this network (the provider drops outbound 445; Hub's SMB has no port field, libsmb2 connects to 445 only); WebDAV with a password and Restore save data from a network zip are still pending.

## v0.14.047 — FTP folders delete with their content; archives from a share cannot extract onto the SD; the title keeps the source name when walking up
- Review of 0.14.046 on the console: Delete of a folder with files sent `DELE folder` (the cache that picked RMD was cleared by every child delete); `devoptab_rmdir` now always sends RMD. Extract from an archive that lives on a network share wrote through the SD filesystem to a `net_xxx:/` path: the entry is offered only when the .zip lives on the SD. A .zip opens on the SD and on network shares only (it had opened on USB/MTP too, untested). `DisplayPath` also matches `net_xxx:` without the slash (walking up from a folder).
- Source: HTTP added through the form (Server URL typed from the PC page, Test Connection ok, mounted, listed with sizes). Found: an archive on a server without Range support (python `http.server`) cannot be opened: the zip reader seeks back from the end and gets the whole file each time (real nginx/apache serve ranges; the FTP source opens fine).
- host tests: pass (quick) · nro: built · switch: verified on the console (0.14.046 build + these edits): FTP — sizes, dates hidden, delete file, delete folder with content, cut/paste into a folder (RNFR/RNTO seen in the server log), paste onto an existing name asks to replace, zip opens, leaving the browser with a zip open; HTTP — add through the form, Test Connection, mount, list. Pending: Create Folder / Rename typed on FTP (system keyboard), WebDAV with the password, SMB, NFS, Restore save data from a network zip (target list shows "Unknown (user) [sp:1 id]", see plan).

## v0.14.046 — FTP source: sizes, delete, rename, new folder, archives; the browser shows the source name; leaving the browser with a network archive open no longer crashes
- Found on the console with 0.14.045 (FTP at 192.168.50.68:2121): every file was 0.00 KiB / 01/01/1970; from the code: a name with no dot was a folder, every path "existed", Delete and Rename sent WebDAV verbs (`DELETE`/`MOVE`) that an FTP server does not know. `devoptab_curl_device.cpp`: lstat answers from the LIST of the parent folder (cached per folder in `m_ftp_stat`, cleared on unlink/rmdir/mkdir/rename/write-close), so size and type are real and a missing name is ENOENT (it was "exists" for every path); FTP delete = DELE/RMD, rename = RNFR/RNTO, mkdir = MKD, each on a fresh connection (a reused one stands in the last listed folder). `devoptab_curl_file.cpp`: an FTP file opens with its listed size (HEAD gave none, so seeks and zip failed).
- File Browser: the title shows the source name (`FTP:/`) instead of `net_bee6ea90:/` (`DisplayPath`); no date when the server gave none; a .zip opens as a folder on any source (was SD only: A did nothing, silently) and offers [[Restore save data]]. Crash fixed: `Menu::~Menu` unmounted network devices before the views closed an open archive (data abort in `_lseek_r`, crash report 01791628086); the views are released first.
- `tools/dev/hub-input.ps1` joins `wait 2` / `lang uk` given as two words (they were sent as two bad lines).
- host tests: pass (quick) · nro: built · switch: see 0.14.047.

## v0.14.045 — Network sources: an edited source mounts with its new settings; phone/PC typing is usable (QR by IP, page in the Hub language, B leaves); Test Connection says why it failed
- Found on the console with 0.14.044: FTP added through the new form passed Test Connection but [[Mount]] failed — the Sources panel had cached the source list when it opened ("ftp://" with no host). `filebrowser_sources.cpp`: the chosen network source is re-read from locations.ini by name (`NetworkFsEntry`); after Add network location the panels close (`App::PopToMenu`) instead of stacking a second, stale Sources panel; Mount rows drop "-> " (the array has a check mark) and "FTP (FTP)" becomes "FTP".
- Typing from a phone/PC (`remote_input.cpp`, `web.cpp`, `web_router.cpp`, `web_pages/remote.hpp`, `progress_box`): the QR encodes the IP URL (`WebShareResult::ip_url`; kefir.local fails on many Android phones), the wait screen shows both addresses, B there cancels without "Are you sure?" (`ProgressBox::SetCancelWithoutConfirm`), the method choice is Back / Manual (Keyboard) / From Phone / PC (B used to open the keyboard); the page gets its words from `/input/config` in the UI language (was English "Send to Switch", "Paste or type the address" for every field), hides a repeated guide, uses `type=password` for `Options::secret`.
- Source form: Server URL starts empty (was "webdav://") with a protocol example as placeholder; Password is secret. `TestLocationConnection` returns a separate code for 401/403/530 and all three "Connection test failed!" boxes add `ConnectionFailureText`: refused login vs unreachable server.
- 9 new keys in all 26 languages (uk reviewed). host tests: pass (quick, i18n contract) · nro: built · switch: verified (FTP added through the form mounts and lists; shown in 0.14.046). Pending: QR on the wait screen opens by IP; WebDAV with a wrong password says "The server refused the login".

## v0.14.044 — Network sources: one form right after the protocol, every field typed on the console or from a phone/PC
- Found on the console: Add network location asked only for a name with the console keyboard, saved an empty source, and the user had to find it in the list and choose Edit Source; no field could be typed from a phone/PC (other inputs in the Hub can).
- `filebrowser_sources.cpp` `AddNetworkLocationInteractive`: protocol → a source named after it ("WebDAV", "WebDAV (2)") is saved and `settings::SourceEditMenu` opens at once (on top of the refreshed source list); the name-first keyboard, the URL guessing from the name (`IsUrlLike`) and the overwrite/keep-both prompt are gone. `settings_categories.cpp` (WebDAV for saves) no longer opens the form a second time.
- `settings_sources.cpp` `SourceEditMenu`: new first row Name (rename, refuses a taken name); Server URL / IP / Share / Port / Username / Password go through `remote_input::PromptTextInput` (Manual (Keyboard) or From Phone / PC).
- uk.json: screen title "Файловий браузер" (was "Файловий менеджер", the Tools tile says "браузер"), "Файлів: %zd" / "Папок: %zd" and "Вставити файли?" instead of "(и)" guesses. 2 new keys in all 26 languages.
- host tests: pass (quick, i18n contract) · nro: built (0.14.044, sent over nxlink) · switch: verified on the console — the form opens after the protocol; WebDAV URL and username, FTP address and port typed from the PC page; FTP Test Connection passes. Pending: File Browser → + → Sources → Add network location → WebDAV: the form opens with Name "WebDAV"; Server URL → From Phone / PC → type `https://…` in the browser; Test Connection; B → the new source is in Mount.

## v0.14.043 — Saves: users with one nickname told apart, plain result texts, restore picker starts on the backup owner, same-second backups of two users no longer collide
- Found by driving the console with Scripted input (Saves → Deleted Games: backup of one game, of all 8, restore for two users; no crash on 0.14.042).
- `save_backup_pub.cpp`: the DBI file name has no user (`<tid>_A_<time>_<index>.zip`), so two users' saves of one game in the same second got one name and the second failed the batch with "path already exists"; a taken name now moves to the next free second. The "N backup(s) created, M already up to date." box is two lines, "Backups created: N" / "Already up to date: M".
- `save_game_group.cpp` `AccountIndexOf` / `AccountLabel`: both [[Restore for user]] pickers start on the backup owner (was always the first user); the [[ACCOUNTS]] rows in the options panel and the restore confirmation show "nin10do (B486)" when two users share a nickname.
- `popup_list.cpp`: the list title scrolls (ScrollingText, as the multi-select popup) instead of running off the screen ("Restore for user (game · owner · date)").
- Restore texts without "(s)", arrows and "not atomic": status line + "Safety copy/copies of the replaced save(s):" + "To undo: open File Browser, select recovery.zip, press + and choose Restore save data." (also in the file browser restore). New keys translated into all 26 languages (tools/i18n-translate, uk reviewed by hand); this also fills the 6 keys of 0.14.037–039 that were en/uk only.
- host tests: pass (quick, i18n contract) · nro: built (0.14.043, sent over nxlink) · switch: pending — Saves → Deleted Games → select all → backup: two-line summary; restore a game with two users: picker title scrolls, cursor on the owner, confirmation shows "(B486)" / "(9BBD)", result text has no "(s)".

## v0.14.042 — Scripted input: `shot` saves the screen as JPEG; hub-input.ps1 keeps every bare word as a command
- New command `shot` (`demo_cmd.hpp`, `demo_input.cpp`): caps:sc `capsscCaptureJpegScreenShot` (layer stack Default, 1 s timeout) writes `/config/kefir/demo/shot.jpg`; `hub-input.ps1 ... shot -Shot out.jpg` copies it to the PC, so an agent sees dialogs and the cursor, not only the screen stack.
- `hub-input.ps1`: `PositionalBinding = $false` — PowerShell bound bare words to `-Device` / `-Storage`, so `-Ftp <ip> R "wait 1" dump` sent only `dump`.
- Verified on the console with 0.14.041 over FTP: `dump` prints "menu Apps", then R → "menu Tools"; no crash. host tests: pass (quick, test_demo_cmd covers `shot`) · nro: not built (demo_input.cpp passes -fsyntax-only) · switch: pending — `hub-input.ps1 -Ftp 192.168.50.69 shot -Shot s.jpg` gives the current screen.

## v0.14.041 — Scripted input: `dump` crashed the Hub on the main screen; hub-input.ps1 works over FTP
- `demo_input.cpp` DumpState: `MainMenu` answers `IsMenu()` without being a `MenuBase`, so the `static_cast` there called `GetShortTitle()` through a foreign vtable and the Hub died on the first `dump` on hardware (0.14.040, main screen open). Now every widget is asked for its chrome owner (MenuBase: itself; MainMenu: the current tab; others: none).
- `tools/dev/hub-input.ps1 -Ftp <ip[:port]>`: pushes input.txt and pulls state.txt / log.txt through the Hub FTP server (curl, port 5000) when MTP is not available; `--ftp-create-dirs` makes `/config/kefir/demo` on first use.
- host tests: pass (quick) · nro: built (ReleaseWithInstall clean, 0.14.041) · switch: pending — Scripted input on, main screen: `hub-input.ps1 -Ftp 192.168.50.69 dump -State` prints "menu Homebrew" (or the open tab) and the Hub keeps running.

## v0.14.040 — storage bars: the focused game's size no longer hides the free space
- Header storage rows (Games and every menu that highlights a title): the value is "size / free" ("6.2 GB / 41.3 GB") instead of the size alone; the value column is sized for it, the network text next to NAND moves left as before. Free-only, "+focus / total / free" (install queue projection) and "written / total" (install progress) are unchanged.
- host tests: not needed (draw only) · nro: built (ReleaseWithInstall clean, 0.14.040, sent to the console over nxlink) · switch: pending — Games: cursor on a game on microSD: the microSD row reads "<size> / <free>", NAND row keeps free only; select several (multi-select): the sum / free.

## v0.14.039 — "Scripted input" debug setting: drive Kefir Hub from a file over MTP
- `demo_input.cpp` (the DocsDemo button queue read from `/config/kefir/demo/input.txt`) is in every build; `App::PollInput` runs it when the new Tools → Settings → [[Scripted input]] (next to [[Logging]], default off) is on, always in DocsDemo. New command `dump`: writes the open widgets bottom to top to `/config/kefir/demo/state.txt` ("menu <short title>", "modal", "widget"). `tools/dev/hub-input.ps1 Down A "wait 1" dump -State -Log` pushes the lines over MTP (Shell COM) and prints state.txt / log.txt, so an agent can press buttons and read the result without the user.
- host tests: pass (quick, test_demo_cmd covers `dump`) · nro: built (ReleaseWithInstall clean, 0.14.039 checkpoint) · switch: pending — Scripted input on, MTP running: `tools\dev\hub-input.ps1 Down Down A dump -State` moves the cursor and prints the menu stack; with the setting off the file is ignored.

## v0.14.038 — MTP: a marker file from the PC offers to switch to PC Install (USB)
- MTP (057e:201d) and the USB install link (057e:3000) are different devices, so a PC install app started after the cable was in cannot reach the console over USB. The signal is an MTP write instead: a file named `kefir-hub.pc-install` created on any MTP storage (haze CreateFile callback) makes Kefir Hub ask "A PC install app is waiting. Stop MTP and open PC Install (USB)?" — yes stops MTP, runs the usual install probe (PC Install opens when the host answers; MTP comes back when nobody does); the marker on the memory card is deleted either way. DBI Backend Qt has to write that file (WPD/MTP) when it finds the console in MTP mode — backend change pending in its own repo; from Explorer, copying any file with that name to the microSD storage does the same.
- host tests: not needed · nro: built (ReleaseWithInstall clean, 0.14.039 checkpoint) · switch: pending — cable in, MTP running, backend server running: copy `kefir-hub.pc-install` to the microSD storage root in Explorer → the box; PC Install (USB) → the queue appears, the marker is gone from the card; No → MTP stays.

## v0.14.037 — after installs: ask about updates and DLC whose base game is not installed
- New `orphan_content` (`NoteInstalled` from yati's record push for every update/DLC, `Poll` from the App loop): 5 s after the last install with no progress box open, the noted titles without an Application content meta are listed in one box — [[Keep]] / [[Delete]] (default Keep; delete = `DeleteApplicationKeepSave`, saves stay). Covers every install path (file browser, queue, USB PC Install, MTP, FTP, web) since all register through yati; a base game arriving later in the same batch clears its update.
- host tests: pass (quick) · nro: built (ReleaseWithInstall clean, 0.14.039 checkpoint) · switch: pending — install only an update by MTP: 5 s after it, the box names the game; Delete removes it from Games; install base + update together: no box; update of a game-card game: box appears, Keep.

## v0.14.036 — Games: sort by size
- Games → Sort By → Sort: new [[Size]] (base + updates + DLC on SD, NAND and game card; Descending = biggest first, ties by name). Storage and Size sorts now read the sizes of all games before sorting (they were loaded one row per frame while drawing, so rows not drawn yet sorted as empty).
- host tests: pass (quick) · nro: built (ReleaseWithInstall clean, 0.14.039 checkpoint) · switch: pending — Games → Sort By → Sort → Size: biggest game first, Ascending reverses; Storage sort right at once after start.

## v0.14.035 — USB queue: Retry from the PC during an install
- A package the install already went past without installing it (failed, skipped, unticked) is queued once more when the PC unticks it and ticks it again (DBI Backend Qt "Retry" does that; it unticks failed packages itself). `QueueEntry::retry_armed` is set when the live queue shows it unticked, so a failed package still ticked on the PC is not retried in a loop. Log line "Queued again: <name>" (en, engb, uk).
- host tests: pass (quick) · nro: built (ReleaseWithInstall clean, 0.14.035) · switch: pending — let a package fail mid-queue, Retry it on the PC: it installs after the current one; a failed one left alone is not retried.

## v0.14.034 — USB queue plan tells the PC which updates and DLC have no base game
- Queue plan flag bit3 (`QueuePlanFlag_NoBase`): an update (…800) or DLC (base + 0x1000 + n) whose base game is in neither NCM database (`IsBaseMissing`, `BaseTitleId` from the file name's title id). DBI Backend Qt lists these, plus per-package results and failure codes, in its end-of-session report.
- host tests: pass (quick, test_install_plan covers BaseTitleId) · nro: built (ReleaseWithInstall clean, 0.14.034) · switch: pending — queue an update whose game is not installed: the PC report lists it under "Without base game"; with the base in the same queue it is not listed.

## v0.14.033 — cheats: no prod.keys → offer a key dump everywhere the Build ID needs them
- When the Build ID cannot be read without keys (game not running, main NSO unreadable) and `/switch/prod.keys` is missing, nx-cheats-db download, manual import and Fix BID now show the existing "prod.keys not found" dialog that offers to start Lockpick_RCM (before: only CheatSlips; nx-cheats-db silently guessed the Build ID from the online version map, the other two said "Could not determine ... Build ID"). `ShowProdKeysMissingDialog` moved out of the anonymous namespace (`cheats_ops.hpp`).
- host tests: pass (quick) · nro: built (ReleaseWithInstall clean, 0.14.033) · switch: pending — rename /switch/prod.keys, game not running: Cheats → nx-cheats-db → a game offers the Lockpick dump; put the file back.

## v0.14.032 — cheats: Build ID byte order; cheat files Atmosphere accepts
- Build ID from the running game (dmnt) and from the main NSO (no prod.keys) was byte-reversed, so cheat files got a wrong name and Atmosphere never loaded them; now read in order like the NCA path. The reversed-ID retries in the nx-cheats-db/KefirUpdater fetch are gone; the Build ID cache is v2 (old one dropped) and is used only for the same game version.
- New `cheat_text.hpp` (`SanitizeCheatText`, host test `test_cheat_text`): dmnt drops the whole file for a second master code, more than 127 cheats, more than 256 words in one cheat, code before a header or brackets in a name. Download rewrites the whole file (old + new cheats) through it, manual import too; skipped cheats are reported.
- host tests: pass (quick) · nro: built (ReleaseWithInstall clean, 0.14.032) · switch: pending — download cheats for a game without prod.keys and with the game running: file name = Build ID shown by EdiZon/Breeze, cheats active in game; add cheats twice to one file: still loads.

## v0.14.031 — PC Install on connect also when the cable was in before start; "already installed" really detected; skip mode from DBI Backend Qt
- Start-up with the PC cable already in and MTP on: MTP no longer grabs the port at once when "PC Install on connect" is on, so the first USB poll runs the install probe (MTP starts after it when no app answers). Before, only a plug-in after start opened PC Install.
- Queue "already installed" took the title id from the cnmt nca's name, which is its content id, so it never matched and installed games were booked as needing space ("may not fit"). Now `ParseNameTitle` reads `[16 hex]` and `[vN]` from the file name and NCM is asked for that id at that version or newer (`IsEntryAlreadyInstalled`). Installed rows book no space only when the skip mode is not Reinstall (`Menu::TakesNoSpace`, `SkipMode`). DBI Backend Qt can set the skip mode: field 3 of the SPHQ revision line (0 = console setting, 1..3 = Reinstall/Skip/Prompt; older backends send 0); queue plan flag bit2 = takes no space.
- host tests: pass (quick, test_install_plan covers the name parser) · nro: built (ReleaseWithInstall clean, 0.14.031) · switch: pending — start Kefir Hub with the PC cable in (MTP on, backend running): PC Install opens; queue with installed games and Skip: no "may not fit", backend unticks them once; Reinstall on the PC: they count again.

## v0.14.030 — Tools: left/right jump between captions; System information: page jump and L/R groups
- Tools → Tools: left/right move the cursor to the first item of the next / previous caption (Diagnostics → Settings → Maintenance, wrapping). System information: left/right page through the rows like the file browser (list page jump on); L / R move to the previous / next group and open it (footer: "Previous group", "Next group").
- host tests: not needed · nro: built (ReleaseWithInstall clean, 0.14.030) · switch: pending — Tools: right from System information lands on Module Manager, again on Clean system junk; System information: right pages down, R opens Atmosphere, L back to Console.

## v0.14.029 — Tools list: left/right step through items; System information without the side bar
- Tools → Tools: D-pad left/right move the cursor one item like up/down (a one-column list without page jump left them unused). System information: the accent bar on the left of an open group and its rows is gone; the open group is still told apart by the filled band, the accent-coloured title and the indented rows.
- host tests: not needed · nro: built (ReleaseWithInstall and DocsDemo clean, 0.14.029) · docs: 4 shots retaken · switch: pending — left/right in Tools → Tools; open a group in System information.

## v0.14.028 — fix the Release (no network install) build; CI builds both presets
- haze_helper.cpp: the MTP close-session hook that finishes a dbi install session is now under `#if ENABLE_NETWORK_INSTALL`, like the file's other dbi blocks; the Release preset had not compiled since v0.13.960. CI matrix: ReleaseWithInstall only (green on branch ci-libnx); Release still fails to link (18 dbi InstallSession / yati Usb symbols from usb transfer and MTP code) and is not maintained. host tests: not run (guard only), nro: CI built, switch: not needed.

## v0.14.028 — Tools list: the last row is no longer cut
- The compact Tools → Tools list has 11 rows (the SD zero-fill is back) and its area was still 462 px, so the last row and its cursor frame were clipped. The list area is now rows × 44 px + 8 px for the outline.
- host tests: not needed (layout constant) · nro: not built · switch: pending — cursor on the last row: text and frame whole.

## v0.14.027 — Game transfer by USB cable; System information and Tools looks
- Console Transfer → Send installed games now ends in two buttons: "Send over Wi-Fi" (as before) and "Send by USB cable". Cable = the existing Dump → "USB transfer (Switch 2 Switch)" path: this console is the USB host (usb:hs, `usb::upload::Usb`, tinfoil protocol) and offers one NSP per installed component of every chosen game (`title::BuildNspEntries`, `game::NspSource`), with the STREAM flag forced off (`DumpLocation::usb_stream`) because the Hub's PC Install (USB) refuses stream hosts. Receive games first asks "How is the other console connected?": Wi-Fi (the v0.14.020 flow) or USB cable, which opens PC Install (USB); the list arrives from the sender by itself. Correction: the v0.14.020 note that a cable was impossible was wrong, sphaira's uploader has run the console as USB host for Switch 2 Switch dumps all along; plan F.13 fixed.
- System information: groups are filled bands with a left accent bar and accent-coloured title while open; parameter names bold (over-drawn), values in the accent colour, rows indented under the open group. Tools → Tools: compact 44 px rows, all captions and items on one screen, name left and description right.
- host tests: pass (quick) · nro: built (ReleaseWithInstall and DocsDemo clean, 0.14.027) · docs: 6 shots retaken (Eden) · switch: pending — two consoles on a USB-C cable: A → Send by USB cable, B → Receive games → USB cable: B shows the list, installs base + update + DLC; retry with the cable the other way round if the roles do not negotiate; Wi-Fi path unchanged.

## v0.14.026 — USB queue: console changes reach the PC; sort survives the live queue; Target and Status keys
- New DBI command QueuePlan (id 6, console → PC, DBI Backend Qt only): count + PC revision, then per package selected / target / planned drive / flags (analysis ok, already installed) / install size / name. The usb thread sends it before every 300 ms live-queue poll when the plan digest changed or a row was changed on the console (X, Y, L3 set `sync_dirty`; ApplyLiveQueue leaves such rows alone until the plan went out), so a tick removed on the console no longer returns from the PC. The PC applies selection and targets only when the plan's revision equals its own, shows "Auto → SD/NAND" per Auto row, and draws the Hub's storage projection (selected packages into free space, hover = focus) — DBI Backend Qt working tree, uncommitted there. SortQueue is re-applied after every ApplyLiveQueue in ReviewQueue; sort keys Target (microSD first) and Status (ticked, unticked, already installed, analysis failed) added. v0.14.025 build checkpoint: clean.
- host tests: pass (quick) · nro: not built · switch: pending — with DBI Backend Qt from E:\Switch\dbibackend-qt: untick on the console stays unticked on the PC; untick on the PC reaches the console; L3 target change shows on the PC; "Auto → SD" on the PC matches the console row; PC bars: equal length, projection before install, hover highlights the row's package; sort by Target/Status holds while the PC re-sends the list.

## v0.14.025 — First start: a full page in the console's language, then the language list
- Startup with no language in config.ini loads the console's matched language (fallback English) instead of English, so the first-start page, the list and the main menu built underneath are already readable. A new `ui::FirstStart` widget (modal, blocks drawing underneath, so the menu is not rendered) replaces the bare language popup: Kefir Hub title and version, "First start", two paragraphs (start Kefir Hub from its HOME Menu icon: all memory and the USB port; choose a language, a different one restarts once), A = Choose language opens the usual list (title "Language", no cancel). A language equal to the page's own: no restart, the page pops. A different one: a dialog says Kefir Hub restarts now and to start it from the HOME Menu icon if it does not come back; OK restarts. Docs drafts EN/UK (index, getting-started), video 01 scene 8, DocsDemo startup steps are now A, A (open list, pick the highlighted language).
- host tests: pass (quick) · nro: not built · switch: pending — fresh config on a Ukrainian console: page in Ukrainian, pick Ukrainian: no restart; pick English: dialog then restart; the menu is never visible under the page.

## v0.14.024 — USB probe: three detection rounds (DBI Backend Qt auto-starts its server)
- DBI Backend Qt (E:\Switch\dbibackend-qt, commit d4ca08d) now starts its USB server by itself when a Switch in install mode appears, on a 2 s poll plus a device reset and a 1 s settle; the Hub probe runs three detection rounds instead of two so that start is always inside the window. Build checkpoint: v0.14.023 sources built clean (ReleaseWithInstall, no errors, no warnings in first-party code).
- host tests: not run (constant change) · nro: built (v0.14.023 + this constant, not rebuilt) · switch: pending — see v0.14.023.

## v0.14.023 — PC Install opens by itself when a USB install app answers; indeterminate bar glides off the edge
- Cable plug-in (handheld, 2 s after the PC charger appears, where auto MTP used to start): with the new Settings → Network → "PC Install on connect" (default on) Kefir Hub opens usb:ds as the install device in a thread (`usb_install_probe.cpp`), waits up to 3 s for enumeration and runs two detection rounds of the install menu's own protocol probe (DBI write probe, Tinfoil listen, Goldleaf). A host that answers hands its open link and file list to a new PC Install (USB) constructor, so the menu starts in Analysing with no second list request (Awoo's one-shot list is not lost). No answer: the link is closed and MTP starts as before. Hub cannot start the app on the PC side.
- ProgressBox indeterminate bar (unknown total: firmware prepare step, stream installs): the segment now travels from fully outside the left edge to fully past the right edge, clipped to the bar, instead of vanishing when it touches the right end; period 2 s.
- host tests: pass (quick) · nro: not built · switch: pending — DBI Backend running + plug cable: PC Install opens with the list; backend not running: MTP starts after the probe (~5 s); ns-usbloader (Awoo) upload started before plugging: list arrives; setting off: MTP at once; firmware install prepare step bar glides.

## v0.14.022 — SD zero-fill back; game transfer only in Console Transfer
- User correction (2026-10-07): only the NAND zero-fill was to go. "Fill free SD space with zeros" is back (zero_fill.cpp restored, SD only) under the Maintenance caption; its 7 i18n keys restored in every language. The "Send to another console" entry in the installed game's + menu is removed: every console-to-console option lives in Tools → Console Transfer for now (other entry points to be decided later). Docs drafts, coverage rows and the games page updated.
- host tests: pass (quick) · nro: not built (1 unbuilt commit) · switch: pending — Tools → Tools → Maintenance → Fill free SD space with zeros runs and cancels; the game + menu has no transfer entry.

## v0.14.021 — Build checkpoint for v0.14.018–020; translations and docs drafts
- System information labels go through `"..."_i18n` so the i18n sync sees them; 93 new strings translated into every bundled language (tools/i18n-translate). Docs drafts (EN+UK) for the grouped Tools list, the System information screen, Move installed games and Send to another console; zero-fill sections removed; DOCS-COVERAGE rows; 12 shot recipes shifted for the new caption rows and the two new Console Transfer items.
- host tests: pass (full, incl. i18n parity and doc labels: 753 used, 0 unknown) · docs: built en/uk, 0 warnings · nro: built (ReleaseWithInstall, v0.14.020 sources + this label change, no errors) · switch: pending — see v0.14.018–020.

## v0.14.020 — Game transfer between two consoles over Wi-Fi
- Console Transfer → "Send installed games": tick games (X = all), Start sharing; the web server offers them at /games as streamed NSPs (base, every update, every DLC — the MTP Games builder, nothing written to the SD) with Range support, one ncm read at a time; the server box shows "Sending: <file>". Installed game → + → "Send to another console" shares that one game at once. Console Transfer → "Receive games": enter the sending console's address (same probe as the user-backup receive), all offered games ticked, Install: each file is installed over HTTP with yati (the Ownfoil path). Needs Install enabled. Game shares end with the server.
- Cable: not shipped. MTP (libhaze) and the USB install protocols (DBI/Tinfoil/Goldleaf) both make the console the USB device; a second console would have to be the USB host (usb:hs), which Kefir Hub does not implement, and two Switches negotiate USB-C roles unpredictably. Decision recorded in plan F.13.
- host tests: pass (quick) · nro: not built · switch: pending — two consoles on one Wi-Fi: send one game with update + DLC, receive on the other, launch it; cancel mid-install; receive with Install disabled shows the enable prompt; the sender's SD listing is also still reachable at / while sharing.

## v0.14.019 — System information as grouped tables
- Tools → Tools → System information is a screen, not a text file: seven groups (Console, Atmosphere, Storage, Power, Battery, Hardware, Play activity) open with A or a tap into parameter → value rows; Y opens or closes all. New readings: model, SoC, burnt fuses, kiosk, HiZ; Atmosphère supported firmware, emuMMC type, blank PRODINFO and CAL0-write flags; microSD CID (maker, OEM, product, revision, serial, date), speed mode, user/protected area, used/free for SD and NAND; USB charger type, power role, every psm limit; MAX17050 fuel gauge over i2c (design/full/remaining capacity, cycles, current, cell voltage, time to empty); setcal serial; installed games, total play time and launches, most played. Serial number: system settings → raw PRODINFO partition → a PRODINFO backup on the SD (Atmosphère automatic_backups, hekate /backup), the source is shown in its own row and a blank system value is shown as blank; nothing is computed. system-info.txt is no longer written.
- host tests: pass (quick; new tests/test_system_info_parse.cpp covers CID byte orders and serial validation) · nro: not built · switch: pending — check: CID maker matches the card label (byte-order heuristic), MAX17050 capacities plausible (Rsense 10 mOhm), burnt fuses equals the known count for the firmware, USB power role labels, serial source row on a console with blank_prodinfo.

## v0.14.018 — Tools → Tools grouped; zero-fill removed
- Tools → Tools is one list with three captions: Diagnostics (System information), Settings (Module Manager, Fan curve, Wi-Fi, Users), Maintenance (Clean system junk, Remove parental controls). Captions reuse the Settings header rows; the cursor steps over them. Both "Fill free … space with zeros" items are gone (zero_fill.cpp deleted, 10 i18n keys dropped in every language).
- host tests: not run · nro: not built · switch: pending — open Tools → Tools, step through with the D-pad and tap a caption (nothing happens).

## v0.14.006 — Unfinished TegraExplorer dump: Later / Cancel operation / Retry
- Rename the unfinished-dump prompt buttons to Later (B, silent until Hub restarts) / Cancel operation (clears state, staging, scripts, reminders) / Retry; default cursor on Later, so no button reboots by accident. Retry re-stages the target folder, flags and pending state before rebooting and reports staging failures instead of launching. A dump without dumped.ok is never treated as complete. The TE dump script reports read errors as errors, not as an empty save. EN/UK docs and video 07 updated. Embedded TegraExplorer 4.2.28 fixes the black OLED screen (LTO inlined hw_init across pivot_stack).
- host tests: pass (quick) · docs: built en/uk, 0 unknown labels · nro: not built · switch: pending — interrupt a dump, delete its folder, start Hub: B exits, Later stays silent this run, Cancel operation clears after restart, Retry re-creates the folder; TE 4.2.28 on OLED.

## v0.14.007 — Mark one-shot TegraExplorer scripts as Hub-owned
- Add a `# kefir-hub-owned` line to the four scripts Hub writes to /startup.te (0010 dump, apply link, NAND dump, NAND restore). Kefir install.bat and kefir-updater update.te now remove leftovers of an unfinished dump/restore: restore_pending, _staging_/_restore_ folders, reopen flag/notify, the one-shot script copies and an owned /startup.te; completed packs, .zip backups, restore_backup and Undo_restore_if_wont_boot.te are kept (Kefir repo commit).
- host tests: pass (quick) · nro: not built · switch: pending — leave an interrupted dump, reinstall via install.bat and via update.te, confirm Hub shows no old prompt and completed backups remain.

## v0.14.008 — Restore Device and BCAT saves without the installed game
- Save slot creation now covers Account, Device and BCAT primary slots. Sizes and owner come from the installed game's control data (Device: nacp device sizes; BCAT: delivery cache size, 2 MiB journal, bcat owner) or, when the game is absent, from the backup's save metadata (Kefir Hub / DBI zips), exactly as the Account path already did. Device/BCAT slots are created without a thumbnail meta and with a zero uid. The old 'launch the game first' refusal is gone; JKSV/Checkpoint folders without metadata still need the installed game. EN/UK docs, video 04 scenes 17-18 updated.
- host tests: pass (quick; bundle rules updated) · docs: built en/uk, 0 unknown labels · nro: not built · switch: pending — restore Device and BCAT from a Kefir Hub zip with the game uninstalled, install the game and check it picks the data; re-restore; mixed Account/Device/BCAT bundle; compare with DBI; corrupt metadata must be refused.

## v0.14.009 — Apply the first-start language everywhere
- The main menu is built in English before the first language dialog, so its tiles kept English labels. After the first choice of a non-English language Kefir Hub now restarts itself once (config already saved); the same restart path as the Settings language change. EN/UK docs and video 01 scene 8 updated.
- host tests: not needed (UI-only) · docs: built en/uk · nro: not built · switch: pending — fresh config, pick Ukrainian: whole main screen in Ukrainian after the automatic restart; second start keeps it; pick English: no restart.

## v0.14.010 — Screen off one minute after the install queue finishes
- The DBI install screen now arms the existing inactivity tracker in the Summary state with a fixed 60 s timeout: the finished queue blanks the screen in the configured Minus-button mode, any input wakes it and restarts the clock, a new install replaces it with the install-time rule. The clock restarts at the transition, so a long untouched install does not blank immediately. Tracker now takes a phase (none/installing/finished); host test extended. EN/UK docs updated.
- host tests: pass (quick, screensaver_timeout) · docs: built en/uk, 0 unknown labels · nro: not built (v0.14.009 checkpoint built clean: build/ReleaseWithInstall/kefir-hub.nro, 0.14.009) · switch: pending — finish a queue, wait 60 s untouched: screen off; press a button at 50 s: restarts; new install during countdown: no blank; wake after blank: summary still shown.

## v0.14.011 — Port small upstream sphaira 1.0.8 changes
- From NaGaa95/sphaira 1.0.7→1.0.8 review: forwarder editor gains 32-bit and 32-bit (no alias) address spaces (f7cd3782); Chinese UI languages use the Chinese shared font as the main face (e098979b); Ownfoil menu joins its connect worker in the destructor and the HTTP source retry wait is cancellable in 100 ms slices (76be2ac8; our cancel path already existed); evman::pop moves the event; dead ProgressCallbackFunc1 removed. Not ported: native Album menu (#377, overlaps our web /album share, owner decision), Docker builder, hard-coded language-name list (ours is data-driven), ru/zh translation refreshes, CMake dependency bumps. EN/UK docs for the forwarder editor updated.
- host tests: pass (quick) · docs: built en/uk, 0 unknown labels · nro: did not compile (see v0.14.012) · switch: pending — forwarder editor cycles 39→36→32→32 (no alias); zh UI font; Ownfoil B during a stalled download returns within ~0.1 s.

## v0.14.012 — build fix
- v0.14.010 redeclared `state` in `dbi_menu.cpp` Menu::Update (conflicting declaration at line 390); renamed the new local to `blank_state`. No behaviour change.
- host tests: pass (quick) · nro: built (ReleaseWithInstall checkpoint clean; rebuilt after the bump so the artifact reports 0.14.012) · switch: pending.

## v0.14.013 — 32-bit address spaces in the global forwarder default
- Settings → Forwarder → Address space offers Automatic / 36-bit / 39-bit / 32-bit / 32-bit (no alias) (sidebar and settings page); the default forwarder options map 3→32-bit, 4→32-bit (no alias). EN/UK settings docs updated.
- host tests: pass (quick) · docs: built en/uk, 0 unknown labels · nro: not built · switch: pending — pick each value, create a forwarder, check the editor shows the same space.

## v0.14.014 — Explain the forwarder address space
- One plain description in Settings (sidebar and page) and, new, in the forwarder editor under the icon column while the Address Space row is selected: leave Automatic (39-bit); 36-bit only if an old app does not start; 32-bit variants only when the app asks for it (Wine-NX, Box64). Automatic detection is not possible: an NRO carries no address-space requirement. Old description key removed from all languages.
- host tests: pass (quick) · docs: 0 unknown labels · nro: built (ReleaseWithInstall checkpoint clean, kefir-hub.nro 0.14.014) · switch: pending — select Address Space in the editor: hint appears under the icon; other rows: no hint.

## v0.14.015 — App Store: several sources, Recompiles filter, store generator
- The App Store downloads every source in `DefaultSources()` (fortheusers + the Kefir recompiles store) plus `/config/kefir/appstore_sources.txt` (one base URL per line, `#` comments) and shows them as one list; a source that fails keeps its last cached `repo_<n>.json`. Entries carry their source base URL and may name a direct `download` zip and `icon`. New category `recompile` → filter Recompiles (label in all languages). `tools/recompile-store/build_store.py` builds an hbstore-layout store from GitHub releases (manifest.install + info.json, incremental by tag); smoke-run on diasurgical/devilutionX 1.5.5. EN/UK docs: sources, filter, "Add your own store".
- host tests: pass (quick + i18n parity + doc labels + recompile store contract) · docs: built en/uk · nro: built (0.14.016 checkpoint) · switch: pending — open Software → Homebrew App Store with and without network, filter Recompiles, install one recompile from a published store. The default Kefir store URL (`raw.githubusercontent.com/rashevskyv/kefir-store/main`) is not published yet; until then that source fails silently.

## v0.14.016 — Cancel while connecting to a network location no longer hangs the Hub
- Cancel in the "Connecting to …" box joins the connect thread, and that thread had no deadline: a dead WebDAV/FTP/HTTP probe, SMB connect or NFS mount blocked the whole UI until the socket gave up. The curl probe now carries the box's stop token (aborts at once), SMB connects time out after 15 s and NFS mounts after 15 s.
- host tests: pass (quick) · nro: built (ReleaseWithInstall checkpoint, 0.14.016, copied to the test card) · switch: pending — add a location that is offline, open it, press B and confirm Cancel: the box closes at once (WebDAV/FTP/HTTP) or within 15 s (SMB/NFS); Hub stays usable. Root cause confirmed by the card log (13:30:33 HTTP probe of an offline 192.168.50.112:8080, Cancel → Yes, last line "popping widget", no fatal report).

## v0.14.017 — File browser: one-line notes for the Kefir files and folders
- The list layout shows what a known file or folder is for under its name (card root, atmosphere, bootloader, config, config/kefir, switch, overlays, themes, emuMMC): 120 notes in the pure table `filebrowser_path_notes.hpp` (`FindPathNote`, case-insensitive, host-tested). The old /config/kefir if-chain moved into the table with the same texts. A recognised payload still wins over the generic note for `.bin` files. `tools/i18n-translate/add_path_notes.py` copies the table into en.json (the strings are not `_i18n` literals); translate.py filled the other languages. Docs: file-browser.md EN/UK.
- host tests: pass (quick, 120 notes) · i18n parity: pass · docs: built en/uk, 0 unknown labels · nro: built (ReleaseWithInstall checkpoint, 0.14.017, copied to the test card) · switch: pending — open /, /atmosphere, /bootloader, /config, /config/kefir, /switch in list layout: notes under the names; /atmosphere/contents still shows module and game names.

## unreleased
- finish.py: a second, independent proxy request reviews each finished page (draft facts in, nothing invented, unrelated lines untouched) before it replaces the page; a rejection feeds the problems into the next attempt, three attempts. Live run on cheats.md EN+UK copies: both reviewed and clean. Product code unchanged.
- docs: new page "What is on the microSD card" (EN/UK): every Kefir folder and file on the card with its purpose, linked from Settings → Where settings are stored.
- Embed TegraExplorer 4.2.29: the Joy-Con connect rumble is muted with a neutral pattern before the disable subcommand, so third-party controllers stop buzzing at TE start (hekate 6.5.4 sequence). nro: built (ReleaseWithInstall checkpoint clean, build/ReleaseWithInstall/kefir-hub.nro 0.14.013 with TE 4.2.29) · switch: pending — start TE with a third-party controller attached.
- Repository folder renamed from `sphaira` to `kefir-hub` (D:\git\dev\kefir-hub, WSL /mnt/d/git/dev/kefir-hub); AGENTS.md, test-build and update-docs skills updated; a junction `D:\git\dev\sphaira` points to the new folder for old tooling. TegraExplorer Makefile/AGENTS.md follow. Product code unchanged.
- Add the Codex Graphify session hook; invoke Git Bash explicitly on Windows. JSON parsed; shell syntax and configured command: pass. Product code unchanged.

## v0.14.005 — Ask before deleting manually installed firmware folders
- After successful manual folder installation, offer Keep (default/B) or Delete for the exact validated source folder, including /firmware. Preserve source on failure/cancel; report deletion errors separately from install success. Update translations, EN/UK docs, video 06 and guide.
- host tests: pass (full + final quick, 80 cleanup checks); Switch C++ syntax checks: pass (4 modules, -Werror); docs EN/UK built, 0 unknown labels, 0 broken guide links · nro: built (ReleaseWithInstall) · switch: pending — Keep/B/Delete, manual /firmware, sibling preservation, install/deletion errors, ZIP/network regression; [USER] updater-firmware-folder-delete screenshot and scene 21 recording.

## v0.14.004 — Consistent TegraExplorer version selection
- Route 8GB DRAM and selected TegraExplorer payload launches through the shared version check: install missing TE, upgrade older SD copies, preserve equal/newer SD versions. Verify staged writes and retain the old payload during replacement; update EN/UK docs and video preparation notes.
- host tests: pass (full, 67 TE checks); Switch C++ syntax checks: pass (3 modules, -Werror) · nro: not built · switch: pending — missing/older/equal/newer TE, selected paths, write failures, profile transfer and downgrade; test 8GB only on modified hardware.

## v0.14.003 — Abandon unfinished TegraExplorer operations
- Add Cancel / Don't remind again / Retry to unfinished profile dump prompts; abandonment clears guarded staging, pending state, scripts and reopen notifications, preserving completed and safety backups. Update translations, EN/UK docs and video 07; [USER] users-unfinished-dump screenshot and scene 19 recording pending.
- host tests: pass (full + final quick); Switch C++ syntax checks: pass (3 changed modules, -Werror) · nro: not built · switch: pending — test Cancel, retry, abandonment across Hub restart, fresh operation and unrelated startup preservation.

## v0.14.002 — Short save-type badges
- Use Acc, Device, BCAT, Cache, Temp, Sys and SysBCAT as fixed badge labels; keep existing colors and type aggregation. Update EN/UK docs and video subtitles.
- host tests: pass (full; final quick pass) · nro: not built (user builds) · switch: pending — check shortened labels in all save layouts.

## v0.14.001 — Save-type badges for complete game bundles
- Show distinct fixed-English Account, Device, BCAT and Cache badges from actual live slots or source-isolated backup children; preserve type coverage across representative replacement and omit badges for games without saves. Update EN/UK docs, video script and guide.
- host tests: pass (full, before final fixed-label adjustment) · nro: not built for final changes (user builds) · switch: pending — badge readability in List, Grid, GridDetail and HbMenu; [USER] retake saves-list screenshots and scene 3.

## v0.13.1000 — Complete game save bundles and source-isolated restore
- Show games with Account, Device, BCAT or Cache saves; back up complete game bundles. Keep restore histories and unchanged-backup checks within the selected creator; preserve known save spaces and legacy unknown-space archive identity before mutation. EN/UK docs, guide and video scripts updated.
- host tests: pass (full; final quick pass) · nro: built · switch: pending — Animal Crossing Device+BCAT, multi-user bundles, USB/source switching, legacy restore and recovery copies; [USER] retake changed save screenshots and video scenes.

## v0.13.999 — Saves: backup skips unchanged saves; archived games count as deleted; sort by name/size
- Create backup / Backup now skips a save whose newest backup in the chosen location has the same commit id and timestamp (no more identical copies); the separate "Create backup if newer" action is gone (`IsBackupUpToDate`). Message: "N backup(s) created, M already up to date." Docs en/uk updated.
- Animal Crossing was still not in Deleted Games: archived games keep their record and content list with storage None, so `BuildInstalledAppIds` called them installed. Installed now means a base program (Application meta) on SD, NAND or game card.
- Sort: Updated / Alphabetical / Size (all saves of the game together, `Entry::sort_size`); live saves and each backup section sort on their own. v0.13.998 (backup-skip only) was built, never deployed.
- host tests: pass (quick) · nro: built · switch: pending — Animal Crossing in Deleted Games; backing up an unchanged save says "already up to date"; Sort → Size/Alphabetical

## v0.13.997 — Saves: Deleted Games lists games with only Device/BCAT saves
- Animal Crossing (only Device + BCAT saves) was missing from Deleted Games, so a Hub backup of all deleted games skipped it: the type filter defaults to Account. Deleted games with a save of any non-system type now get a tile (one `DiscoverSaveDataInfo()` pass). Found by comparing 129 Hub/DBI backups with the MTP Saves drive: all identical, Hub covered 55/57 saves, DBI 57/57.
- host tests: not run (UI-only) · nro: built · switch: pending — Animal Crossing in Deleted Games, backup writes Device + BCAT

## v0.13.996 — Saves: backup scan drawn in place of the grid; Deleted Games opens no archives
- The v0.13.994 progress box covered the whole screen. The library scan now runs on a menu-owned thread (`BackupScanJob`, `PollBackupScan`) and the Backups tab draws "Reading backups", a bar, "N / total" and the current path where the grid goes; other tabs stay usable meanwhile, leaving the menu cancels it.
- Deleted Games no longer reads the whole library: it needs only names, now taken from backup folder names (`ReadBackupNames`: game folder + title id from the first archive name, no archive opened).
- Backups grid: row gap 34 -> 60 px, section label centred in it (DBI label no longer touches the Kefir Hub row above).
- host tests: not run (UI-only) · nro: built · switch: pending

## v0.13.995 — Saves: YouTube tile shows even with only a Cache save
- v0.13.994 still hid YouTube: the list's type filter defaults to Account and YouTube has only a Cache save. Installed 0x05… apps now get a tile when they have a save of any type (one extra `DiscoverSaveDataInfo()` pass, only when such apps exist).
- host tests: not run (UI-only) · nro: built · switch: pending — YouTube in Installed Games

## v0.13.994 — Saves: YouTube listed; backup library read under a progress box
- Save menu skipped every 0x05… app as a forwarder, so YouTube (05003A400C3DA000 on this console) and its Cache save never showed and never got backed up. 0x05… apps now count as installed and show when they have a save (`BuildInstalledAppIds`).
- Opening Deleted Games / Backups froze the UI while every backup archive was opened (hundreds of DBI zips). The tab now switches at once; the library is read in a progress box with "N / total  <path>" and Cancel (`StartBackupScan`, two-pass `ReadBackupEntries`). New key "Reading backups" in all languages.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — YouTube in Installed; Deleted Games shows the progress box, no freeze

## v0.13.993 — the previous session's log is kept as log.prev.txt
- `log_file_init` renames `log.txt` to `log.prev.txt` before starting a new one (`log.cpp`). A crash or an odd exit used to leave no trace: the next launch truncated the only log (2026-10-04: a reported crash had no log, and Atmosphère wrote no crash report since 2026-09-30).
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — relaunch twice, `/config/kefir/log.prev.txt` holds the earlier session

## v0.13.992 — NSP file names keep Cyrillic and other non-Latin letters
- `ResolveExportTitleName` used an ASCII-only sanitizer, so "Mario + Rabbids Битва за королевство" became "Mario + Rabbids _ _ _" in NSP names (MTP Games drive, SD dumps) while folder names kept it. It now uses the UTF-8 sanitizer the folders use (only `\ / : * ? " < > |` and control codes become `_`), trims spaces, and truncates on a character boundary; `SanitizeAsciiTitleName` removed. Host test updated (Cyrillic, Japanese, boundary cut).
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: verified over MTP — "Mario + Rabbids Битва за королевство [010067300059A000][B+U589824+4DLC].nsp", "Pokémon Legends_ Arceus …"

## v0.13.991 — MTP Games drive reads tickets again; MTP on by default next to USB storage
- The real cause behind v0.13.990's report (console log): every es call from the MTP drive failed with 0xE401 (invalid handle) — es was only opened by the Games menu, so with MTP alone no ticket could be read and only key-area content (no rights id) was dumped. `BuildNspEntries` / `BuildMergedNspEntry` now open es themselves. `mtp_enabled` defaults to on; the startup rule that switched MTP off when USB storage was also on is gone — the port already goes to what is plugged in (`haze::Init` drops the host stack, `haze::Exit` restores it). Docs: settings.md.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: verified over MTP (nxlink to the console) — Merged lists all 18 games with updates and DLC (e.g. Cuphead [B+U655360+1DLC]); Separate shows base + update + DLC, Cuphead opens at once. Open: NSP file names replace Cyrillic with `_` (folder names keep it).

## v0.13.990 — MTP Games drive lists every game; Games and Saves drives on by default
- User report (console, 2026-10-04): Merged showed 3 of 18 games, Separate folders empty (Cuphead, Mario Galaxy 2) or update only (Mario Wonder). Cause: an NSP needs each content's ticket and Hub read only common tickets, so games with a personalized ticket (eShop, DBI installs) failed, and one failed component dropped the whole game. Now `title_nsp.cpp` falls back to the personalized ticket (new es 15/17, converted to common by `PatchTicket`) with the common certificate chain from any common ticket (`es::GetAnyCommonCertificate`); Separate skips only the broken component, Merged keeps every component that builds and is named after what it holds. `mtp_show_games` / `mtp_show_saves` default to on.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — Merged lists all games, Cuphead / Galaxy 2 / Wonder show base + update + DLC, copying one works; Cuphead no longer hangs

## v0.13.989 — game restrictions switched at install too (plan F.11, part 2)
- Settings → Install → "Game restrictions (need sigpatches)": start without linked account, allow screenshots, allow video capture (off by default, stored in config.ini `[install]` by `control_patch`, not in App). After an install, each installed game/update gets `control_patch::PatchInstalled` (both install paths in `yati.cpp`); a failure is logged, the install still counts. Docs: settings.md, games.md anchor.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — turn "start without linked account" on, install a game that needs one, launch without a linked account

## v0.13.988 — game restrictions on an installed game: linked account, screenshots, video (plan F.11, part 1)
- Game details → **+** → "Restrictions": three switches read from the control data; a change patches every installed Control NCA of the game (base + update, SD + NAND): decrypt the RomFS section, patch control.nacp (`startup_user_account`, network license bit, `screenshot`, `video_capture`), rehash IVFC after verifying the stored hashes, update the fs header hash, encrypt, write a placeholder and re-register under the same content id, invalidate the ns control cache (`control_patch.cpp`, `nacp_patch.hpp` + host test with a synthetic IVFC tree and RomFS). New `ncm::ListAllKeys`, reused by the cleanup. Needs sigpatches.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — toggle each switch on an eShop game and a cartridge-dumped game, with and without an update, launch, screenshot/record; a refused layout must leave the game untouched

## v0.13.987 — Tools → System information (plan F.14, after DBI 905)
- Report in sections: firmware (version, name, hash, hardware, retail, DRAM id, device id, serial, nickname, language, region, parental controls), Atmosphère (version, target firmware, key generation, git commit, RCM patched, emuMMC, USB 3.0), battery and power (charge, raw, health, charger, charging, temperature, voltages and current limits on 17.0.0+), hardware (BT/Wi-Fi MAC, configuration id, battery lot). Saved to `/config/kefir/system-info.txt` and opened in the text viewer (`system_info.cpp`). Not yet: SD card CID, burnt fuses, MAX17050 registers, play activity.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — open it, check values against DBI's screen

## v0.13.986 — JKSV / Checkpoint folders restore for a new user when the game is installed (plan F.15)
- A save folder without metadata no longer stops with "Backup folder metadata is missing…": it goes through `PlanAccountSaveCreation` like a ZIP without metadata, sized from the installed game's NACP, or "… is not installed. Install the game…" when it is not (`save_restore_route.cpp`). Docs: saves.md.
- host tests: pass (full) · nro: built (ReleaseWithInstall) · switch: pending — restore a JKSV folder for a user without a save of an installed game

## v0.13.985 — Tools → Clean system junk works (plan F.4, after DBI 905)
- One list of switches, then "Run selected" (`system_cleanup.cpp`): old game updates (older than the newest of the same game, SD+NAND, ns record re-pushed), lost content on SD / NAND (content ids no registered meta uses, meta ncas kept), unfinished installs (CleanupAllPlaceHolder), unused tickets (common + personalized whose rights id no installed content uses; new es Delete/Count/ListCommon), `/atmosphere/erpt_reports`, `/atmosphere/contents` folders of games without an app record (sysmodules never match), saves of removed users (off by default). Notification shows items removed and space freed. Not taken: downloaded system update, ticket cache, "fix tickets" (no reliable API yet). `system_cleanup_plan.hpp` + host test.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — run with all on, check games still launch (eShop + pirated + cartridge with update), freed space shown

## v0.13.984 — force a game's language; translation packs set it on install (plan F.8)
- Game details → **+** → "Force language" (only the game's languages + Off) writes Atmosphère `[override_config] override_language` into `/atmosphere/contents/<tid>/config.ini` (other keys kept, minIni); the Languages stat shows "Forced: …". Installs read `kefir_lang.json` from the PFS0 root (`{"format":1,"title_id":…,"language":"en-US"}`, both stream and random-access paths) and apply it after the title is registered. Format for the swuk_shop_nx repacker: `docs/dev/KEFIR-LANG-PACK.md`. `forced_language.hpp` + host test.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — force English on a game, launch, check language; Off; install an NSP with kefir_lang.json

## v0.13.983 — file browser extracts RAR, 7z, TAR, GZ, XZ, BZ2 (plan F.9b)
- "Extract" (was "Extract zip") now also takes rar/7z/tar/tgz/gz/xz/txz/bz2/tbz2 via libarchive (`archive_extract.cpp`, reads through `fs::File` so SD, USB and mounts work, seekable for 7z/rar); entries leaving the target folder are skipped (`archive_extract_plan.hpp` + host test); a bare .gz/.xz/.bz2 becomes one file without that extension. New build dependency `switch-libarchive` (README, CI step). New result code `ArchiveRead`.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, +430 KB, no first-party warnings) · switch: pending — extract a .rar, a .7z and a .tar.gz to the current folder, cancel a big one

## v0.13.982 — Phase S done: every docs screen shot in English and Ukrainian from the DocsDemo build (S.6)
- 88 recipes + 2 web pages, all taken in en and uk (176 + 4 PNGs, each checked) with `tools/docs/shoot.ps1 -Lang en,uk` in one unattended Eden run; the 49 old English shots retaken from DocsDemo for one look. `user` (console or PC side only): install-mtp-explorer, network-ftp-client, sharing-mtp-pc, console-transfer-ip-entry (Eden keyboard opens outside the frame), sharing-web-album (Eden album holds the owner's screenshots). uk shots synced into the guide site clone (switch-hub, branch kefir-hub, local commit ef9ff16e, not pushed).
- DOCS_DEMO: minimized install = background session (the badge over a working menu). Tools: shots.json `ini` / `startup` / `fresh` / `crop` / `web`, shoot.ps1 stops the Hub before writing config.ini, Sync retries the `ready` file, Hide-OwnData also hides /games and /pictures, sync_site_shots.py skips binaries and creates the image folder; demo save backups moved to `dumps/<title id>/` (the game-name folder changes with the UI language); fixtures for pictures, a text file, the changelog `____` section end. docs build: pass (en, uk; 59 links, 0 broken).
- Found, not changed (product): restoring a save looks in `dumps/<game name>` with the name in the console language, so backups made under another system language are only found through the title id folder; the USB badge says USB 3.0 because Eden's system_settings.ini forces it.
- host tests: pass · nro: built (DocsDemo, ReleaseWithInstall; 0 demo symbols in release) · switch: n/a

## v0.13.981 — DocsDemo: frozen demo scenes, game card, USB drive, console transfer (Phase S.4)
- DOCS_DEMO only: `[demo] scene=<name>` opens a frozen screen after start (`demo_scene.cpp`, `demo_install.cpp`: install queue review over fixture paths with deferred analysis, SD queue mid-install with speed graph, minimized, screensaver, summary 3 installed / 1 failed, MTP and FTP sessions, USB queue, Extract Options, firmware install prompt, TegraExplorer restore prompt; DemoSession never starts a thread or a source). PC Install (USB) stays on "Waiting for PC"; the titles.json `gamecard` game is the inserted card; a read-only SD folder is a USB drive (`ums0:/` in the header); Console Transfer takes a fictional sending console and lists its backups from an http fixture; microSD size from titles.json `sd`. Fixtures: demo /games files, USB drive folder, SMB location (TEST-NET 192.0.2.30, never reached). eden.ps1 also hides the owner's /games files while shooting; Test-Path -LiteralPath (names with [..] were read as wildcards). shoot.ps1: scene shots get their own launch per language.
- Recipes + en PNGs: install-sd-card-queue-review/-queue-progress/-minimized-badge/-screensaver/-summary, install-mtp-progress, network-ftp-progress, install-usb-waiting/-queue, console-transfer-remote-list/-te-confirm, install-gamecard-games-row, file-browser-split/-sources, settings-sources, software-extract-options, updater-firmware-confirm. `console-transfer-ip-entry` = user (Eden's keyboard opens outside the frame). The USB badge reads "USB 3.0 Enabled" as Eden's system_settings.ini leaves USB 3.0 forced (marker says USB 2.0).
- host tests: pass · nro: built (DocsDemo, ReleaseWithInstall; 0 demo symbols in release) · switch: n/a

## v0.13.980 — DocsDemo: network replies from fixtures (Phase S.3)
- DOCS_DEMO only: every http(s) request is answered from `sdmc:/config/kefir/demo/http/<host>/<path>` (`demo_http.cpp`, name rules in `demo_http_path.hpp` + host test; POST = path.<fnv1a32 of body>; the log prints each URL, its file and a missing POST body), nothing reaches the network. Ownfoil discovery finds one fictional shop; Wi-Fi shows two saved networks; account link/unlink is refused in demo builds (a stray A on "Link and reboot" during a recipe ran the real link in Eden; the account save stayed closed, nothing was written). Fixtures (fictional): App Store repo + generated icons/screens, Themezer page + previews, nx-links (Kefir 930, firmware 22.5/22.1/20.5), Kefir changelog, Hub release 0.14.0 (no .nro asset, so nothing installs), Ownfoil Home Shop catalog + title page, nx-cheats-db for Borshch Royale, translations cache, Kefir version file; images by `tools/docs/make_demo_images.py`. shoot.ps1: per-recipe `"ini"` and `"startup"` (own launch per language).
- Recipes + en PNGs: software-appstore-grid/-entry, themes-themezer-grid, updater-main/-changelog/-downgrade-warning/-hub-update-prompt, network-ownfoil-servers/-catalog/-install-panel, kefir-settings-translate, settings-about, system-tools-wifi, cheats-select. Moved to S.4 (scene): software-extract-options (needs a typed URL), updater-firmware-confirm (needs a downloaded firmware). Docs: Ownfoil opens the catalog directly when one server is saved (network.md says to select a server first).
- host tests: pass · nro: built (DocsDemo, ReleaseWithInstall; 0 demo symbols in release) · switch: n/a

## v0.13.979 — MTP Games drive: a Mods folder in every game folder (plan F.12)
- With the Separate or Both dump format each game folder lists `Mods` (localized): the game's `/atmosphere/contents/<tid>/` on the SD card, readable and writable (create, write, rename, delete files and folders); NSP files stay read-only, `.`/`..` segments are rejected (`ParseGamesPath` → `PathKind::ModsPath`, host test), drive free space = SD free space so PCs allow copies. Docs: sharing.md.
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — copy a mod folder into Games/Separate/<game>/Mods from Windows, rename/delete it, check it lands in /atmosphere/contents/<tid>

## v0.13.978 — split the MTP Games drive proxy (no behaviour change)
- `haze_game_proxy.cpp` (598 lines) → class in `include/haze/haze_game_proxy_internal.hpp`, filesystem ops in `haze_game_proxy.cpp`, game scan and NSP caches in `haze_game_proxy_catalog.cpp`; room for the per-game mods folder (plan F.12).
- host tests: not run (no logic change) · nro: built (ReleaseWithInstall, same size as 0.13.975) · switch: pending (MTP Games drive lists and copies as before)

## v0.13.977 — DocsDemo: demo saves, save backups, linked users (Phase S.2)
- DOCS_DEMO only: `save_discovery.cpp` appends one account save per titles.json save (owner = Eden profile at that index); `ListUsers` shows the first two profiles as linked. `tools/docs/make_demo_backups.py` writes Kefir Hub backup archives (`.nx_save_meta.bin`, owner uids from Eden profiles.dat) into fixtures `dumps/`: three dated Stonks Tycoon backups (Pixel), Borshch Royale for Pixel and Guest. Eden profiles Pixel, Kotyk, Guest were already set up. `eden.ps1`: Start-Hub moves the owner's own backups on the Eden SD (real games and nicknames) to `user/sdmc-hidden-by-docs`, shoot.ps1 / `Restore-OwnData` put them back; nothing is deleted.
- Recipes + en PNGs: `saves-list`, `saves-backup-options`, `saves-select-backup`, `saves-restore-confirm` (dialog only, No focused), `saves-backup-group`, `users-list`, `users-delete-hold` (dialog only, never held).
- host tests: pass · nro: built (DocsDemo, ReleaseWithInstall; 0 demo symbols in release) · switch: n/a

## v0.13.976 — Users: the separator in user headers shows as a dot
- Users grid header and the profile backup lists (restore library, remote list) printed "В·" (a UTF-8 "·" saved through CP1251) between the nickname and the link status; now "·". No other such bytes in sphaira/ or i18n.
- host tests: pass · nro: built (with v0.13.977) · switch: pending (Users screen header)

## v0.13.975 — Phase F: fill free space with zeros, hex view, mods size/delete, backup before save delete
- Tools: "Fill free SD space with zeros" works (was "Coming soon") + new "Fill free NAND space with zeros" (`zero_fill.cpp`: one ncm placeholder of free space − 64 MiB, written with zeros, always deleted; B cancels). File browser → Advanced Options → "View as hex" (`TextMode::Hex`, streamed pager, `text_helper::FormatHexLine/ReadHexPage` + host test, fixed-cell drawing for aligned columns). Game card shows mods size; "Delete mods" (keeps `cheats/`); deleting games with mods offers "Delete with mods". Deleting a live save first writes an AUTO backup (save is kept if the backup fails). Phase F added to plan.md (DBI comparison).
- host tests: pass (quick) · nro: built (ReleaseWithInstall, no first-party warnings) · switch: pending — fill SD/NAND (cancel too), hex view of a large file, mods size/delete + delete with mods, save delete leaves an AUTO zip

## v0.13.974 — DocsDemo: demo games with names, covers, contents and cheats (Phase S.1)
- DOCS_DEMO only: wraps `nsGetApplicationControlData` (NACP: name in the UI language in every slot, publisher, version, cover JPEG), `nsListApplicationContentMetaStatus` (base, updates, add-ons on the titles.json storage) and the ncm calls behind sizes and the move plan (`ncmContentMetaDatabaseList/ListContentInfo/Get`, `ncmContentStorageGetRightsIdFromContentId`); `title_info.cpp` skips the nxtc cache and `LoadControlManual` for demo ids, `lang` clears the title cache; Build ID for demo games from titles.json (`cheats_lookup.cpp`). Fixtures: cover `borshch-royale.jpg` (press-f.jpg had no game), two Stonks Tycoon cheat files.
- Recipes + en PNGs: `games-list`, `games-details`, `games-move-summary`, `cheats-files` (uk replay checked: Ukrainian game names). `cheats-select` moved to S.3 (cheats come from CheatSlips online). Stale files removed from build/ReleaseWithInstall romfs (ru.json, two .te); release romfs now equals a clean copy.
- host tests: pass · nro: built (DocsDemo, ReleaseWithInstall; 0 demo symbols in release) · switch: n/a

## v0.13.973 — DocsDemo: focus-free Eden input, every language on one screen (Phase S.0b)
- DOCS_DEMO only: the Hub reads button presses from `sdmc:/config/kefir/demo/input.txt` (`demo/demo_input.cpp`, one hook in `App::Poll`; commands `demo_cmd.hpp`: buttons, `wait`, `lang <code>` = new language + menus rebuilt on the main screen, `ready`); the old-forwarder notice is skipped. `eden.ps1` B/W/Shot/Lang go through that file (PostMessage removed), `Set-HubLang` writes language codes; `shoot.ps1` launches Eden once and loops the languages per shot. Recipe `index-tools-tab` + startup steps in shots.json.
- Checked: `shoot.ps1 -Lang en,uk -Only index-tools-tab` with Eden in the background, no clicks: both PNGs, correct labels per language, EmuNAND row (PNGs not committed, S.6 retakes all).
- host tests: pass (new tests/test_demo_cmd.cpp) · nro: built (DocsDemo, ReleaseWithInstall) · switch: n/a (release path unchanged when OFF)

## v0.13.972 — DocsDemo build switch (Phase S.0)
- `option(DOCS_DEMO OFF)` + `DocsDemo` preset (build/DocsDemo/kefir-hub.nro); `source/demo/`: titles.json read once, `-Wl,--wrap=nsListApplicationRecord` appends the six demo ids after the real records. Eden has no BIS fs (fsp-srv cmd 11), so the header had no NAND row: `#if DOCS_DEMO` hooks in `fs::GetStorageSpaces` (NAND space from titles.json `nand`) and the header EmuNAND label (App::IsEmummc unchanged). `nm`: demo symbols only in DocsDemo ELF, 0 in ReleaseWithInstall (the planned `strings nro | grep __wrap_` is 0 for both: nro has no symbols).
- Eden network (decides S.3): DNS and TCP work, HTTPS fails in the handshake (`code: 0 SSL connect error`, Eden ssl service) in the normal build; App Store screen itself not opened (Eden needs window focus for keys: PostMessage alone is ignored while unfocused). Games in Eden: 6 entries (ids, placeholder icons), EmuNAND 18.60 GB row shown.
- host tests: pass · nro: built (DocsDemo, ReleaseWithInstall) · switch: n/a (demo build only; release path unchanged when OFF)

## unreleased
- docs: Hub docs published at https://hub.customfw.xyz/ (Pages custom domain of this repo; customfw.xyz/kefir-hub/ redirects there): indexed, canonical links and per-language sitemap.xml (`DOCS_SITE_BASE` in build.sh → `site_url`), robots.txt; the guide preview moves to /guide/ and stays noindex. docs build: pass (local, DOCS_SITE_BASE set).
- docs: online preview https://customfw.xyz/kefir-hub/ (GitHub Pages, `.github/workflows/docs-preview.yml`): Hub docs from branch `docs` (en, `/uk/`) and the reworked guide from rashevskyv/switch branch `kefir-hub` under `/guide/`, rebuilt on push to either (switch sends `guide-updated`); edit link on every page, noindex. mkdocs.yml: repo_url/edit_uri/noindex/language links from env (unset locally = as before). docs build: pass.
- test: tests/test_demo_fixtures_contract.py (plan S.5) — DOCS_DEMO fixtures: titles.json ids/icons (256x256 JPEG)/save owners, every JSON http fixture parses, demo save backups carry a valid 128-byte .nx_save_meta.bin of a demo game with an owner, shots.json entries are recipes or user; runs in tests/run.sh. host tests: pass.
- docs: plan S.0b — focus-free input for Eden (commands from a file in DOCS_DEMO) and every language shot on one screen without restarting Eden.
- chore: screenshots in any UI language — recipes (`docs/site/shots.json`: button presses recorded by `tools/docs/eden.ps1` Rec/Shot), replay per language `tools/docs/shoot.ps1 -Lang uk,en` (sets `[config] language`, `[demo] scene`), fixtures `docs/site/fixtures/sdmc/` (six meme demo games with covers for Phase S), `tools/docs/sync_site_shots.py` (uk shots into the guide site, `inc/hub-shot.html`), shotlist recipe column; plan Phase S (DOCS_DEMO build). Scripts checked in PowerShell 7 with stubs; not yet run against Eden.
- docs: install/usb requirements and site inc/zadig.txt — Windows USB driver now installed by DBI Backend Qt 2.9.0 (WinUSB), ns-usbloader/Fluffy still need libusbK via Zadig, udev rule on Linux; plan D.1 done.
- docs: skill `update-docs` (.agents/skills, stub in .claude/skills; rule 5 in AGENTS.md) — how any agent keeps docs/site, screenshots, video scripts and the guide site in step with the code; tools/docs (eden.ps1, web-shot.mjs, check_site_links.py); label check warns about doc labels missing from the code; Console Transfer listed as under review.
- docs: video tutorial scripts `docs/video/01..09` (scene table, UK+EN voiceover, draft subtitles via `docs/video/srt.py`); docs fixes from the site pass (Album needs R on Kefir, assoc ini, /firmware is cleared), uk docs use «папка».
- docs: user docs site `docs/site` (MkDocs, EN+UK, 23 pages written from code); UI names as `[[en.json key]]` resolved from prod i18n per language (`tests/test_doc_labels_contract.py`); `shot` markers + `docs/site/shotlist.py` for screenshots; coverage map `docs/dev/DOCS-COVERAGE.md`, code findings `docs/dev/AUDIT-2026-10-02-docs.md`. docs build: pass; host tests: pass.
- chore: context diet — AGENTS.md/CLAUDE.md/plan.md, CHANGELOG from git history, junk untracked, one test-build skill, graphify hook.
- docs: README 435 -> 109 lines; its feature detail moved into docs/wiki (new Interface-and-Navigation.md).
- test: drop C++ source-text assertions from Python contracts (9 files deleted, static functions removed from 21); i18n JSON checks kept.
- docs: plan 2.2 table — every remaining Python test mapped to its C++ mirror (1 PURE part, 1 DROP, rest SEAM/KEEP).
- test: tests/run.sh honours `// LINK: <libnx-free .cpp>` lines in host C++ tests.
- test: DBI root-marker and save-entry matrix moved from Python model to tests/test_dbi_root_marker.cpp (real path_util.hpp).
- test: drop test_shutdown_lifecycle_contract.py (Python MockSystem of exit interleavings; mirrors no callable C++, hardware-only).
- docs: docs/dev/ARCHITECTURE.md — directory map, 19 Menu structs, thread map, god-node rules, build/test, i18n.
- docs: wiki Developer-Guide.md (architecture, changelog, build, host tests); README feature headings verified in wiki.
- docs: tools/i18n-translate/README.md (translate, add a language, parity check); module_catalog README checked (its test_catalog has 1 stale source-text test).
- test: `tests/run.sh --quick` runs host C++ tests + dead-symbol guard only (~20 s).
- chore: tools/dev/check.ps1 runs `tests/run.sh --quick` (or `-Full`) in WSL from Windows (not executed here: no PowerShell/WSL in this environment).
- test: drop source-text assertions — tests/test_save_restore_contract.cpp (only grepped .cpp text) deleted; test_catalog.py loses its uninstaller_menu.cpp text check (was the 1/9 failure; 8/8 pass).
- chore: .graphifyignore excludes docs/dev/CHANGELOG.md, docs/dev/history/, graphify-out/; after `graphify update .` CHANGELOG (was #2, 640 edges) is gone from God Nodes.

## v0.13.971 — restore of an uninstalled game's save names the game
- Restoring a backup whose game is not installed (no control data, archive without owner/metadata) now says "<game> is not installed. Install the game to restore its save." instead of "Application control data is missing save data owner." New status `GameNotInstalled`; 26 translations; docs saves.md EN+UK.
- host tests: pass; nro: built; switch: pending (restore a backup of an uninstalled game, e.g. from the Backups tab).

## v0.13.970 — Backup owner names: safety copies and two users with one nickname
- Found in Eden on the user's `/dumps` + DBI saves: safety copies (`recovery.zip`, no `.dbi_save_info.ini`) showed an id code; they now take the name from another backup of the same uid. The user's console has two accounts named "nin10do": tiles and rows now add the same `(XXXX)` tag the user picker already uses, so the two are told apart.
- Also seen, not changed: same game + owner from Kefir Hub and DBI is one tile (in the first source's section) with one row per source; backups that hold only metadata (Celeste, 1-2-Switch from v0.13.907) are left out of the library as before.
- host tests: pass; nro: built; switch: pending.

## v0.13.969 — Backups show the owner's name from the archive
- A backup of a user who is not on this console showed an id code (C37BD4AE); DBI shows the nickname. The metadata reader now keeps "Account=" from `.dbi_save_info.ini` (DBI and Kefir Hub archives both write it; `path::IniAccountName`, host-tested on the user's files) and `FormatBackupAccount` uses it when the uid is not a console account, so tiles, rows, the header, the user question and the confirmation say "Shark". The single-backup "Restore for user" question now also names the game, owner and date. Folder backups are not covered.
- host tests: pass; nro: built; switch: pending (Backups tab names).

## v0.13.968 — Backups tab: no "Restore all", a one-save tile opens its actions
- User decision: a tile now holds one owner, so "Restore all" had nothing to combine. Removed (`RestoreAllForGame`, `PromptRestoreAllDestinations`); a tile with one backup group opens Backup Action directly, as DBI does; a tile with several (slots, Device + BCAT) lists them first. Several saves at once = select tiles with X. Back from Backup Action returns to the list only when there is one.
- host tests: pass; nro: built; switch: pending (B5).

## v0.13.967 — Backups tab: one tile per game and user
- User decision after the Fall Guys report: backups of one game from different users are no longer one tile with a cross-user "Restore all". The Backups tab groups by game + owner uid (device/BCAT backups of a game share an owner-less tile); the owner is drawn on a strip at the bottom of the icon (grid), after the name (list) and in the header. Back navigation finds the tile by game and owner.
- Compared in Eden with DBI 810 on the user's own `/switch/DBI/saves` (DBI v119 from the card does not start in Eden: OpenBisFileSystem is not implemented): DBI lists one row per backup with the owner nickname from `.dbi_save_info.ini`; we show an id code for users not on the console (not changed yet).
- host tests: pass; nro: built; switch: pending (B3, B5).

## v0.13.966 — Restore: say whose backup the user question is about; a clash asks again
- User report (Fall Guys, "Duplicate restore target slot selected"): the card holds two DBI backups of the game from two users (Shark, not on this console; nin10do). "Restore all" asked "Restore for user" twice with nothing telling the backups apart, so both went to one user. The question now reads "Restore for user (game · backup owner · date)" (`BackupPickerTitle`), and a clash explains itself and asks that save again instead of ending the restore (stops if the console has one user).
- The old message key is replaced in all 26 locales (translated by hand). host tests: pass; nro: built; switch: pending (B5 with Fall Guys).

## v0.13.965 — Saves: tab switch reuses the scanned backup library
- Evidence (`scratch/log-saves-session-v934.txt`): Installed tab scans in ~20 ms, Deleted and Backups in ~980 ms each time, all of it in `ReadBackupEntries` (opens every archive). `ScanHomebrew(true)` on a tab switch reuses the last result; any other rescan and every `OnFocusGained` (dialog, transfer or another screen closed) drops the cache.
- Still ~1 s: the first visit to Deleted/Backups after opening the screen or after any action. Known limit: files added over MTP/FTP while the screen stays open and focused appear after the next action.
- host tests: pass; nro: built; switch: pending (tab switch speed, B2: a new backup is listed right away).

## v0.13.964 — Star/Unstar on L3
- Homebrew, Themes and Themezer: Star/Unstar moves from R3 to L3, so it no longer overlaps with R3 (expand a minimized task). Docs EN+UK, wiki and video scripts 08/09 updated.
- host tests: pass; nro: not built (built with v0.13.965); switch: pending.

## v0.13.963 — Batch save restore: one account question, one confirmation
- Restoring several selected saves asks "Restore for user" once for the whole batch (new item "Choose for each save" keeps the per-save question; the batch asks per save by itself when two selected backups would land in the same slot of one account). `CanShareAccount` in `save_batch_util.hpp`, host-tested.
- The paged confirmation (Item 1 of N, Next/Back/Restore) is replaced by one dialog that lists every save and its target (first 6, then "+N"). Three unused `PromptBatchRestoreTargets` overloads removed. New string translated in all 26 locales by hand (the translation proxy was down); docs EN+UK updated.
- Not changed: "Restore all" of one game still asks the user per account save. host tests: pass; nro: built; switch: pending (B5).

## v0.13.962 — Minimize/Expand on R3; File Browser Split back on L3
- User decision after v0.13.961: the background-task badge (install queue, MTP/FTP/web transfers) is minimized and expanded with R3 from any menu; File Browser Split returns to L3. In the install review queue "Package target" moves from R3 to L3 to free the button. L3 Launch in Games no longer collides.
- Known overlap: while a task is minimized, R3 expands it instead of Star/Unstar in Homebrew, Themes and Themezer.
- Docs (EN+UK), wiki, README, video scripts 01/02/03 and the guide site updated; script 09 restored. Screenshots with the stick glyph in the footer or badge still show the old button: retake pending.
- host tests: pass; nro: built; switch: pending (A10).

## v0.13.961 — File Browser Split on R3; minimized install badge shows Finished (A10)
- File Browser: Split moved from L3 to R3, because L3 expands a minimized background task from any menu.
- Minimized install badge: after the last package (Summary, or an MTP/FTP session still waiting for more files) it shows "Finished" and a full bar instead of N/N with an empty bar; "Cancelled" for a cancelled session. Docs (EN+UK), video script 09 and subtitles updated. Screenshots `file-browser-list` and `file-browser-picker` still show the L3 glyph next to Split: retake pending.
- Hardware: section A re-run by the user after v0.13.960 — all PASS (3.2 and H3 closed). host tests: pass; nro: built; switch: pending (A10: R3 Split, Finished badge).

## v0.13.960 — MTP progress-box state machine (3.2), no behaviour change
- The five `g_mtp_*` flags are one `MtpTransferState g_mtp_state`; every change goes through pure transitions in `include/haze/mtp_transfer_state.hpp` (`OnFileStart`, `OnFileDone`, `OnUserCancel`, `OnWorkerCancel`, `OnUiLaunch`, `OnUiClosed`, `OnSessionEnd`, `OnInit`, `TransferredBytes`). Call sites in `haze_helper.cpp` / `haze_internal.cpp` keep the mutex and apply the returned action.
- New `tests/test_mtp_transfer_state.cpp` (79 checks on the real header: Switch cancel, PC cancel, idle-window cancel, late-file relaunch, exit during transfer, repeated cancel). Deleted: `tests/test_mtp_cancellation_models.py` (Python model of itself; the contract test keeps the real patch-chain scenarios) and `tests/test_mtp_progress_calc.cpp` (local copies; its one real function is now tested through the header).
- host tests: pass; nro: built; switch: pending (re-run checklist section A).

## v0.13.959 — USB waiting-screen test exercises the real code (2.8, 1 of 10)
tests/test_usb3_indicator.cpp re-implemented the badge text choice and the warning-card position locally. Both now live in ui/menus/dbi/usb_status_text.hpp (UsbStatusTextKey, WaitingWarningY), dbi_draw.cpp calls them, and the test includes that header. No behaviour change: same five strings, same max(text bottom + 35, 470). Nine copy-tests remain in 2.8. host tests: pass · nro: built (checkpoint v0.13.959) · switch: pending (PC Install (USB) waiting screen shows the same badge text)
## v0.13.958 — fix the -Werror build (1.5)
The first -Werror build failed on warnings my path filter had missed because they are reported inside dependency headers: save_restore_route.cpp included libhaze's internal `haze.hpp` (unused there), which redefined ON_SCOPE_EXIT/R_TRY/R_THROW/R_SUCCEED for the rest of that file and pulled in two vapours warnings — include removed; `-fdiagnostics-all-candidates` was passed to the two C sources — now C++ only. Clean `ReleaseWithInstall` build with -Werror, 0 first-party warnings. host tests: pass (--quick) · nro: built · switch: pending (no behaviour change)
## v0.13.957 — graph holes: net.hpp parses, the rest are marked (3.5)
net.hpp: the braced default argument `= {}` stopped the tree-sitter parser for the whole header; `= nullptr` is the same empty std::function and parses. defines.hpp (macro-generated enumerators), nxlink.h / ams_su.h (extern "C" brace under #ifdef in a C header) and hbl/source/main.c (attribute macro before the name) have no trivial rewrite and carry a `// graphify: parse stop` note. host tests: pass (--quick) · nro: built (checkpoint v0.13.959) · switch: pending (no behaviour change)
## v0.13.956 — warning-free first-party build with -Werror (1.2)
Inventory after recompiling every first-party object: 21 warnings in sphaira/ (0 in hbl/, sysmodule/). Fixed all: 11 ternaries mixing Result with an FsError/Result_ enumerator (cast to Result), 4 missing switch cases in file_picker (default), unused GetFsSaveAttr deleted, enum|enum in devoptab_mtp_usb (u8 casts), a %lld/s64 format in yati_pipeline. `-Werror` is now on for the sphaira target (dependencies are separate targets and unaffected). 1.3: `tests/run.sh` exits 0 in WSL. host tests: pass · nro: built (checkpoint v0.13.959) · switch: pending (no behaviour change)
## v0.13.955 — starts in the Eden emulator
main.cpp: with no argv (Eden starts an .nro without one) use `sdmc:/switch/kefir-hub.nro` instead of exiting; when acc:u0 fails for lack of a launch property, fall back to acc:u1. Both paths are unused on a console (hbloader passes argv; acc:u0 works). Lets the docs screenshots be taken in Eden. host tests: pass (--quick) · nro: built (same change in a separate copy) · eden: starts, reaches the Homebrew tab · switch: pending (start from Album and from a title, as before)
## v0.13.954 — save restore: one confirmation, no cut-off mid-save, cancel is not an error
Hardware feedback on v0.13.953. (1) Restore asked twice: an untranslated «Restore save data to <slot>?» from ResolveRestoreTarget and then the real confirmation; a single matching slot is now taken without a prompt and the remaining confirmation names the game and the account. (2) Cancelling while a save was being rewritten left it half-written: ProgressBox::SetCancelDeferred — from the moment RestoreSaveZip starts rewriting until that save is written and verified, a confirmed cancel is only remembered; it is applied afterwards, so a batch stops before the next save. Cancel before the rewrite still stops at once. (3) A user cancel showed «Restore failed!»; it is now the toast «Restore cancelled.» (existing string). host tests: pass · nro: built (checkpoint v0.13.954) · switch: pending (B2/B4/B5: one prompt; cancel during a restore)
## v0.13.953 — save restore no longer requires MTP to be off
Restore refused whenever MTP was running, which with auto-MTP means whenever a PC is connected. The real conflict is narrower: the MTP «Saves» storage caches the save filesystems a PC browsed, and a mounted save cannot be opened again. haze::ReleaseSaveMounts() closes that cache before a restore; the «MTP is currently active» message now appears only while the PC has a file of a save open. host tests: pass · nro: built (checkpoint v0.13.953) · switch: verified on v0.13.953 (restore with MTP on)
## v0.13.952 — PC-side MTP cancel is detected at once (H2, second pass)
v0.13.949 on hardware: after Cancel in Explorer the box still decayed for ~a minute. Cause: Windows stops sending data and issues the Still Image class Cancel Request (0x64) on the control endpoint, which libhaze never read; SendObject waited until Windows sent its next command and swallowed that as file data (the 16-byte «EOT» in scratch/A4.txt). libhaze patch 25: the bulk-out read also waits on the interface setup event; a Cancel Request during an object transfer retires the read, drops the partial file and ends without a response; Get_Device_Status is answered OK. The installer no longer restarts MTP after a source interruption (that restart killed the copy Windows started next; ShouldRestartMtp/ScheduleMtpRestart removed). Control-transfer servicing is written without hardware access: if it is wrong the cancel is still detected and Windows falls back to its own timeout. host tests: pass · nro: built (checkpoint v0.13.953) · switch: verified on v0.13.953 (A4, A1, A8, cancel on PC then a new copy)
## v0.13.951 — uk: «Параметри» for every Options label
uk.json: the 24 values that read «Опції …» now read «Параметри …», the same word as the + panel title (v0.13.950). host tests: i18n contract pass · nro: not built · switch: pending
## v0.13.950 — uk translation fixes; translator context notes
uk.json: Target «Ціль» (was «Макет»), Legacy «Застарілі», HB Menu kept as is, Options «Параметри» (was the same word as Settings), Install «Встановити» (verb), Enable sysMMC/emuMMC as toggle labels, «Налаштування Kefir», «чити», «папка» instead of «тека». tools/i18n-translate/context.json gives translate.py notes for ambiguous keys (`--only-context --force` re-translates them). host tests: pass (--quick + translator tests) · nro: not built · switch: pending (look at the strings in the UI)
## v0.13.949 — serialize web server stop/start (3.7f)
web.cpp: StartShareServer (main thread) could run while WebShareStop (the server's ProgressBox worker) was still joining workers — both wrote g_share_threads/count/port. Start and stop now take one lifecycle mutex; `g_share_port` and `g_share_offline` are atomics. Closes plan 3.7 (a–f). host tests: pass (--quick) · nro: built (checkpoint v0.13.949) · switch: pending (C4; stop the web server and start it again at once)
## v0.13.948 — atomic install-thread flag (3.7e)
install_stream_menu_base.cpp: `s_install_thread_created` is written by the transport thread that starts the background install worker and read on teardown from another thread; it is now std::atomic<bool>. host tests: pass (--quick) · nro: built (checkpoint v0.13.949) · switch: pending (A1, A6)
## v0.13.947 — no join on a thread that never started (3.7d)
ProgressBox, the download queue/workers and the threaded transfer core checked threadStart only for logging (or returned early) and then called threadWaitForExit on a thread that never ran — a hang on the error path. Each now records whether the thread started and joins only then; ProgressBox reports the failure through its done callback and closes, the transfer core clears the running flag of the missing thread and returns the start error through its normal teardown. host tests: pass (--quick) · nro: built (checkpoint v0.13.949) · switch: pending (smoke: any transfer, a download, app exit)
## v0.13.946 — bounded path append in recursive directory creation (3.7c)
fs.cpp CreateDirectoryRecursively appended each component with `strncat(path, dir, dir.size())` (bounded by the source, not the destination); it now uses FsPath::operator+=(string_view), which clamps to FS_MAX_PATH-1 and logs a truncation. host tests: pass (--quick) · nro: built (checkpoint v0.13.949) · switch: pending (smoke: create nested folders)
## v0.13.945 — bounded fallback NRO name (3.7b)
nro.cpp: the name of an NRO without a valid NACP was copied with `strncpy(..., len - 4)` — no destination bound and an underflow for names shorter than 4 chars; now clamped to sizeof(name)-1 and `len >= 4` is checked. host tests: pass (--quick) · nro: built (checkpoint v0.13.945) · switch: pending (smoke: homebrew list)
## v0.13.944 — bounded FsPath copies (3.7a)
FsPath::From(std::string/string_view) copied without a bound or terminator and operator+=(string/string_view) appended the whole source; both now clamp to FS_MAX_PATH-1, terminate and log a truncation, and operator+=(char) no longer writes past a full buffer. Not compilable on host (libnx). host tests: pass (--quick) · nro: built (checkpoint v0.13.945) · switch: pending (smoke: file browser, long paths)
## v0.13.943 — log MTP object creation to diagnose the folder drop (H3, diagnostics only)
No console log of a folder drop exists, and the code path reads correct (SendObjectPropList -> Association -> FsProxy::CreateDirectory), so no behaviour is changed. libhaze patch 24 logs every SendObjectPropList (storage, parent, format, name) and FsProxy::CreateDirectory logs its result: the next folder drop shows whether the host never asks, the parent lookup fails, or the directory create fails. H3 stays open. host tests: pass · nro: built (checkpoint v0.13.945) · switch: pending (A2 with a folder, then send log.txt)
## v0.13.942 — Backups tab shows source sections for games; DBI saves folder from dbi.config (H7)
Sections by origin existed only for loose (system) backups: game tiles were put in one unlabeled section, so with game backups nothing was visible. ComputeGridSections now labels game sections too (Kefir Hub / DBI / JKSV / Checkpoint / Other); a game with several sources sits under its highest-precedence one. DBI root = `SavesFolder` from /switch/DBI/dbi.config (path::SdFolderFromConfigValue, host-tested), fallback /switch/DBI/saves; JKSV and Checkpoint roots unchanged. host tests: pass · nro: built (checkpoint v0.13.945) · switch: pending (B3)
## v0.13.941 — remove MTP device-root routing (H8)
User decision: delete it. libhaze patches 5 and 6a no longer route uploads addressed to the device root (packages -> Install, the rest -> microSD, v0.13.913); SendObjectInfo/SendObjectPropList resolve the storage root only, and trees patched with the routed shape are upgraded in place. Wiki line about copying to the «Nintendo Switch device» removed. The Install storage and copying into microSD are unchanged. host tests: pass · nro: built (checkpoint v0.13.945) · switch: pending (A1, A2 file copy)
## v0.13.940 — save scan logs an unreadable space once (H10)
`fsOpenSaveDataInfoReader` for space 100 (ProperSystem) fails on every scan with 2002-6001; the value only looked like it grew by 0x00400000 per call because fs sets the reserved upper bits of the Result — the low 22 bits are constant and nothing is left open (a failed open returns no reader). The line is now logged once per run per space with `R_VALUE`. `account uid is not found: 0x0` is the default-user lookup on a console with two users (no preselected user), unrelated to H5/H6. host tests: pass · nro: built (checkpoint v0.13.940) · switch: pending (log check only)
## v0.13.939 — batch restore asks once per save slot; same-named accounts are distinguishable (H6)
B5: a game backed up by two tools (e.g. Kefir Hub + DBI) for one account is two backup groups, so «Restore» asked for the user twice and the second answer hit «Duplicate restore target slot»; RestoreBackupGroups and RestoreAllForGame now keep only the newest group per slot (BackupGroupKey). The two «nin10do» rows are two real accounts (different uids, same nickname): the user pickers append the last 4 uid hex digits when nicknames collide. The «(Minecraft)» title was the batch's current item, not a stale one. New pure header save_batch_util.hpp + tests/test_save_batch_util.cpp. host tests: pass · nro: built (checkpoint v0.13.940) · switch: pending (B5)
## v0.13.938 — restore from a live save entry no longer fails revalidation (H5)
Exact failing comparison: RestoreSavesPicked/RestoreSaves required `check_info.backup_source == group.backup_source`, but a group built from a live save (MakeBackupGroupFromLiveEntry) spans all sources and kept the default `Other`, so every Kefir Hub/DBI/JKSV archive was reported as «changed or no longer available» (since v0.13.882). Revalidation now checks identity (BackupGroupKey) only and logs a failure; the live group is labelled with its newest archive's source. Target label: rescanned action entries keep the list name, and the "Corrupted" NACP placeholder is never shown (falls back to Unknown). host tests: pass · nro: built (checkpoint v0.13.940) · switch: pending (B4, B2)
## v0.13.937 — empty saves are not written as invisible backups (H4)
The /dumps/12Switch archive (892 bytes, Device save) holds only DBI metadata: an empty save still produced a ZIP because the root collection always exists, and the Backups tab drops archives without payload (restore refuses them too). BackupSaveInternal now skips a save with no files or folders, and the toast says "No save data found for this title" when nothing was written. Device backups are not filtered and `show_backups` only gates the mixed view. host tests: pass · nro: built (checkpoint v0.13.940) · switch: pending (B1 → B2)
## v0.13.936 — PC-side MTP cancel closes the box and drops the partial file (H2)
scratch/A4.txt: Windows ends the data phase with a short packet, so libhaze saw a normal EOT, kept a truncated file (116 MB of 443 MB) and reported success; the ProgressBox then waited out its 1.5 s idle window. SendObject now treats EOT before the declared size as a host cancel: WriteEnd is reported aborted (box closes at once), the partial file is deleted, the response is IncompleteTransfer (libhaze patch 23 + ops_so_body_new_tail). host tests: pass · nro: built (checkpoint v0.13.936) · switch: v0.13.949 FAIL on A4 (box still decays ~1 min; fixed again in v0.13.952)
## v0.13.935 — MTP survives a Switch-side cancel (H1)
libhaze patch 21–22: SendObject cleared the cancel flag in its scope exit, so the main loop never saw a local cancel, left the thread with usb:ds closed and app_usb handed the port to host mode (the v0.13.922 recovery was unreachable). Now every broken transport re-enumerates in device mode (log: `[USB] MTP re-enumerated in device mode after a local abort`); Exit during recovery skips the re-init; `0x828C` (status of the URB we cancel ourselves) is no longer reported as a failed cancel. Keeping the PTP session open without re-enumeration is not done: the host is mid data phase and libhaze has no STALL/Get_Device_Status path; drain-to-EOT (v0.13.918) breaks the ≤2 s goal on large files. host tests: pass · nro: built (checkpoint v0.13.936) · switch: verified on v0.13.949 (A3, A5, A6, A11; A7 covered by A3)
## v0.13.934 — build checkpoint (ReleaseWithInstall)
Verified clean build for v0.13.929-933 (safe string copies, thread lifecycle parity, test cleanup); [100%] Built target sphaira_nro in WSL. host tests: pass · nro: built · switch: pending

## v0.13.933 — thread lifecycle parity
Audit of all 19 threadCreate sites (+ utils::CreateThread users): create → threadWaitForExit → threadClose on normal, error and Exit paths. Fixed 4 error-path defects: ftpsrv Init and utils::CreateThread leaked the created thread when svcSetThreadCoreMask failed; title_info Init leaked it when threadStart failed; nxlinkExit joined/closed even when not running (stale handle after a failed re-init). Not compiled on host (needs libnx). host tests: pass (--quick) · nro: not built · switch: pending (FTP/NX-Link toggle, title icons)
| thread (site) | create | wait + close | result |
|---|---|---|---|
| download queue + workers (`download.cpp` 28, 86) | `Init` | `Exit` → `ThreadEntry::Close` | ok; start/core-mask failure → Close waits on an unstarted thread (noted, not changed) |
| forwarder check (`forwarder_auto_install.cpp` 339) | `StartCheck` | `StopCheck` (app exit) | ok |
| FTP (`ftpsrv_helper.cpp` 277) | `Init` | `Exit` | fixed: core-mask failure path |
| log flush (`log.cpp` 98) | `ensure_thread_started` | `stop_thread` (log_file_exit / log_nxlink_exit) | ok |
| NTP (`ntp.cpp` 411) | `Start` | `Stop` | ok |
| NX-Link (`nxlink.cpp` 511) | `nxlinkInitialize` | `nxlinkExit` | fixed: no join/close unless running |
| transfer read/write (`threaded_file_transfer_core.cpp` 411, 415) | `TransferInternal` | ON_SCOPE_EXIT | ok; t_write start failure → waits on unstarted thread (noted) |
| title info (`title_info.cpp` 393) | `Init` (ref-counted) | `Exit` | fixed: threadStart failure path |
| install session (`dbi_menu.cpp` 93, 123) | `Menu::Menu` | `~Menu` (m_thread_created) | ok |
| FS metadata (`filebrowser_view.cpp` 81) | `FsView::FsView` | `~FsView` | ok |
| stream installer (`install_stream_menu_base.cpp` 266) | `OnInstallStart` | `JoinInstallThread` (next start, TeardownWorker) | ok |
| ProgressBox (`progress_box.cpp` 53) | ctor | dtor | ok; create/start failure → dtor waits on unstarted thread, box never completes (noted) |
| yati read/decompress/write (`yati.cpp` 129-137) | `InstallNcaInternal` | ON_SCOPE_EXIT after each start | ok |
| `utils::CreateThread` (`utils/thread.hpp` 12): web workers, mDNS, upload writer, curl push/pull, Async | callers | `WebShareStop`, `StopMdnsResponder`, `finish_writer`, `~PushPullThreadData`, `~Async` | fixed: core-mask failure path |
| MTP responder | libhaze (`haze::Initialize`) | `haze::Exit` | not a sphaira threadCreate |
## v0.13.932 — bounded string copies
45 strcpy/strcat/sprintf sites: 14 literal-into-large-buffer tagged `// literal, bounded`; 30 replaced (snprintf with sizeof(dst) / NAME_MAX+1 for devoptab dirnext; FsPath From/+= now truncate at FS_MAX_PATH via memmove/strncat; fs.cpp trailing "/" via bounded strncat); 1 dead commented strcat removed. Not compiled on host (needs libnx; host g++ 11 lacks if consteval) — FsPath/hasher/tik-path logic checked in a scratch copy only. host tests: pass (--quick) · nro: not built · switch: pending
## v0.13.931 — atomic mDNS address
web_mdns.cpp: `g_mdns_ip` -> std::atomic<u32> (set by StartMdnsResponder from the main thread or the web ProgressBox worker, read by the mDNS thread). Plan 3.1 closed: the task grep has 0 unannotated non-atomic globals. Not compiled on host (needs libnx). host tests: pass (--quick) · nro: not built · switch: pending
## v0.13.930 — atomic web share counters
web.cpp: `g_share_ip`, `g_share_resume_gen`, `g_share_thread_count` -> std::atomic (written by StartShareServer on the main thread, read/written by the server ProgressBox worker in TickShareNetwork/WebShareStop). Not compiled on host (needs libnx). host tests: pass (--quick) · nro: not built · switch: pending
## v0.13.929 — annotate remaining single-thread and mutex-guarded globals
Comment-only: nxlink `g_is_running` (g_mutex), i18n `g_languages_scanned` (main thread, startup), auto_update `g_notify_shown` (g_job_mutex), title_info `g_ref_count` (g_mutex), filebrowser `g_smb_ref_count` (main thread), remote_input `g_*` (g_mutex), steamgriddb `g_api_key_cache_loaded` (g_api_key_mutex), wifi `g_connect_request_active` (main thread); per-line tags on haze `g_mtp_*` (g_mtp_ui_mutex). Every use checked. host tests: pass (--quick) · nro: not built · switch: pending
## v0.13.928 — build checkpoint (ReleaseWithInstall)
Verified clean build for v0.13.923-927 global-lock annotations; [100%] Built target sphaira_nro in WSL. host tests: pass · nro: built · switch: pending
## v0.13.927 — annotate FTP running flag
ftpsrv_helper.cpp: `g_is_running` is only touched under `g_mutex`; comment added, no code change. host tests: pass · nro: not built · switch: pending
## v0.13.926 — annotate log globals
log.cpp: `g_file_open`, `g_buffer_len`, `g_thread_running`, `g_thread_stop` are only touched under `mutex` (checked at every use, incl. the flush thread); comments added, no code change. host tests: pass · nro: not built · switch: pending
## v0.13.925 — annotate MTP shared state
haze: `g_is_running` is only touched under `g_mutex` and the `g_mtp_*` state only under `g_mtp_ui_mutex` (checked at every use); comments added, no code change. Audit F3's g_is_running race claim does not hold. host tests: pass · nro: not built · switch: pending
## v0.13.924 — atomic account-daemon flag
account_link: `g_daemons_terminated` -> std::atomic<bool> (written on ProgressBox workers, consumed on the main thread); `g_launch_link_prompted` annotated main-thread only. host tests: pass · nro: not built · switch: pending
## v0.13.923 — atomic network cache flags
net.cpp: `g_cache_value`/`g_cache_valid` -> std::atomic<bool> (TryConnect workers clear them while the UI reads); `g_request_open`, `g_cache_ts` annotated. host tests: pass · nro: not built · switch: pending
## v0.13.922 — keep MTP active during USB recovery
Recovery flag set before local cancel; no haze::Exit during controlled USB detach; MTP restarts if enabled but stopped. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.921 — fix libhaze cancel patch compilation and verify build
Fixed duplicate R_TRY_CATCH from patch_libhaze_cancel.cmake in ptp_responder.cpp; patch idempotent. host tests: py contracts pass · nro: built · switch: pending
## v0.13.920 — abort cancelled MTP transfer promptly
Local MTP install cancel stops reading the file without draining to EOT; libhaze re-inits broken transport. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.919 — select language from bundled translations
Language list from bundled JSON `__language_name`; language stored as text code in config.ini, numeric values migrated. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.918 — drain MTP transfers after install cancellation
After cancel libhaze drains the transfer, then sends TransactionCanceled; libhaze patches split cancel/cleanup/usb. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.917 — avoid MTP restart after install cancellation
User-confirmed cancel ends the PTP transaction; MTP stays active for the next transfer. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.916 — fix compilation errors and verify build
Dropped `<haze/results.hpp>` from haze proxies (R_* macro clash); added `haze::ResultCancelled()`. host tests: py contracts pass · nro: built · switch: pending
## v0.13.915 — add first-run language choice and 26 localizations
First-run language picker, 26 locales, ru.json removed; i18n completeness contract. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.914 — fix MTP install cancellation and shutdown
Cancel-install dialog takes input over active session; exit joins worker before stopping MTP. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.913 — route MTP device root uploads
MTP writes to device root: packages go to Install proxy, other files to microSD. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.912 — fix compilation errors and verify build
Fixed libhaze `operator==` for ams::Result and `transfer_success` scope; patch dedup on reconfigure. host tests: py contracts pass · nro: built · switch: pending
## v0.13.911 — fix MTP transfer cancellation
B -> Yes cancels the active MTP file to microSD; partial file deleted; URB 0x748C treated as abort. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.910 — default forwarders to 39-bit with optional fourth core
Forwarders default to 39-bit address space and 3 CPU cores; 4 cores optional with warning. host tests: i18n JSON checked · nro: not built · switch: pending
## v0.13.909 — expose current app forwarder in plus menu
"+" menu gets Install Title Mode forwarder above Settings. host tests: not run · nro: not built · switch: pending
## v0.13.908 — fix MTP SD copy routing and minimized progress
MTP copies packages to SD (no root interception); collapsed install bar shows overall progress. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.907 — fix CollectBackups namespace collision and verify build
Fixed `path` shadowing in CollectBackups (save_backup_pub.cpp). host tests: py contracts pass · nro: built · switch: pending
## v0.13.906 — target hbmenu for Kefir Hub forwarders
Forwarder target is /hbmenu.nro when HB Menu replacement is on, else current NRO path. host tests: not run · nro: not built · switch: pending
## v0.13.905 — write save backups to selected folder
Save dumps go to configurable backup_root (default /dumps). host tests: py contracts pass · nro: not built · switch: pending
## v0.13.904 — finish MTP progress banner lifecycle
MTP progress box closes after the last file; timer reset per file. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.903 — fix MTP transfer UI lifecycle and build warnings
MTP banner lifecycle via g_mtp_transfer_active/seq under g_mtp_ui_mutex. host tests: py contracts pass · nro: not recorded · switch: pending
## v0.13.902 — update README and wiki documentation for release
README and docs/wiki synced to release. host tests: n/a (docs) · nro: not recorded · switch: pending
## v0.13.901 — update USB queue during package transfer
Live SPHQ queue polled after a FileRange read. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.900 — fix compilation errors and verify build
Ownfoil compile fixes. host tests: py contracts pass · nro: built · switch: pending
## v0.13.899 — synchronize live USB queue
DBI Backend Qt sends ordered SPHQ list with revision; ReviewQueue applies it. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.898 — recognize empty SPHQ queue
Empty SPHQ reply handled. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.897 — restore initial DBI USB list request
Initial DBI List/SPHQ request after Awoo listen. host tests: not recorded · nro: not built · switch: pending
## v0.13.896 — add Ownfoil client
## v0.13.895 — install packages recursively from folders
## v0.13.894 — group game backups and restore all
## v0.13.893 — return from nested file browser with minus
## v0.13.892 — fix batch restore target call
## v0.13.891 — simplify backup restore menus
## v0.13.890 — fix HB Menu title scroll access
## v0.13.889 — improve backup titles and icon fallback
## v0.13.888 — remove duplicate icons save layout
## v0.13.887 — wrap save backup navigation past section header
## v0.13.886 — restore backup without save slot prompt
## v0.13.885 — remove empty rows between backup sources
## v0.13.884 — align backup dividers and title labels
## v0.13.883 — remove first backup grid gap
## v0.13.882 — group backup sources and restore folder backups
Backups grouped by origin (Kefir Hub, DBI, JKSV, Checkpoint, Other). host tests: not recorded · nro: built · switch: pending
## v0.13.881 — restore saves for uninstalled games
Restore a save without the game installed, using ZIP metadata. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.880 — show DBI save backups
DBI ZIP backups listed (payload_count carried with metadata). host tests: not run · nro: not built · switch: pending
## v0.13.879 — close remaining file-size audit
Remaining 25 files over 600 lines split by responsibility. host tests: pass · nro: built · switch: pending
## v0.13.878 — split installer, menus and contract suites
yati.cpp, menus and tests split. host tests: pass · nro: built · switch: pending
## v0.13.877 — split transfer pipeline
threaded_file_transfer.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.876 — split App implementation by responsibility
app.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.875 — split web responsibilities
web.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.874 — split save operations
save_menu_ops.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.873 — split save menu responsibilities
save_menu.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.872 — split save path responsibilities
save_paths.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.871 — remove dead code and restore clean build
Dead code removed; accumulated compile blockers fixed. host tests: py contracts pass · nro: built · switch: pending
## v0.13.870 — add Game Tools save-slot manager
## v0.13.869 — create and grow save slots safely
## v0.13.868 — admit DBI backups through exact restore route
## v0.13.867 — fix compilation errors and verify build
## v0.13.866 — fail closed on save deletion errors
## v0.13.865 — expose all save types over read-only MTP
## v0.13.864 — make MTP Saves read-only
## v0.13.863 — make backup freshness conservative
## v0.13.862 — distinguish backup rank provenance
## v0.13.861 — retain exact backup library members
## v0.13.860 — check ordinary SD backup publication
## v0.13.859 — reject mismatched save identity before backup export
## v0.13.858 — refuse unsafe RAW save container restore
## v0.13.857 — import selected SD save folders through checked ZIP staging
## v0.13.856 — decode JKSV ZIP metadata through shared admission
## v0.13.855 — commit ZIP restore chunks through closed handles
## v0.13.854 — verify ZIP restores through fresh read-only mount
## v0.13.853 — require verified SD recovery before ZIP save restore
## v0.13.852 — reject obvious save undersize and remove growth guesses
## v0.13.851 — fix shutdown session admission and transport teardown
## v0.13.850 — validate ZIP payload accounting before restore
## v0.13.849 — require explicit existing restore targets
## v0.13.848 — discover exact save slots and use actual space
## v0.13.847 — show MTP saves by game and user
## v0.13.846 — fix host test blockers
## v0.13.845 — share ZIP save restore backend
## v0.13.844 — make ZIP save restore fail closed
## v0.13.843 — fix backup account restore flow
## v0.13.842 — align backup metadata columns
## v0.13.841 — polish save metadata and restore
## v0.13.840 — fix compilation errors and verify build
## v0.13.839 — finish save actions compilation fixes
## v0.13.838 — fix save actions GCC build
## v0.13.837 — enrich save backups and actions
## v0.13.836 — keep web sharing awake and fix cancel dialog
## v0.13.835 — fix transliterated russian strings and uninstalled save titles
## v0.13.834 — clean saves backup actions
## v0.13.833 — improve selection mark contrast
## v0.13.832 — separate save popup from tabs
## v0.13.831 — fix compilation errors and verify build
## v0.13.830 — move selection checks to tile corners
## v0.13.829 — add save category tabs
## v0.13.828 — harden SysNAND forwarder safety
## v0.13.827 — fix Wi-Fi connection reliability
## v0.13.826 — add wi-fi management menu under tools
## v0.13.825 — fix firmware cleanup false failure
## v0.13.824 — informative error dialogs and network gate in updater
## v0.13.823 — localize firmware update and reboot notifications
## v0.13.822 — silence error on user creation cancellation
## v0.13.821 — improve downgrade warning readability
## v0.13.820 — fix downgrade staging and translation layout
## v0.13.819 — simplify translation install UI
## v0.13.818 — separate interface translation languages
## v0.13.817 — restore translation removal contract
## v0.13.816 — remove locked translations offline
## v0.13.815 — fix post-806 regressions
## v0.13.814 — preserve DBI translation files during firmware cleanup
## v0.13.813 — background translation replacement and deferred reboot on removal
## v0.13.812 — remove themes and translations on all firmware updates
## v0.13.811 — remove themes and translations on downgrade and warn about Nintendo folder
## v0.13.810 — automated post-downgrade fix via TegraExplorer and Maintenance Mode warning
## v0.13.809 — fix duplicate HR and separator navigation in DBI menu
## v0.13.808 — pin top DBI items, add separator and alphabetical sorting
## v0.13.807 — move fan curve from Kefir settings to Tools
## v0.13.806 — sort interface translation languages
## v0.13.805 — prefer standard HTTP port
## v0.13.804 — automated firmware-to-translation mapping and FW 22.5.0+ support
## v0.13.803 — fix global list focus draw order
## v0.13.802 — add kefir.local mDNS discovery
## v0.13.801 — avoid TegraScript compound conditions
## v0.13.800 — integrate profiles and playtime restore
## v0.13.799 — add test-build skill and fix compilation errors
## v0.13.798 — restore immediate NTP display with timezone
## v0.13.797 — harden remote NAND backup transfer
## v0.13.796 — receive and restore NAND backups from another console
## v0.13.795 — legend actions parity, direct restore, and console transfer share in Manage Backups context menu
## v0.13.794 — Manage Backups context menu with Select All, Delete and Rename
## v0.13.793 — diagnostic dashboard for dump script and 5s auto-reboot to Hekate
## v0.13.792 — payload swap fallback for legacy Hekate and bidirectional Hekate restore
## v0.13.791 — restore payload.bin in goHekate, early disarm in TE scripts and payload integrity audit
## v0.13.790 — embed TegraExplorer in RomFS, payload version auto-sync, dump script pause and hekate reboot
## v0.13.789 — auto launch TegraExplorer via hekate payload fallback on backup
## v0.13.788 — archive NAND transfer backups
## v0.13.787 — restore exact selected NAND transfer pack
## v0.13.786 — harden remote update check, stream-free version parsing and auto-update install destination
## v0.13.785 — fix svcCallSecureMonitor build error, SetRegion_HTK compatibility and DBI USB status/target sync
## v0.13.784 — accurate EmuNAND detection, header NAND label and SysNAND forwarder safeguard
## v0.13.783 — strict only-if-newer remote update detection via version_compare
## v0.13.782 — fix account link detection via Baas administrator IPC
## v0.13.781 — consistently center folder and file labels in icon layout
## v0.13.780 — diagnostic firmware, target translation tag and release URLs preview in interface translation menu
## v0.13.779 — on-the-fly image viewer rotation via shoulder buttons L and R
## v0.13.778 — move file browser layout setting into view options submenu
## v0.13.777 — dynamic minus button navigation to homebrew screen or app exit
## v0.13.776 — single-prompt concise notification on incomplete TegraExplorer restore
## v0.13.775 — eliminate interior tab overlap from vector folder icon
## v0.13.774 — prompt reboot after setting user profile avatar
## v0.13.773 — fix minimized install touch badge compile error and verify build
## v0.13.772 — direct firmware-matched interface translations and metadata fallback
## v0.13.771 — USB install Minimize / Expand and dedicated origin identification
## v0.13.770 — disable B and map X to Cancel installation for FTP/MTP/HTTP
## v0.13.769 — unify vector folder outline and remove interior tab overlap
## v0.13.768 — fix menu list selection outline draw priority and z-order
## v0.13.767 — transport-specific install success notifications and i18n decoupling
## v0.13.765 — fix MTP install actions and stat row label spacing
## v0.13.764 — fix USB unplug UI teardown
## v0.13.763 — refresh automatic install plan
## v0.13.762 — clarify install storage targets
## v0.13.761 — correct install queue delivery version
## v0.13.760 — add install queue sorting and free space
## v0.13.759 — repair transport install window
## v0.13.758 — serialize background MTP batch installs
## v0.13.757 — unify transport install queue UI
## v0.13.756 — clean TegraExplorer dump progress
## v0.13.755 — stabilize Kefir Hub forwarder title id
## v0.13.754 — allow zero identity BAAS placeholders
## v0.13.753 — persist account link diagnostics
## v0.13.752 — simplify user backup management
## v0.13.751 — link profiles with unique donor pool
## v0.13.750 — show cumulative install storage progress
## v0.13.749 — preserve BAAS payload during account linking
## v0.13.748 — delete linked users; backup all accounts before delete
## v0.13.747 — nand pack date, nicknames, delete only from list
## v0.13.746 — drop TE busy-wait that RESET-crashes dump
## v0.13.745 — nand pack list multi-select delete
## v0.13.744 — dump progress, pack library, no sticky toast
## v0.13.743 — dump_auto deletes one-shot temp files
## v0.13.742 — TE dump RESULT stats green/red
## v0.13.741 — TE dump_auto known-tree clear and reboot
## v0.13.740 — fix TE dump_auto combinepath on nested save files
## v0.13.739 — fix MTP haze split (SUPPORTED_EXT bound, FsSaveProxy members)
## v0.13.738 — fix Games split includes for title_nsp/ncm/save_paths
## v0.13.737 — include account_user.hpp in slim users_menu.cpp
## v0.13.736 — split USB queue, MTP haze, and File Viewer TUs
## v0.13.735 — split Games menu into game/ TUs
## v0.13.734 — split Settings menu into settings/ TUs
## v0.13.733 — USB auto-install balances usable space; live yellow storage bar
## v0.13.732 — split File Browser into filebrowser/ TUs
## v0.13.731 — split Users menu and group account domain
## v0.13.730 — auto TE dump for profiles & play hours; Ultrahand reopen hint
## v0.13.729 — restore profiles & play hours via TE auto
## v0.13.728 — manage user backups and sidebar navigation
## v0.13.727 — add portable account backup archives
## v0.13.726 — fix config folder deletion and descriptions
## v0.13.725 — defer network source connection
## v0.13.724 — browse Console Transfer HTTP sources
## v0.13.723 — simplify Console Transfer addresses
## v0.13.722 — Restore Backup TE apply link (no Horizon 0010 write)
## v0.13.721 — SnapshotOk accepts TE dump; auto-continue restore
## v0.13.720 — Restore Backup one user+NA, proven Replace only, no playtime
## v0.13.719 — Restore Backup Replace when NA unproven; keep ns/friends
## v0.13.718 — raw 0010 Undo snapshot, no unpack
## v0.13.717 — one Nintendo Account per baas, no other-console bootloop
## v0.13.716 — receive user backups over HTTP
## v0.13.715 — simplify user backup sharing
## v0.13.714 — add restore backup sources
## v0.13.713 — share Console Transfer sources over HTTP
## v0.13.712 — add Console Transfer hub placeholder
## v0.13.711 — match same-console restore by pack UID, do not clone
## v0.13.710 — same-console restore asks to replace existing profile
## v0.13.709 — restore onto existing NA; auto-write play hours via TE
## v0.13.708 — rollback path via TE; restore dialog before link
## v0.13.707 — load hekate after dump, do not stay in TegraExplorer
## v0.13.706 — name rollback script Undo_restore_if_wont_boot.te
## v0.13.705 — dump locked 0010 only via /startup.te
## v0.13.704 — mount romfs before reading dump.te
## v0.13.703 — launch TegraExplorer via hekate autoboot fallback
## v0.13.702 — hekate > payloads > tegraexplorer path in UI
## v0.13.701 — say Hekate Payloads TegraExplorer, not RCM
## v0.13.700 — dump locked 0010 via startup.te and TegraExplorer
## v0.13.699 — snapshot 0010 before restore with TE rollback
## v0.13.698 — warn then reboot; skip duplicate Nintendo Account on restore
## v0.13.697 — do not write 00F0 under Horizon; reboot after ACCOUNT stop
## v0.13.696 — honest play-hour backup restore and empty PlayEvent parse
## v0.13.695 — restore user backup with remapped play hours
## v0.13.694 — export account link, overwrite backups, dates and delete
## v0.13.693 — remap baas UID on restore and Icon link dots
## v0.13.692 — put Backup user under its own Options heading
## v0.13.691 — put Backup user and Restore Backup back in Options
## v0.13.690 — fix user backup stack overflow and L/R Backup Restore
## v0.13.689 — fix user backup restore build
## v0.13.688 — complete user backup and restore backup
## v0.13.687 — move Users Icon labels above tiles
## v0.13.686 — clarify account link safety
## v0.13.685 — separate Users Icon captions
## v0.13.684 — clarify account link save safety
## v0.13.683 — improve Users Icon labels
## v0.13.682 — fix false suspended-app gate in applet
## v0.13.681 — fix launch account-link prompt stacking
## v0.13.680 — remove account diagnostics and add Unlink
## v0.13.679 — MainMenu B back and launch account-link warning
## v0.13.678 — link only unlinked account profiles
## v0.13.677 — add manual account diagnostics
## v0.13.676 — probe administrator account linkage
## v0.13.675 — classify official accounts from save tokens
## v0.13.674 — Official vs Fake status and coloured labels
## v0.13.673 — streamline paste replacement confirmation
## v0.13.672 — label DBI translation file
## v0.13.671 — confirm file replacements
## v0.13.670 — romfs official link launch/Users UX
## v0.13.669 — LinkAllFromRomfsDonor live apply with safety filters
## v0.13.668 — LinkKind Fake vs Official status in Users
## v0.13.667 — romfs Kefir donor + LoadRomfsDonorPackage
## v0.13.666 — dump 0010 via copied file lists
## v0.13.665 — dump full account save 0010 /su
## v0.13.664 — add read-only official-link save layout probe
## v0.13.663 — match BaaS export by content
## v0.13.662 — fix NAS export condition precedence
## v0.13.661 — avoid nested NAS prefix loop in export
## v0.13.660 — draw System Tools focus text above border
## v0.13.659 — fix live DBI queue compatibility and metrics
## v0.13.658 — sync live DBI queue additions and metrics
## v0.13.657 — fix TimeStamp update method in dbi_menu & verify WSL build
## v0.13.656 — sync DBI backend queue selection
## v0.13.655 — continue DBI queue after skipped package
## v0.13.654 — improve queue scrolling and screen-off options
## v0.13.653 — keep System Tools focus border on top
## v0.13.652 — extend payload label scan
## v0.13.651 — unified manual firmware file and folder picker
## v0.13.649 — add downgrade warning QR dialog
## v0.13.648 — add manual firmware ZIP install
## v0.13.647 — add safe payload handoff and touch hold
## v0.13.646 — fix NAS filename matching in export script
## v0.13.645 — pair account save iterator fix with TegraExplorer
## v0.13.644 — read account link data from su directory
## v0.13.643 — fix account transfer script diagnostics
## v0.13.642 — auto-select NAND for account transfer
## v0.13.641 — restore account link status semantics
## v0.13.640 — fix compilation and verify NRO build and tests
## v0.13.639 — prepare offline official link transfer
## v0.13.638 — respect system timezone after clock sync
## v0.13.637 — export selected official account link
## v0.13.636 — query Horizon account link status
## v0.13.635 — clarify Nintendo Account link status
## v0.13.634 — use Horizon user creator and select SGDB games
## v0.13.633 — remove embedded user avatar presets
## v0.13.632 — make user backup paths UID-unique
## v0.13.631 — harden readable user backup export
## v0.13.630 — readable user backup export
## v0.13.629 — NAND dump never kills services; TegraExplorer dump.te fallback
## v0.13.628 — vector folder tiles, centered names, SGDB search keyboard
## v0.13.627 — avatar picker on create; delete user without killing account
## v0.13.626 — folder silhouette around Icon-layout previews
## v0.13.625 — image picker A selects; Fit Image only after zoom
## v0.13.624 — fix IsIconLayout compile, ReleaseWithInstall built
## v0.13.623 — never terminate ns when dumping play hours
## v0.13.622 — user UID in all layouts, folder mosaics, avatar crop
## v0.13.621 — backup user does not delete; delete requires hold A
## v0.13.620 — Users list layout, create-before-complete, avatar image picker
## v0.13.619 — fix avatar JPEG compile, ReleaseWithInstall built
## v0.13.618 — encrypt profile and play-hour restore in Hub via FS commit
## v0.13.617 — decrypt profiles and play hours on console, restore via TegraExplorer
## v0.13.616 — Users manager with grid, avatars, backup and delete
## v0.13.615 — mount account save via system_save_data_id
## v0.13.614 — import official Nintendo Account link instead of Linkalho-only stub
## v0.13.613 — Users menu with Linkalho-style offline account link
## v0.13.612 — explain sys-patch and FunControl instead of ErrorBox
## v0.13.611 — draw menu under the web-server overlay again
## v0.13.610 — Module Manager running counter and filter
## v0.13.609 — After reboot Enabled/Disabled uses the same green/grey
## v0.13.608 — Module Manager status dot green on, grey off
## v0.13.607 — drop Overlay memory, keep Sysmodule RAM
## v0.13.606 — Overlay memory bar and retry per-module RAM via debug
## v0.13.605 — Module Manager shows System RAM used and free
## v0.13.604 — drop Task Manager, show per-module RAM
## v0.13.603 — Task Manager lists every process using RAM
## v0.13.602 — Module Manager shows where RAM actually goes
## v0.13.601 — drop Software Network Downloads, match Tools icon set
## v0.13.600 — menu action icons, Games options, Tools tiles
## v0.13.599 — fix crash when nxlink exits with a file viewer open
## v0.13.598 — pass INVALID_HANDLE to svcGetSystemInfo
## v0.13.597 — Module Manager RAM via svcGetSystemInfo
## v0.13.596 — remote editor shows a closed-session overlay
## v0.13.595 — remote editor Save no longer closes the session
## v0.13.594 — Game Tools folder, system-tool stubs, Module Manager in Tools
## v0.13.593 — L3 launches the focused game from the Games list
## v0.13.592 — MTP Games drops Unmerged, localizes folder names
## v0.13.591 — MTP Games puts forwarders in their own folder
## v0.13.590 — replace dynamic_cast with virtuals (-fno-rtti)
## v0.13.589 — Games list uses the same badge pills as icon layouts
## v0.13.588 — keep NAND/SD move UI alive so Cancel works
## v0.13.587 — NAND/SD move shows the NCA being copied
## v0.13.586 — detect USB flash after MTP, open and close that drive
## v0.13.585 — do not hold USB as gadget when plugging a flash drive
## v0.13.584 — blue NAND/SD bars only on storages that hold the game
## v0.13.583 — MTP Games dumps — compatible, separate, or both
## v0.13.582 — USB plug identifies flash vs PC (open browser / auto MTP)
## v0.13.581 — restore only-if-newer auto-update for GitHub release
## v0.13.580 — Ask skip is a full-width row; Minus skips, Plus updates
## v0.13.579 — tap Ready — restart to relaunch Kefir Hub
## v0.13.578 — drop On demand; Ask dialog is Later / Skip this update / Update
## v0.13.577 — Auto-update is a folder in General, not a sidebar category
## v0.13.576 — stop the header N/M counter from jittering
## v0.13.575 — parse ### changelog headings; About update vs refresh notes
## v0.13.574 — Saves settings for filters, default location, WebDAV
## v0.13.573 — Network shows FTP and MTP toggles, then their folders
## v0.13.572 — Auto-update category at the top of Settings
## v0.13.571 — Y toggles boolean lines in the text editor
## v0.13.570 — toggle 0/1 with A; fix swkbd overflow after applet
## v0.13.569 — pass StopToken to silent auto-update ToFileAsync
## v0.13.568 — non-silent update uses the download transfer icon
## v0.13.567 — force auto-update from GitHub latest for mode testing
## v0.13.566 — silent auto-update with header progress and update modes
## v0.13.565 — text-file open menu before viewing; L/R+Up/Down in range legend
## v0.13.564 — stop auto-forwarder from stalling launch and duplicating HOME icons
## v0.13.563 — ask to save unsaved remote editor changes on B
## v0.13.562 — make the remote CodeMirror editor fill the window
## v0.13.561 — Edit on PC / phone from the file browser Options
## v0.13.560 — full-page CodeMirror editor for remote file edit
## v0.13.559 — expand an existing text selection from either edge
## v0.13.558 — stretch the line outline across the whole text selection
## v0.13.557 — file edit and paste reuse the existing remote input page
## v0.13.556 — paste multiline text from PC/phone at the editor cursor
## v0.13.555 — edit text files in the browser on PC or phone
## v0.13.554 — Create Folder in extract picker defaults to the archive name
## v0.13.553 — Enter in Direct Download sends the URL
## v0.13.552 — add Close picker to folder-picker Options
## v0.13.551 — picker Create Folder only, minus returns to extract options
## v0.13.550 — keep zip-extract row lines still, pad selection off the stripes
## v0.13.549 — smaller zip-extract checks, X/Y select, create folder in picker
## v0.13.548 — remove previous Kefir Hub HOME icon when installing a new one
## v0.13.547 — after zip app install, offer launch instead of the folder
## v0.13.546 — full-screen zip extract with tree, checkboxes, named folder
## v0.13.545 — use Manual video capture on forwarders, Auto crashed am
## v0.13.544 — treat OptionBox glyph as part of the caption again
## v0.13.543 — center OptionBox buttons, honest auto-forwarder notices
## v0.13.542 — enable forwarder capture, collapse stacked http(s), friendly URL errors
## v0.13.541 — label single-NRO zip action as install to /switch
## v0.13.540 — wrap OptionBox button labels, keep plus glyph fixed
## v0.13.539 — install a single zip NRO to /switch/stem, drop extract-to-root
## v0.13.538 — hide remote-input Paste on desktop, keep it on phone
## v0.13.537 — GameCard row, drop dead install screens, zip extract defaults
## v0.13.536 — fix nacp_util::GetName in forwarder_auto_install.cpp
## v0.13.535 — optimize auto-forwarder with fast-path check and avoid touching /Games folder
## v0.13.534 — clean legacy HBL forwarders and generate native KefirHub forwarder on the fly
## v0.13.533 — fix auto-forwarder thread lifecycle to guarantee threadClose and bypass in Application mode
## v0.13.532 — ensure 64-bit integer-safe NRO bounds and pre-body read validation
## v0.13.531 — validate contiguous OverrideHeap and check NRO code/BSS bounds
## v0.13.530 — clean NRO launch handoff and eliminate duplicate FS commit
## v0.13.529 — Fix List null controller dereference in Forwarder Editor and improve dual-pane D-Pad navigation
## v0.13.528 — Fix HBL loader NRO segment bounds, BSS zeroing, AppletType detection & full heap restoration
## v0.13.527 — Separate Network Downloads and Custom Link into dedicated bottom section in Software Menu
## v0.13.526 — Move Network Downloads and Custom Link to Software Menu, focusing Updater on Kefir and Firmware
## v0.13.525 — USB 3.0 indicator, graceful download cancellation, universal remote input (QR/Web) & direct NRO d...
## v0.13.519 — AppStore EntryMenu layout anti-overlap & instant launch state transition
## v0.13.518 — AppStore installed version display, RetroArch LibRetro Nightly 7z extractor & clean network teardown
## v0.13.516 — AppStore EntryMenu launch confirmation guard
## v0.13.515 — UPA-13 confirmed ROM database compatibility aliases
## v0.13.514 — UPA-11 GameCard theme roles and safe storage ratio
## v0.13.513 — UPA-10B localized UTF-8 MTP display names
## v0.13.512 — UPA-10A usable-title core and ASCII-safe NSP export naming
## v0.13.511 — UPA-09 forwarder editor touch/controller focus matrix
## v0.13.510 — UPA-08A discovery gate and UPA-08B raw FTP mutation adapter
## v0.13.509 — UPA-07B MTP delete/rename/directory operations mutation coverage
## v0.13.508 — UPA-07A MTP upload/final-close shared mutation policy integration
## v0.13.507 — UPA-06 shared homebrew mutation policy and complete Web success coverage
## v0.13.506 — UPA-05 playtime worker UI-thread isolation and race elimination
## v0.13.505 — UPA-04A MTP zero-byte upload support and patch shape verification
## v0.13.504 — UPA-03 centralized GitHub and direct URL validation
## v0.13.503 — UPA-02B GHDL ZIP type detection and safe non-ZIP destination
## v0.13.502 — UPA-02A GHDL operation identity, cancel and temp isolation
## v0.13.501 — UPA-01 GitHub downloader callback ownership and selection safety
## v0.13.500 — guard NRO heap after return
## v0.13.499 — auto-update and tools UI
## v0.13.496 — auto-update and tools UI
## v0.13.495 — pixel-balanced split & full-width justified 2-row footer layout
## v0.13.491 — fix flush thread stack overflow
## v0.13.490 — fix cstring include in static logger
## v0.13.489 — zero-heap static logging buffer and image load ordering
## v0.13.488 — increase sysmodule boot timeouts for slow SD cards
## v0.13.487 — stabilize microSD FS sync, background logger and NanoVG image decoding
## v0.13.469 — unify pending UI and updater work
## v0.13.468 — stabilize cURL shutdown
## v0.13.467 — version HTTP user agent
## v0.13.466 — stabilize menu header subheadings
## v0.13.465 — add multi-line text editing
## v0.13.464 — fix Homebrew search path build
## v0.13.463 — add local forwarder icon crop editor
## v0.13.460 — refine text editor controls
## v0.13.458 — add Homebrew settings
## v0.13.457 — decouple text viewer read-only viewport scrolling
## v0.13.456 — add streamed text viewer pager
## v0.13.454 — add localized NSP install diagnostic messages
## v0.13.453 — harden PFS0 NSP parser
## v0.13.452 — restore NRO loader affinity
## v0.13.451 — add custom NRO search paths
## v0.13.450 — harden NRO icon normalization
## v0.13.449 — add read-only NFS source
## v0.13.448 — limit NTP notifications
## v0.13.447 — harden ZIP extraction paths
## v0.13.446 — enable HOS clock auto correction
## v0.13.445 — persist NTP time via set:sys
## v0.13.444 — trace NTP synchronization on screen
## v0.13.443 — write NTP time via system-user service
## v0.13.442 — prevent file browser association crash
## v0.13.440 — add install queue package skip and queue cancel controls
## v0.13.439 — fix immediate NTP time synchronization
## v0.13.438 — add USB 3.0 Kefir toggle
## v0.13.437 — finalize text editor delivery
## v0.13.433 — Add TICO core launchers and forwarders
## v0.13.432 — Improve ROM forwarder titles and validation
## v0.13.431 — Fix missing includes in file_viewer and format specifier
## v0.13.430 — Interactive screensaver controls via analog sticks
## v0.13.429 — Document delivery and audit
## v0.13.413 — Rebind the web server after a sleep, close it if the address moved
## v0.13.412 — Plan where every queued title lands before installing any of it
## v0.13.404 — End the header gap before NAND, not before SD
## v0.13.403 — The header gap is a slot, sized by what the title leaves
## v0.13.402 — Time the shutdown too, and account for frames
## v0.13.401 — Sub heading moves to the header, marked rows get a background, boot timings
## v0.13.400 — Measure the footer hint row when it changes, not every frame
## v0.13.399 — Fling scrolling, wrap past the Updater's captions, select-and-advance
## v0.13.398 — Wrap every list, unmerge the footer, mark list rows like the file browser
## v0.13.397 — Game details page, DBI-style list rows, nxlink that stops on a dead socket
## v0.13.386 — Drop the unused half of the curl Api, fold i18n's three lookups
## v0.13.385 — One case-insensitive path compare instead of five
## v0.13.384 — One copy of the firmware version logic, with tests
## v0.13.383 — Delete dead code the compiler could never warn about
## v0.13.382 — Mount every selected folder, not just one
## v0.13.381 — A ".." row, and Mount acts on what the cursor is pointing at
## v0.13.380 — Mount the highlighted folder, not the folder you are standing in
## v0.13.379 — Root means the same thing in the browser and over HTTP
## v0.13.378 — Survive a failed reconnect; reset the recovery guard per session
## v0.13.377 — Retry a failed post even when the endpoint reports healthy
## v0.13.376 — Retry a stalled post in place; open endpoints like libusbhsfs does
## v0.13.375 — Align USB posts to the real max packet size, not a hardcoded 512
## v0.13.374 — Never hold an MTP data phase open across a reader stall
## v0.13.373 — Never cancel an MTP transaction; size the request to the reader
## v0.13.372 — Stop RemoveDevice from nulling the sd card's devoptab slot
## v0.13.371 — Fix yati shutdown deadlock and settle MTP cancels properly
## v0.13.370 — Clear USB endpoint halts instead of tearing the MTP link down
## v0.13.369 — Stream MTP file reads in one long transaction
## v0.13.368 — Survive MTP session drops mid-install, halve read transactions
## v0.13.367 — MTP host self-heal, devoptab NULL-hole crash, honest listing errors
## v0.13.366 — One mount, shared by FTP, HTTP and MTP
## v0.13.365 — Close remaining MTP host races and read-path gaps
## v0.13.364 — Rewrite MTP host transport, protocol and session handling
## v0.13.363 — Fix folder mounting over FTP and HTTP
## v0.13.362 — Fix USB transfer handling - eventWait check, retry with delay, memcpy UB fix, re-enable pre-fetch
## v0.13.361 — Strict MTP interface filter (only ifClass 0x06), revert pre-fetch to prevent USB system crash
## v0.13.360 — Fix MTP mount path (keep trailing slash) + pre-fetch root entries during scan
## v0.13.359 — Store dir path in Dir struct and use full device-qualified paths for stat() fallback on DT_UNKNOWN
## v0.13.358 — Add stat fallback for DT_UNKNOWN devoptab entries in fs.cpp
## v0.13.357 — Fix USB DMA read buffer post size to prevent endpoint packet babble
## v0.13.356 — Strip device prefix in ResolvePathToHandle to fix empty MTP directory listing
## v0.13.355 — Fix MTP Host empty directory listing and active session disconnect probe
## v0.13.291 — Show confirmation prompt when cancelling MTP installations (via B or Stop).
## v0.13.290 — Optimize Stream buffering for MTP installs to prevent speed drop and USB timeout.
## v0.13.288 — feat: close sidebar with START button
## v0.13.287 — fix: resolve MTP stall by returning short stream reads early
## v0.13.286 — fix: resolve Yati member compilation error in InstallNcaInternal
## v0.13.285 — fix: replace scary error dialog with friendly notification on install cancel
## v0.13.284 — fix: resolve deadlock on cancel, add verbose condvar logging to installer
## v0.13.283 — fix: replace if with while for condvarWait to fix 3% install hang
## v0.13.282 — fix: MTP install hangs on EOF, block input while expanded, Stop button, dynamic badge
## v0.13.281 — fix: robust handling of EOF and short reads during MTP install
## v0.13.253 — feat: R/W speed graph, install-hang fixes, ReleaseWithInstall builds
## v0.13.252 — fix: send auth across redirects, wrap-around browsing, cursor-first metadata
## v0.13.251 — fix: WebDAV Digest auth, friendly network errors, persistent source badges
## v0.13.250 — fix: robust WebDAV/HTTP listing and root-level source management
## v0.13.249 — fix: validate remote sources and WebDAV sync
## v0.13.248 — fix: verify install no-sleep guard
## v0.13.247 — fix: clarify game badges and storage totals
## v0.13.246 — fix: scroll long HB menu card titles
## v0.13.245 — fix: align game badges and detail controls
## v0.13.244 — fix: mark and filter unavailable game records
## v0.13.243 — fix: crashes when browsing HTTP/WebDAV sources
## v0.13.242 — feat: stack game content badges vertically
## v0.13.241 — feat: allow changing network source protocol
## v0.13.240 — feat: refine games selection and details UX
## v0.13.239 — feat: rebuild games UI around DBI details
## v0.13.238 — feat: harden game details and applet mode
## v0.13.237 — feat: implement WebDAV, FTP, HTTP browsing, System Root and status badges
## v0.13.236 — feat: handle location name collisions and show protocol type in source lists
## v0.13.235 — feat: accept any HTTP response code from 200 to 599 as successful connection test
## v0.13.234 — feat: simplify HTTP connection testing using HEAD request without PROPFIND
## v0.13.233 — feat: preserve focus index and scroll offset on SettingsMenu and SourceEditMenu focus restore
## v0.13.232 — feat: show all configured network locations in file browser mount picker and restrict non-smb bro...
## v0.13.231 — feat: implement sidebar context menu, connection testing, and auto url copying for network sources
## v0.13.230 — feat: fix translation removal target lock, filter empty translations, and add auto language switc...
## v0.13.225 — feat: wrapping submenus, translation package updates, language/region filtering
## v0.13.223 — fix: resolve Use-After-Free crash on add network source by replacing PopToMenu with Pop
## v0.13.222 — feat: implement single source of truth for saves sync and support FTP and local locations
## v0.13.221 — feat: integrate WebDAV saves config as a shared source and fix swkbd crash
## v0.13.220 — feat: WebDAV, FTP, HTTP storage sources, settings management, and battery layout fix
## v0.13.219 — Harden SMB network sources
## v0.13.217 — Integrate localized sysmodule catalog
## v0.13.216 — Correct and validate sysmodule catalog
## v0.13.215 — Complete sysmodule catalog with newly verified modules and overrides
## v0.13.213 — feat(modules/settings): UX improvements to Module Manager and Settings touch navigation
## v0.13.212 — Fix install-queue UI bugs (id 57-59) in PC Install (USB)
## v0.13.210 — DBI Backend protocol extension - file sizes in LIST response
## v0.13.209 — MTP copy progress popup, WebDAV settings folder, File Browser Sources picker
## v0.13.208 — MTP Saves drive (S6 / id 37) - read-only decrypted game saves
## v0.13.207 — Review fix for S5 - revert MTP toggle when haze::Init() fails
## v0.13.206 — MTP storage options
## v0.13.205 — Cheats i18n, ProgressBox overflow, selection outline clipping, zip name sanitize
## v0.13.204 — Review follow-ups for S1/S4 - stdio auto-sync upload and cancel result code
## v0.13.203 — Shared /dumps constant and honest Location/Sync tooltips
## v0.13.202 — Sync resilience - continue past failed transfers, summarise at end
## v0.13.201 — Fix auto-sync after Backup ignoring the selected backup location
## v0.13.200 — Fix DeletePath/MovePath directory regression from Step 14.4
## v0.13.193 — Fix web.cpp duplication and restore LF line endings
## v0.13.192 — Decompose web.cpp and extract web_http and web_screenshots
## v0.13.191 — Mark Phase 11 complete, document comment-restoration fixes for Phases 10-11
## v0.13.190 — Decompose cheat game select menu and isolate CheatDownloadMenu
## v0.13.181 — docs(walkthrough): add missing entries for (Step 6.2) and move to top
## v0.13.180 — docs(walkthrough): add missing entries for (Step 6.2) and move to top
## v0.13.146 — Complete MTP, module manager, and WebDAV updates
## v0.13.133 — Allow concurrent web server clients and show install progress from any device
## v0.13.131 — Fix case-sensitive NCA/tik/cert lookup causing install failures (YatiNcaNotFound)
## v0.13.130 — Use NanoVG vector arrow for submenu indicator, remove arrow from PC Install (USB)
## v0.13.129 — Fix compilation error due to extra closing brace in filebrowser.cpp
## v0.13.128 — Reorder context menu options and add submenu arrows
## v0.13.127 — Integrate WebDAV auto-sync, update battery charging indicators, and cleanups
## v0.13.126 — Fix Themezer exit crash, hide ZL/ZR from legend, and add Down/Right auto-navigation
## v0.13.125 — Fix early namespace closure compile error in filebrowser.cpp
## v0.13.124 — Reorganize file options menu to Windows-like layout and translate descriptions
## v0.13.123 — Update Themezer page navigation, layout, and Ukrainian translations
## v0.13.122 — Add Connection Options sidebar to Tools menu for Plus button
## v0.13.121 — Support deleting Favorite themes via R3 in Themes menu
## v0.13.120 — Merge ZL/ZR legend for theme pages and fix Screenshot translation
## v0.13.119 — Reorganize Software -> DBI menu and move PC Install (USB) to it
## v0.13.118 — Fix sorting tooltips and Show Hidden translations in Homebrew menu
## v0.13.115 — Fix status bar overlapping and vertically align NAND/SD labels
## v0.13.114 — Implement saves synchronization with remote WebDAV and auto-sync after backup
## v0.13.113 — Add SMB network storage mount support and play via NXMP
## v0.13.112 — Implement Phase 8.1 - Play with NXMP from File Browser
## v0.13.111 — Fix thread safety and cleanup in USB and DBI menus
## v0.13.110 — Add support for DBI Backend USB protocol
## v0.13.109 — Fix install_location migration bug for fresh users
## v0.13.108 — Set default install location to Auto and support all 5 location priority types in Settings UI
## v0.13.107 — Implement Target layout and Tags filters in Themezer client
## v0.13.106 — Fix thread safety in BackgroundInstaller, optimize status bar storage polling, and resolve audit...
## v0.13.105 — Support background MTP game installation from any interface screen
## v0.13.104 — Support for custom install location priority and free space reserve threshold
## v0.13.103 — Support for NACP v2 format introduced in FW 20.0+
## v0.13.102 — Reorganize status bar UI with dual NAND/SD storage capacity bars and static charging icon
## v0.13.101 — Add List layout to Saves, move Layout to Save Options top level, fix selection checkmark on icon
## v0.13.100 — Fix sidebar option label scrolling - scroll label text instead of value
## v0.13.99 — Localize all hardcoded strings in settings_menu, add Save/Fan/Kefir translations to i18n
## v0.13.98 — Ignore 0x1002 error when deleting translations, localize Translate Interface title
## v0.13.95 — Auto-crop and upscale icon symbols, use exact background color from example
## v0.13.94 — Sliced Tools icons with transparency, render dark icon background in C++ code
## v0.13.93 — Slice and apply new Tools icons, improve HoldConfirmBox dynamic fonts and line spacing, translate...
## v0.13.91 — Merge App Store and Software menus under Software, update uk.json translations and always render...
## v0.13.89 — Center Install button in changelog, translate all changelog text, refactor fast scroll through Li...
## v0.13.88 — Move Install button to center to prevent overlap with Cancel, translate all changelog labels, fix...
## v0.13.87 — Show changelog preamble only when target version description is not found in the file
## v0.13.86 — Require full scrolling of Kefir changelog to focus and activate Install button, and fix version d...
## v0.13.85 — ZL/ZR fast scrolling (except settings), and Left/Right page-middle navigation in List View
## v0.13.84 — Always show Kefir changelog, skip auto-skip logic, and handle download cancellation without showi...
## v0.13.83 — Fix double keydown events using stopImmediatePropagation, move AppendConfirmModal out of header,...
## v0.13.82 — Fix confirmation double execution, deduplicate confirm modals, and resolve title service lifecycle
## v0.13.80 — Fix infinite recursion on START button press in MenuBase
## v0.13.76 — Fix file item focusing scroll-margin-top, add Backspace parent folder navigation, and implement i...
## v0.13.74 — Fix compilation error in PopupList B1, remove dead SanitizeRelativePath B2
## v0.13.73 — Fix resource leaks A2, remove dead ShareMode A3, optimize title initialization A4
## v0.13.72 — Context menu for Web Server & Screenshots in Tools, change route to /album, fix upload queue esca...
## v0.13.71 — Fix compilation errors: restore namespace structure and use explicit type in progress box lambda
## v0.13.70 — Fix review comments: deduplicate progress box, case-insensitive screenshots, document P2-7, facto...
## v0.13.69 — Clean up code and expand HTTP read limit to 16 KB
## v0.13.68 — Optimize web server memory allocation using constexpr string_view
## v0.13.67 — Fix active download cancellation and cleanup dead code
## v0.13.66 — Implement Screenshot Gallery and integrate into Tools menu
## v0.13.65 — Remove obsolete Share Images and image/gallery page builders dead code
## v0.13.64 — Extract NAND/SD target heuristic into ChooseInstallTarget helper
## v0.13.63 — Add deletion logging in HandleDelete
## v0.13.62 — Convert ScanDirectoryRecursive to DFS and increase server thread stack to 128KB
## v0.13.61 — Make ProgressBox pointer and mute flag atomic to eliminate data races
## v0.13.60 — Add socket stream timeouts and cancellation checks
## v0.13.59 — Fix image share and gallery routing
## v0.13.54 — Web SPA, sequential transfer queue, direct game installation, battery anim, touch stop, dynamic s...
## v0.13.53 — feat(web): drag-prevention and list-container element ID fixes
## v0.13.52 — feat(web): custom checkbox styling, keyboard navigation (ArrowKeys, Space, Esc, Delete, Enter), a...
## v0.13.15 — Improvement: Invert panning controls and fix themezer ProgressBox/LazyImage collisions
## v0.13.14 — Improvement: Add solid overlays and 3s hold confirm actions
## v0.13.12 — Feature: Add Image Theme Creator
## v0.13.11 — Combine L/R and ZL/ZR buttons in the bottom legend bar as single items with slash dividers and co...
## v0.13.10 — Trigger actions on release (UP) instead of press (DOWN) for widget buttons, and filter out chord/...
## v0.13.9 — Reduce spacing in button legend bar, display L/R always, shorten Bezier label, and enable horizon...
## v0.13.8 — Implement Bezier Helper Mode as an auxiliary overlay (green curve) that preserves the original cu...
## v0.13.7 — Implement 3-point Bezier Easy Curve Mode and fix graph sensor marker drawing layers
## v0.13.6 — Query actual hardware fan speed level using fanControllerGetRotationSpeedLevel in sysmodule
## v0.13.5 — Fix fan control IPC conflicts, add SoC temperature sensing, and implement smooth fan inertia visu...
## v0.13.3 — FunControl sysmodule version tag (not a Kefir Hub release)
