# Kefir Hub architecture

Map for humans and models. Verify against code before relying on a detail; update this file when a module moves.
Graph: `graphify explain "<symbol>"`, `graphify path "A" "B"` (repo-root `graphify-out/`).

## Directories

| Path | Responsibility |
|---|---|
| `sphaira/source/main.cpp`, `app*.cpp` | Entry point and `App` lifecycle: startup, main loop/frame, widget stack, USB/MTP/network/save settings |
| `sphaira/source/ui/` | Widgets: list, sidebar, progress box, popups, notifications, screensaver, image viewer helpers |
| `sphaira/source/ui/menus/` | Screens (see Menu table); subfolders hold the split parts of the big menus |
| `sphaira/source/ui/menus/save*`, `save/` | Save Hub: discovery, backup catalog, ZIP/folder restore, slot backend, WebDAV sync |
| `sphaira/source/ui/menus/dbi*`, `dbi/` | Install session (USB/MTP/FTP/Web/Ownfoil queues, review queue, minimized badge) |
| `sphaira/source/yati/` | Installer core: NSP/NSZ/XCI/XCZ containers, sources (USB, stream, HTTP), NCA pipeline |
| `sphaira/source/haze*`, `haze/` | MTP server (libhaze): storages, install/save/game proxies, transfer progress UI |
| `sphaira/source/app_usb.cpp`, `usb/`, `utils/devoptab_mtp*` | USB host/device modes, MTP host drives (phones), DBI/GoldLeaf/Tinfoil protocols |
| `sphaira/source/web*`, `web_pages/` | HTTP server: file manager SPA, uploads/direct install, `/album`, `/input`, mDNS |
| `sphaira/source/threaded_file_transfer*` | Read/write worker transfer engine, ZIP preflight/extract/verify |
| `sphaira/source/download*` | libcurl download pool and WebDAV client |
| `sphaira/source/account/` | Users, offline account link (RomFS donors), NAND profile/playtime transfer |
| `sphaira/source/utils/` | devoptab mounts (SMB2, NFS, curl, MTP, NCA, RomFS), Ownfoil client, payload/reboot helpers |
| `sphaira/source/ftpsrv_*`, `nxlink.cpp`, `ntp.cpp`, `net.cpp` | FTP server, NX-Link, NTP clock, network state |
| `sphaira/source/i18n.cpp` | Translation lookup (`i18n::get`), language selection |
| `sphaira/include/` | Headers. Libnx-free pure logic lives here (`path_util.hpp`, `install_plan.hpp`, ...) and gets host tests |
| `sphaira/cmake/` | Reproducible patches for fetched deps (`patch_libhaze*.cmake`, ftpsrv) |
| `hbl/` | Homebrew loader embedded in forwarders (C) |
| `sysmodule/` | `sphaira_fan` / FunControl fan sysmodule (C) |
| `assets/romfs/` | Bundled data: `i18n/*.json`, TegraExplorer payload and `.te` scripts, assoc files, modules catalog |
| `tests/` | Host tests (see Build and test) |
| `tools/` | `i18n-translate/` (translation batch tool), `module_catalog/` (sysmodule catalog generator) |
| `docs/wiki/` | User documentation; `docs/dev/` developer docs (this file, CHANGELOG, audits, hardware checklist) |

## The 19 `Menu` structs

Always qualify (`ui::menu::save::Menu`), grep with the header path. Base classes: `MenuBase` (`ui/menus/menu_base.hpp`)
and `grid::Menu` (shared grid/list layouts).

| Namespace `sphaira::ui::menu::` | Header (`sphaira/include/ui/menus/`) | Shows |
|---|---|---|
| `grid` | `grid_menu_base.hpp` | Base for grid/list menus (not final) |
| `homebrew` | `homebrew.hpp` | Homebrew (NRO) launcher |
| `game` | `game_menu.hpp` | Installed games, Game Tools, save-slot manager |
| `save` | `save_menu.hpp` | Save Hub: Installed / Deleted / Backups |
| `appstore` | `appstore.hpp` | Homebrew App Store |
| `ownfoil` | `ownfoil.hpp` | Ownfoil server library |
| `users` | `users_menu.hpp` | User profiles, account link, backups |
| `wifi` | `wifi_menu.hpp` | Saved Wi-Fi networks |
| `dbi` | `dbi_menu.hpp` | Install session (derives `InstallSession`) |
| `filebrowser` | `filebrowser.hpp` | File browser, network sources, System Root |
| `filepicker` | `file_picker.hpp` | File/folder picker used by other menus |
| `fileview` | `file_viewer.hpp` | Text/file viewer |
| `gc` | `gc_menu.hpp` | Gamecard install/dump |
| `gh` | `ghdl.hpp` | GitHub release downloader |
| `kefir` | `kefir_menu.hpp` | Kefir Settings (fan curves, system switches) |
| `settings` | `settings_menu.hpp` | App settings |
| `themezer` | `themezer.hpp` | Themezer browser |
| `theme_creator` | `theme_creator.hpp` | Image to `.nxtheme` creator |
| `tools` | `tools_menu.hpp` | Tools hub grid |

## Threads

Main thread: `App` loop (input, draw, widget stack). Every other thread and what it shares:

| Thread | Started in | Shares |
|---|---|---|
| Log flusher | `log.cpp` `ensure_thread_started` | `std::mutex mutex`, `g_thread_running`, `g_thread_stop`, `g_file_open` |
| MTP responder (libhaze) | `haze_helper.cpp` `Init` → `haze::Initialize` | `haze/haze_internal.cpp`: `g_mutex`, `g_mtp_ui_mutex`, `g_mtp_done_event`, `g_mtp_transfer_*`, `g_is_running`, `g_should_exit` (atomic) |
| MTP progress worker | `haze_helper.cpp` `StartMtpProgressBox` (ProgressBox thread) | same as MTP responder |
| FTP server | `ftpsrv_helper.cpp` `Init` | `g_mutex`, `g_is_running`, `g_should_exit` (atomic) |
| NX-Link | `nxlink.cpp` `nxlinkInitialize` | `g_mutex`, `g_is_running`, `g_quit` (atomic) |
| NTP | `ntp.cpp` `Start` | atomics `g_thread_running`, `g_stop`, `g_display_offset`; `g_wake_event` |
| Title info loader | `title_info.cpp` `Init` | `g_mutex`, `g_ref_count` |
| Forwarder auto-install check | `forwarder_auto_install.cpp` `StartCheck` | atomics `g_stop_requested`, `g_thread_active`, `g_thread_created` |
| Download pool | `download.cpp` `ThreadEntry::Create` | per-entry `m_uevent`; queue owned by the pool |
| curl devoptab push/pull | `utils/devoptab_curl_thread.cpp` `PushPullThreadData::CreateAndStart` | per-stream buffers |
| Web share server, mDNS, upload writer | `web.cpp` `StartShareServer`, `web_mdns.cpp` `StartMdnsResponder`, `web_upload_routes.cpp` `ReceiveUpload` | web server state in `web*.cpp` |
| Install session worker | `ui/menus/dbi_menu.cpp` `Menu::Menu` / `InstallSession` | session object (`InstallSession`) |
| Background stream installer | `install_stream_menu_base.cpp` `BackgroundInstaller::OnInstallStart` | `s_install_thread`, session queue |
| File browser metadata | `filebrowser/filebrowser_view.cpp` `FsView::FsView` | `m_metadata_mutex`, `m_metadata_cond` |
| ProgressBox worker | `ui/progress_box.cpp` `ProgressBox::ProgressBox` | the box's `m_thread_data` |
| Transfer read/write | `threaded_file_transfer_core.cpp` `TransferInternal` | `t_data` ring buffers (joined before return) |
| Installer read/decompress/write | `yati/yati.cpp` `Yati::InstallNcaInternal` | `t_data` (joined before return) |

Network state (`net.cpp`: `g_mutex`, `g_cache_*`, `g_request_open`) is read from several of these.
Rule: a global touched by more than one row above is `std::atomic` or used only under that module's mutex.
Phase 3.1 of `plan.md` tracks the remaining plain globals.

## God nodes

| Node | Rule |
|---|---|
| `App` (`app.hpp`, ~845 edges) | No new static members or global state; new logic goes into free functions of the owning module |
| `Result` / `R_TRY` / `R_SUCCEED` (`defines.hpp`) | Never include `<haze/results.hpp>` in sphaira sources: its macros shadow sphaira's |
| `log_write` (`log.hpp`) | Callable from any thread; keep format strings bounded |
| `fs::Fs`, `FsNativeSd` (`fs.hpp`) | Behaviour changes here reach every menu; change only with a task that names them |
| `InstallSession` (`dbi_menu.hpp`) | All install transports go through it; change with a hardware check |

`docs/dev/CHANGELOG.md` also shows up as a large graph node; it is a document, not code.

## Build and test

- NRO build (WSL, devkitPro): `cmake --preset ReleaseWithInstall && cmake --build --preset ReleaseWithInstall --parallel $(nproc)`;
  procedure and fix loop: `.claude/skills/test-build/SKILL.md`. Output `build/ReleaseWithInstall/`.
- Host tests: `tests/run.sh` — every `tests/test_*.cpp` (g++ -std=c++20 -Werror, `-I sphaira/include`, optional
  `// LINK:` units), `check_dead_symbols.py`, libhaze/ftpsrv patch checks (need `cmake`), Python contracts.
- Single host test: `g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_X.cpp -o /tmp/X && /tmp/X`.
- Hardware checklist: `docs/dev/HARDWARE-CHECKLIST.md`.

## i18n

- Keys are the English strings: `i18n::get("...")` / `"..."_i18n` in code; values in `assets/romfs/i18n/<code>.json`.
- `en.json` is the reference; every language in `tools/i18n-translate/languages.json` must have all keys,
  the same printf specifiers and newlines, and `__language_name`. Checked by `tests/test_i18n_deployment_contract.py`.
- New keys: add to `en.json` and translate with `tools/i18n-translate/` (see its README).
