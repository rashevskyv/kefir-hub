# Audit 2026-10-02: findings from writing the user docs

Seven agents read the code area by area to write `docs/site/`. This file lists what they found along the way.
None of it has been verified on hardware, and line numbers are as of v0.13.936. Questions for the docs
(`<!-- TODO(verify) -->` in the pages) are listed at the end.

## High: security, data loss, ban risk
- **The web server has no login and no path limit.** `web_file_routes.cpp:407` `HandleDelete` and the upload, download and list routes
  accept any path, `path=/` included. Anyone on the LAN can read or recursively delete the whole microSD card or USB drives,
  even when the user shared only one folder (Console Transfer "Share Save Backups").
- **Installs bypass the install-enable switch.** USB, MTP, FTP and web installs never call `App::GetInstallEnable()`, so the switch
  and its ban warning are skipped. Call sites: `install_share.cpp:340`, `dbi_menu.cpp:62`, `install_stream_menu_base.cpp:69-96`,
  `web_upload_routes.cpp:255`. Only the File Browser, Ownfoil and gc_menu check it.
- **The downgrade dialog opens with confirm focused.** In `kefir_downgrade_box.cpp:39-41` one press of **A** (or **+**) starts a
  firmware downgrade. Cancel should be focused.
- **`/firmware` gets deleted.** If the user manually picks `/firmware`, it equals FIRMWARE_DEST and is deleted after install
  (`kefir_firmware_ops.cpp:395-400`), which contradicts the comment there. A firmware download also silently deletes an existing
  `/firmware` (`kefir_firmware.cpp:267-268`).
- **Nxlink is on by default** (`app.hpp:447`): any host on the LAN can push an NRO and launch it.
- **Verification-skip options default to On.** Skip NCA hash, skip RSA header verify and lower system version are all On
  (`app.hpp:532,533,539`), while the help text says "recommended to keep this disabled" (`app_display_options.cpp:121-155`).
- **Profile backup can reboot into TegraExplorer without warning.** When a save is locked, Backup does this
  (`users/users_nand.cpp:21-24, 58-90`). Also, `Result_SaveSyncFailed` can never show because the result of
  `TrySnapshotRawSystemSaves` is ignored (`users_nand.cpp:~166`).
- **Unlink removes real Nintendo Account links without warning** (`users_profile.cpp:~395-420`). The list also does not tell real
  links from donor links (`users_menu.cpp:143`).

## Medium: broken or misleading behaviour
- **Game card install and XCI dump are unreachable.** `gc_menu.cpp` and `gc_menu_ops.cpp` are compiled, but no menu opens them.
- **Undo after a save restore cannot be followed.** The instructions say "File Browser → recovery.zip → Restore", but "Restore save
  data" is shown only for DISA files and `RestoreSaveFile` accepts only zip or folders (`filebrowser_options.cpp:188` vs
  `filebrowser_ops.cpp:46-58`; the instructions are at `save_menu_ops.cpp:292` and `filebrowser_ops.cpp:157`).
- **"Include remote backups" does nothing visible.** The remote names are never passed to the picker, so the cloud icon never
  appears (`save_restore_route.cpp:111/407`).
- **"Auto backup on restore" is a dead toggle.** `GetSaveAutoBackupOnRestore` is never read.
- **"Create backup if newer" and "Restore all" ignore Location.** Both always use `/dumps` (`save_menu_actions.cpp:152`,
  `save_game_group.cpp:126`).
- **Restoring into an existing slot asks for confirmation twice** (`save_menu_target.cpp:140` + `save_menu_ops.cpp:351`).
- **"Open in file browser" for a live save always fails** with "not supported" (`save_menu_actions.cpp:90/116`).
- **Tabs override the Show-saves filters** (`save_menu_scan.cpp:111-123`).
- **B with apps selected exits Kefir Hub.** `main_menu.cpp:246/293` copies B=Exit onto the Homebrew tab, so `homebrew.cpp:56-62`
  ("B clears selection") never runs.
- **Sorting.** File Browser alphabetical ascending and descending are swapped (`filebrowser_scan.cpp:419-423`). Homebrew
  alphabetical sort ignores Order (`homebrew_scan.cpp:502`).
- **ZR combos jump to the end of the list first** (`list.cpp:445` + `filebrowser_view.cpp:399`).
- **Downgrade fix fails with a generic message.** It refuses to stage when `/startup.te` exists, which the 8GB DRAM toggle can
  leave behind, and then reports only "Firmware update failed" (`kefir_firmware_ops.cpp:160`, `kefir_ops.cpp:316`).
- **Delete hint is wrong.** It says games are deleted "and their data", but saves are kept (`game_menu.cpp:323`).
- **`DeleteAllCheats` always returns 0**, so the success message shows even when nothing was deleted (`cheats_ops.cpp:250-298`).
- **Random game can pick an unavailable entry** (`game_menu.cpp:162`).
- **"Skip package" may cancel the whole USB transfer** (`dbi_session.cpp:73-81`, unverified).
- **"USB transfer stream" resets to On at every start** (`app.hpp:546`, `file=false`).
- **Turning Logging off and on mid-session wipes the log** (`log.cpp:163` O_TRUNC). Enabling it from Settings shows no slowdown warning.
- **USB read-only does not remount drives already mounted** (`app_network_settings.cpp:135`).
- **"Mount MTP" without a cable** says "Failed to start MTP", but the setting stays on (`install_share.cpp:322`).
- **Dropping an installable file at the top level of FTP installs it instead of saving it** (`ftpsrv_vfs.cpp:389`).
  Check this against what users expect.
- **Web page "Download Selected" loads whole files into browser memory** (`web_pages/folder.hpp:163`).
  `addSelectedToDownloadQueue` is defined twice (`:138`, `:394`).
- **mDNS has no conflict handling.** Two consoles both answer `kefir.local`, and the QR code always uses `kefir.local`
  (`web_mdns.cpp:182`).
- **Profile rename cuts at 31 bytes**, which can split a Cyrillic character (`users_profile.cpp:61`, `account_user.cpp:163`).
- **App Store feedback is sent over plain HTTP** with no result shown (`appstore_entry_menu.cpp:54-74`).
- **Some caches still live in the legacy `/switch/sphaira/cache`** (`auto_update.cpp:180-182`, `appstore.cpp:25-27`,
  `themezer_internal.hpp:19`).

## Stale texts, wrong paths in messages
- `app.hpp:567` INSTALL_DEPENDS_STR: "Menu (Y) -> Advanced -> Install options -> Enable". The real path is Tools → Settings → Install.
- `haze_install_proxy.cpp:35`: "Please launch MTP install menu", but that menu was removed in v0.13.537.
- `app_display_options.cpp:26-31` ShowTitleModeHelp is not `_i18n` and gives a wrong path ("Install & Share -> Web Server").
- `main_menu.cpp:~72,76`: "Tools → Users". The real path is Tools → Tools → Users.
- `tools_menu.cpp:205`: Console Transfer promises "installed content", but no installed titles are shared.
- `cheats_menu.cpp:44`: "Full KefirUpdater cheats pack", but the URL points to HamletDuFromage switch-cheats-db.
- `settings_categories.cpp:473`: popup "Already installed behaviour" vs row "Skip if already installed".
- The same install options have two label sets: Settings vs the `app_display_options.cpp:46-155` sidebar.
- `file_picker_draw.cpp:179`: the hint "L/R flips"; in the image viewer the D-pad flips and L/R rotates.
- `filebrowser_options.cpp:558`: Compress to... shows the extract prompt.
- `filebrowser_scan.cpp:167-173`: the USB/MTP notification fires on every root scan, and says FAT32/exFAT where `filebrowser_share.cpp:272` says NTFS too.
- `dbi_draw.cpp:167`: "USB session failed" is shown for any failure, local queue included.
- `settings_themes.cpp`: "Mario BG Dark" is described as Modern.
- `homebrew_ops.cpp:41-47`: Edit name/icon shows forwarder rows that do nothing.
- `filebrowser_sources.cpp:239`: "Image System memory/microSD" appears in the Mount list without god mode.
- Typos at `app_display_options.cpp:140` ("distruction") and `~146` ("recommened").
- `file_picker_draw.cpp:34`: missing comma (`"aac" "ac3"`).

## Translation gaps
- **Mojibake.**
  - `users_menu.cpp:139` separator: D0 92 C2 B7.
  - `cheat_download_menu.cpp ~415-430` PreviewCheat: CP1251 double encoding.
  - `en.json:2704`: duplicate key "Computer connected â MTP started".
- **Not `_i18n`: screen titles.**
  - `kefir_menu.cpp:22`, `settings_kefir.cpp:244`, `settings_software.cpp:129`, `settings_themes.cpp:118`, `settings_dbi.cpp:99`, `settings_fancurve.cpp:63`.
  - Cheats menus: `cheats_menu.cpp:220`, `cheat_game_select_menu.cpp:102`, `cheat_download_menu.cpp:132`, `cheat_files_menu.cpp:252`, `cheat_content_menu.cpp:30/314`.
- **Not `_i18n`: dialogs and messages.**
  - Firmware install confirm, including "Do not power off" (`kefir_ops.cpp:255-257`), plus more in `kefir_ops.cpp`, `kefir_firmware*.cpp`, `kefir_draw.cpp` and `kefir_changelog*.cpp`.
  - Wi-Fi dialogs (`wifi_menu.cpp:410-535`), fan curve texts, translate screen (`settings_translate.cpp:159,181,186`).
  - Cheat prompts and notices (`cheat_game_select_menu.cpp:335-380`, `cheat_files_menu.cpp:389-465`, `cheat_download_writer.cpp:42-267`).
  - `settings_kefir.cpp:32-34` hold warning; `ghdl_api.cpp:464,502-504`; `filebrowser_options.cpp:392,536`; `save_menu.cpp:175`; `save_menu_target.cpp:140`; `game_details.cpp:286/291`.
  - Save type names "Device/BCAT/Cache/System BCAT" (`save_paths.cpp:204-210`).
- **Default MTP drive names** are always English on the PC (`haze_helper.cpp:379-426`).
- **The web pages** (file manager, album) are English-only.
- **uk.json.**
  - "Options" and "Settings" both translate to «Налаштування».
  - "HB Menu" is «Головне меню», which is wrong.
  - «Теки» vs «папка».
- **Stale "Sphaira" keys in en.json** (logs warning, hbmenu restore, replace hbmenu). Probably dead.

## Dead code (unreachable from any menu)
- Per-profile backup/restore and the user backup library: `users_profile.cpp:78` ConfirmBackup,
  `users_restore.cpp:353`, `users_manage.cpp:122`, `users_restore_remote*`, `users_manage_backups/ops`.
  Console Transfer "Share User Backups" and Delete → "Backup all accounts" produce backups that nothing can restore.
- `SaveHubMenu` (`save_hub_menu.cpp`).
- CheatSlips (`CheatslipsLoginMenu`, `cheat_download_menu.cpp:84`).
- GitHub releases browser `gh::Menu` (`ghdl.cpp`): `kefir_links.cpp:205-219` never creates its entries.
- `gc_menu` (see Medium).
- `web_pages/folder.hpp` `createZip`.
- Settings duplicates: "Kefir Hub theme options" repeats Theme and 12 Hour Time (`settings_categories_items.cpp`).
- No UI to turn the startup link reminder back on (config key `account_link_prompt_skip`).

## Open questions in the docs (TODO(verify), 36 in total)
Grep `TODO(verify)` in `docs/site/en/`. The main ones:
1. Does the Kefir package install Kefir Hub as `/hbmenu.nro`?
2. What happens online, in eShop and with cloud saves for a donor-linked profile? Is there a ban risk?
3. Does Windows need a USB driver (Zadig/libusbK) for PC Install? Which DBI Backend Qt version, and where to download it?
4. Which overlay does Kefir ship for toggling cheats in game?
5. What are Kefir's out-of-the-box values for the Kefir Settings switches, and what is the 40MB patch for?
6. What does the user see on the reboot after a Kefir update, and in TegraExplorer during the downgrade fix?
7. Does Dump work for content that is only on the game card?
