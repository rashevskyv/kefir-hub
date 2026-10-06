# User docs coverage

Map of user-visible features to the page in `docs/site/en/` that documents them. It was built from the code
in October 2026 (agents read `sphaira/source/ui/menus/*` and the `"..."_i18n` strings).
Status values: **doc** = documented, **partial** = documented with gaps (see TODO(verify) in the page),
**none** = not documented, with the reason.
When a feature ships or changes, update its row and its page (skill `.agents/skills/update-docs/SKILL.md`).

## Features under review
The owner may remove these. Keep them documented as they are, but do not expand them or build other pages
around them. When a decision is made, follow the skill: "removed" or drop it from this list.

| Feature | Pages and places that mention it | Since |
|---|---|---|
| Console Transfer (Tools hub tile, Install & Share entries, profile packs) | console-transfer, index (Tools tile table), users (Manage Backups, receive), saves (Share Save Backups), video 07, site /usage, /hbl | 2026-10-02 |

## Settings, troubleshooting
| Feature | UI | Code | Page | Status |
|---|---|---|---|---|
| Settings navigation, entry points | Tools → Settings | settings_menu.cpp, tools_menu.cpp:202 | settings | doc |
| Auto-update (when, update now, skipped version) | General → Auto-update | settings_categories_items.cpp | settings#update-kefir-hub | partial: check timing |
| Language, scroll speed, 12h time, clock sync, logging, restart/exit | General | app_settings.cpp, app_network_settings.cpp, log.cpp | settings#general | doc |
| About box | General → About | about_box.cpp | settings#update-kefir-hub | doc |
| Homebrew search paths, replace hbmenu on exit | Homebrew | settings_categories_items.cpp, app_settings.cpp:362 | settings#homebrew | doc |
| Forwarder defaults | Homebrew → Forwarders | settings_categories_items.cpp | settings#forwarder-defaults | doc |
| Saves options | Saves | settings_categories.cpp:280 | settings#saves → saves | doc (names + link) |
| Theme, waves, theme options | Appearance | settings_categories.cpp | settings#appearance → themes | doc |
| FTP, MTP, MTP storages, Nxlink | Network | app_network_settings.cpp, app_mtp_settings.cpp | settings#network → sharing | doc (link) |
| Network locations, USB storage read-only | Sources | settings_sources.cpp | settings#sources | partial: remount behaviour |
| Install options (22) | Install | settings_categories.cpp:440 | settings#install → install/index | doc (names + link) |
| Screen off during installs | Install | settings_categories_items.cpp | settings#screen-off-during-installs | doc |
| Dump options | Dump | settings_categories.cpp | settings#dump | doc |
| Config location, legacy migration, reset | — | app_paths.hpp, app_startup.cpp:265, app.cpp:333 | settings#where-settings-are-stored | doc |
| errors.txt, log, crash reports | — | log.cpp, dbi_draw_transfer.cpp:391 | troubleshooting | partial: crash report coverage |
| Frozen UI during transfer | — | progress_box.cpp:29 | troubleshooting | doc |
| Common error strings | various | — | troubleshooting#common-problems | doc |
| Bug reporting | header, About | menu_base_draw.cpp:439 | troubleshooting#report-a-bug | doc |

## Users, console transfer
| Feature | UI | Code | Page | Status |
|---|---|---|---|---|
| Profile list and link status | Tools → Tools → Users | users_menu.cpp | users#the-profile-list | doc (official and donor links look the same in the UI) |
| Create, rename, avatar, delete | Users options | users/users_profile*.cpp | users | doc; rename: max length unclear |
| Per-profile backup/restore, user backup library | **unreachable** | users_profile.cpp:78, users_restore*.cpp, users_manage*.cpp | — | none: no menu reaches it (dead code) |
| Link / unlink Nintendo Account (donor) | Users options | account/account_link_*.cpp | users#link-profiles-to-a-nintendo-account-offline | partial: online/eShop risk |
| Startup link reminder | launch | main_menu.cpp:51 | users#the-link-reminder-at-startup | doc |
| Profiles and play hours backup/restore (TegraExplorer) | Users → CONSOLE MOVE | users/users_nand*.cpp, account/nand_transfer*.cpp | users#back-up, users#restore | doc; unfinished dump choices documented; users-unfinished-dump: [USER] Switch shot pending |
| Manage backups, receive from another console | Users → Manage Backups | users_nand_library*.cpp, install_share.cpp:374 | users#manage-backups, console-transfer | doc |
| Undo profile restore | hekate → TegraExplorer | account_restore.cpp | console-transfer#undo-a-profiles-restore | doc |
| Console Transfer server and shares | Tools → Console Transfer | install_share.cpp:57-238 | console-transfer | doc |
| Share Save Backups | Console Transfer | save/save_paths.cpp:357 | console-transfer#move-save-backups | partial: receiving via HTTP location |
| Share User Backups | Console Transfer | install_share.cpp:362 | console-transfer#what-you-can-move | partial: nothing restores them |

## Install
| Feature | UI | Code | Page | Status |
|---|---|---|---|---|
| Install enable switch + ban warning | Settings → Install | settings_categories.cpp:137-168 | install/index#enable | doc (USB/MTP/FTP/web skip the switch: bug) |
| Methods, formats, Applet Mode NSZ limit | — | filebrowser_assoc.hpp:43, yati.cpp:403 | install/index#methods, #formats | doc |
| Install location (5 modes), auto balancing, reserve space, per-package target | Settings → Install; queue options | install_plan.hpp, dbi_menu_options.cpp | install/index#location | doc |
| Install options (3 groups) | Settings → Install | settings_categories.cpp:436-514 | install/index#install-options | doc |
| Install one/several files, recursive folder | File Browser | filebrowser_view.cpp, filebrowser_recursive_install.cpp | install/sd-card | doc |
| Install queue: review, buttons, progress, skip, cancel, summary, errors.txt | Install queue | dbi_*.cpp | install/sd-card#queue | doc |
| Minimize to badge | R3 | app_frame.cpp:169-230 | install/sd-card#minimize | R3 from any menu since v0.13.962 (was L3); Package target moved to L3 |
| Screen off during install | − | dbi_menu_options.cpp:249-334, screensaver.cpp | install/sd-card#screen-off | doc |
| PC Install (USB): DBI Backend/Qt, ns-usbloader, Fluffy; live queue; USB speed badge | Tools + → PC Install (USB) | dbi_usb.cpp, yati/source/usb.cpp, dbi_plan.cpp | install/usb | partial: backend version for live queue |
| MTP install: auto start, storages, progress, cancel, .nro → /switch | PC Explorer | app_usb.cpp, install_stream*.cpp, haze_helper.cpp | install/mtp | partial: Windows-side behaviour |
| Dump game card to NSP | Games → Dump | game_menu.cpp:217-244 | install/gamecard#dump | partial: card-only content |
| Game card install / XCI dump menu | **unreachable** | gc_menu.cpp, gc_menu_ops.cpp | install/gamecard | none: no entry point (says "not available") |

## Games, saves, cheats
| Feature | UI | Code | Page | Status |
|---|---|---|---|---|
| Games list, badges, launch, random | Game Tools → Games | game_menu.cpp | games#read-the-list, #launch-a-game | doc |
| Game details (Content/Tickets/Saves tabs, component actions) | A on game | game_details*.cpp | games#see-a-games-details | doc |
| Move NAND↔SD | + / Game Actions | title_move.cpp, game_details_ops.cpp | games#move-a-game-between-nand-and-microsd | doc |
| Dump NSP, dump locations, dump options | + → Dump | game_ops.cpp, dumper.cpp | games#export-a-game-as-nsp-dump, #dump-options | doc |
| Create repack | Details + | game_details_ops.cpp:190 | games#create-one-merged-nsp-repack | doc |
| Delete game (keeps saves) | + → Delete | game_internal.cpp:458 | games#delete-a-game | doc |
| Mods folder | details / + | game_details_ops.cpp:167 | games#mods-folder | doc |
| Save slots: create, increase size, info | Details → Saves | game_save_manager.cpp | games#manage-save-slots | doc |
| Sort, search, filters, options | + | game_menu.cpp, game_scan.cpp | games#sort-search-and-filter, #options | doc |
| Saves tabs, filters, per-game view, save-type badges | Game Tools → Saves | save_menu*.cpp | saves#find-your-way-around | doc |
| Backup (options, if newer), storage paths | A | save_backup_pub.cpp, save_paths.cpp | saves#back-up-a-save | doc |
| DBI/JKSV/Checkpoint backups, search paths | Backups tab | save_menu_catalog.cpp | saves#backups-made-by-other-apps | doc |
| Restore (game bundles, source picker, slot target, new slot, batch) | A → Restore | save_restore_route.cpp, save_slot_backend.cpp | saves#restore-a-save | doc |
| Undo restore (recovery.zip) | — | save_restore_zip.cpp | saves#undo-a-restore | partial: instructed flow is broken |
| Delete save / backups | A → Delete | save_deletion.cpp | saves#delete-a-save-from-the-console | doc |
| WebDAV sync | + → Sync with remote | save_remote_sync.cpp | saves#sync-backups-with-webdav | doc |
| Restore save from file browser | File browser + | filebrowser_options.cpp:185 | — | none: broken for .zip |
| SaveHubMenu | **unreachable** | save_hub_menu.cpp | — | none: dead code |
| Cheat packs, exact cheats, import, view, Fix BID, delete, cache | Game Tools → Cheats | cheats/*.cpp | cheats | doc |
| In-game cheat toggling | overlay | — | cheats#how-cheats-work | partial: which overlay |
| CheatSlips | **unreachable** | cheat_download_menu.cpp:84 | — | none: dead code |

## Updater, Kefir settings, system tools, themes, software
| Feature | UI | Code | Page | Status |
|---|---|---|---|---|
| Updater screen, update Kefir, changelog | Tools → Updater | kefir_menu.cpp, kefir/*.cpp | updater#update-kefir | partial: reboot behaviour |
| Firmware download/install, manual from folder/zip | Updater → FIRMWARE | kefir_ops.cpp, kefir_firmware*.cpp | updater#update-the-firmware | doc |
| Downgrade warning, downgrade fix modes, recovery | Updater | kefir_downgrade_box.cpp | updater#downgrade-the-firmware | partial: TegraExplorer step |
| Theme/translation cleanup after firmware | automatic | kefir_firmware_ops.cpp:94,496 | updater, themes#remove-a-theme | doc |
| Kefir Hub self-update | Settings → General → Auto-update | auto_update.cpp | updater#update-kefir-hub | doc |
| Overclock, 40MB, USB 3.0, redirect saves, 8GB DRAM | Tools → Kefir Settings | settings_kefir.cpp, settings_tweaks.cpp | kefir-settings#settings | partial: defaults, 40MB meaning |
| Translate system interface | Kefir Settings | settings_translate*.cpp | kefir-settings#translate-the-system-interface | doc |
| Module Manager | Tools → Tools | uninstaller_menu.cpp | system-tools#module-manager | doc |
| Fan curve | Tools → Tools | settings_fancurve*.cpp | system-tools#fan-curve | doc |
| Wi-Fi manager | Tools → Tools | wifi_menu.cpp, wifi_manager.cpp | system-tools#wi-fi | doc |
| System info, zero-fill, parental controls, junk clean | Tools → Tools | tools_menu.cpp:430-433 | system-tools | "planned" (Coming soon) |
| Theme packs, favorites, Themezer, NXThemes Installer | Tools → Themes | settings_themes.cpp, themezer*.cpp | themes | doc |
| Theme creator | File Browser → + → Create Switch Theme | theme_creator.cpp | themes#make-a-theme-from-a-picture | doc |
| Remove system theme | none in app | — | themes#remove-a-theme | partial: no UI |
| Kefir Hub appearance | Settings → Appearance | app_theme.cpp | themes#change-the-look-of-kefir-hub | doc |
| App Store | Tools → Software | appstore*.cpp | software#homebrew-app-store | doc |
| DBI submenu (translations, reset config) | Software → DBI | settings_dbi.cpp | software#dbi | doc |
| Mod downloaders, Custom Link | Software | settings_software.cpp, ghdl_api.cpp | software#mod-downloaders, #download-from-a-link | doc |
| GitHub releases browser | **unreachable** | ghdl.cpp, kefir_links.cpp:205 | — | none: dead code |

## Basics, homebrew, file browser
| Feature | UI | Code | Page | Status |
|---|---|---|---|---|
| Launch (applet vs title mode), replace hbmenu, Title Mode forwarder | Homebrew Menu, Settings → Homebrew | app.cpp, install_share.cpp:112 | index#start-kefir-hub | partial: does Kefir ship Hub as hbmenu |
| First run (language, account prompt), tabs, Tools grid | main | main_menu.cpp, tools_menu.cpp | index#first-start, #the-two-tabs | doc |
| Header, buttons, tabs, sidebars, dialogs, touch, keyboard + phone input | all | menu_base*.cpp, option_box.cpp, swkbd.cpp, remote_input.cpp | getting-started | doc |
| Language change | Settings → General | app_settings.cpp:481, i18n.cpp | getting-started#change-the-language | doc |
| Progress, minimize, screen off | transfers | progress_box*.cpp, app_frame.cpp | getting-started#long-tasks-and-background-tasks | doc |
| Homebrew list, launch, sort, star, select, delete, edit, forwarder, search paths | Homebrew tab | homebrew*.cpp, forwarder_editor.cpp | homebrew | doc |
| File Browser navigation, open by type, select, copy/move/delete/rename/create | File Browser | filebrowser/*.cpp | file-browser | doc |
| View options, split screen, sources, USB, MTP host, network locations | File Browser | filebrowser_sources.cpp, settings_sources.cpp | file-browser#switch-sources, #add-a-network-share | doc |
| ZIP browse/extract/compress, image viewer, text editor | File Browser | fs_zip.cpp, file_viewer*.cpp | file-browser | doc |
| File associations, forwarder for .nro/ROM, folder picker | File Browser | filebrowser_assoc.cpp, filebrowser_forwarder.cpp | file-browser#open-a-file-in-another-app, #make-a-forwarder | doc |
| Image System memory / microSD sources | god mode | filebrowser_internal.hpp:17 | — | none: debug |
| File picker sub-dialog | icon/avatar pickers | file_picker*.cpp | — | none: covered by caller pages |

## Network install, sharing
| Feature | UI | Code | Page | Status |
|---|---|---|---|---|
| Install & Share panel, Web Server (QR, kefir.local, ports), applet-mode prompt | Tools → + | install_share.cpp, web.cpp | install/network#browser, sharing#web | doc |
| Web upload install, .nro to /switch | browser | web_upload_routes.cpp | install/network#browser | doc |
| Web file manager, album page, server stop rules | browser | web_file_routes.cpp, web_screenshots.cpp, web.cpp | sharing#web-page, #album, #web-stop | doc |
| FTP server + settings, FTP install, progress | Settings → Network | ftpsrv_*.cpp | sharing#ftp, install/network#ftp | partial: top-level names, cancel on console |
| Ownfoil servers, catalog, title install | Install & Share / Software → Ownfoil | ownfoil*.cpp | install/network#ownfoil-add, #ownfoil-install | doc |
| Install from network shares | Settings → Sources, File Browser | settings_sources.cpp | install/network#network-share | partial (short, links out) |
| MTP start, drives, storages options | Install & Share, Settings | app_usb.cpp, haze_helper.cpp | sharing#mtp-start, #mtp-drives | doc |
| Single mounted folder (MTP/FTP/HTTP) | File Browser → + | filebrowser_share.cpp | sharing#mount | doc |
| NX-Link | Settings → Network | nxlink.cpp | sharing#nxlink | doc |
