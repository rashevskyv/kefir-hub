# Refactor plan: decompose oversized source files

Goal: split the largest source files into smaller compilation units. This is a
**move-only refactor** — behaviour must be byte-for-byte identical.

Note: this fork no longer merges from upstream (both upstream repos are dead), so
every file is fair game — there is no merge-conflict constraint.

## Ground rules (read before every step)

1. **Move-only.** Cut code and paste it into a new file. Do NOT rename symbols, do NOT
   "improve" logic, do NOT reorder statements, do NOT fix bugs you notice (report them
   in the PR/commit message instead), do NOT reformat untouched lines.
2. **One step = one commit.** Commit message format: `refactor(<area>): extract <what> into <file>`.
3. **The project must compile after every commit.** Build is done by Agent 1 / reviewer;
   your job is to keep the code structurally sound: every moved function keeps its exact
   signature, every new `.cpp` is added to `sphaira/CMakeLists.txt` (the source list starts
   around line 40), every new header has `#pragma once`.
    *Примітка:* Збірка проекту виконується користувачем вручну, агенту самостійно збирати
    програму не потрібно. Запитувати користувача про збірку також не потрібно.
4. **Anonymous-namespace helpers** that end up used from more than one `.cpp` must move to
   a named `detail` namespace inside the new shared header/cpp
   (e.g. `sphaira::ui::menu::hats::detail`). Helpers used by only one new `.cpp` stay in
   an anonymous namespace inside that `.cpp`.
5. Keep the existing include style of each file (relative includes, same order style).
   A new `.cpp` includes only what it actually needs — start by copying the original
   include block, then delete unused ones.
6. **Only touch files listed in the phase you are working on.** Do not start
   opportunistic refactors of other files, even if they look easy.
7. After each phase, STOP and wait for review before starting the next phase.
8. Line numbers below are hints from the current state of `master`; trust symbol names,
   not line numbers.

---

## Phase 1 — `ui/menus/cheats_menu.cpp` (5667 lines, fork-owned)

Current layout: ~3060 lines of anonymous-namespace helpers, then 7 menu classes.

Create directory `sphaira/source/ui/menus/cheats/` and matching headers under
`sphaira/include/ui/menus/cheats/` only where a declaration must be shared.

| Step | New file | What moves there |
|---|---|---|
| [x] 1.1 | `cheats/cheats_dmnt.hpp/.cpp` | `DmntMemoryRegionExtents`, `DmntCheatProcessMetadata` (line ~89) and every helper that talks to dmnt/dmntcht (attach, toggle, read metadata). |
| [x] 1.2 | `cheats/cheats_lookup.hpp/.cpp` | `InstalledNcaLookupResult` (~229), `BuildIdLookupResult` (~718) and their helper functions (build-id resolution, NCA lookup). |
| [x] 1.3 | `cheats/cheats_db.hpp/.cpp` | `CachedCheatMetadata` (~1587), `NxDbVersionInfo` (~1902) and helpers for the cheat DB / cache / download-side metadata. |
| [x] 1.4 | `cheats/cheat_files_menu.cpp` | Classes `CheatFilesMenu`, `CheatContentMenu`, `CheatCodeViewerMenu` (~3515–4130). Their declarations move from `cheats_menu.hpp` into `include/ui/menus/cheats/cheat_files_menu.hpp` if they are declared there; if declared only in the .cpp, keep them local. |
| [x] 1.5 | `cheats/cheat_game_select_menu.cpp` | Classes `CheatGameSelectMenu`, `CheatDownloadMenu` (~4132–end). Same header rule as 1.4. |
| [x] 1.6 | leftover check | `cheats_menu.cpp` should now contain only `CheatsMenu` + `CheatViewMenu` and the helpers used exclusively by them. Target: under ~1500 lines. |

## Phase 2 — `ui/menus/settings_menu.cpp` (4062 lines, fork-owned)

| Step | New file | What moves there |
|---|---|---|
| [x] 2.1 | `ui/menus/settings/settings_fs_utils.hpp/.cpp` | Generic file/string helpers from the anon namespace: `Trim`, `ReadTextFile`, `ReadLines`, `WriteLines`, `StartsWith`, `SplitCommand`, `ExtractBracketName`, `ExtractIniKey`, `ExtractJsonStringField`, `FileExists`, `DirectoryExists`, `ParentPath`, `EnsureParentDirectory`, `CopyFileSimple`, `DeletePath`, `CopyDirectoryContents`, `MovePath`, `IniValueEquals`, `SetIniValue`, `ReadIniRawValue`, `SetIniRawValue`. Namespace `sphaira::ui::menu::settings::detail`. Do NOT try to deduplicate against `utils/utils.cpp` — that is a separate future task. |
| [x] 2.2 | `ui/menus/settings/settings_translations.hpp/.cpp` | `DbiTranslationEntry`, `InterfaceTranslationEntry`, `ParseDbiTranslations`, `ParseInterfaceTranslations`, `ReadInterfaceReplacementOptions`, `FileNameFromUrl`, `TranslationExtractFolder`, `InstallDbiTranslation`, `InstallInterfaceTranslation`, plus `DownloadFile`/`UnzipFile` wrappers if only used here. |
| [x] 2.3 | `ui/menus/settings/settings_tweaks.hpp/.cpp` | System-tweak appliers: `IsEmummcEnabled`, `RebootAfterSetting`, `ApplyOverclock`, `Apply40Mb`, `ApplyRedirectSaves`, `Apply8GbDram`, fan-curve functions (`DefaultHandheldFanCurve`, `DefaultDockedFanCurve`, `QuietFanCurve`, and the rest of the fan/OC block), `KefirSetting`, `PackageAction` if they belong to this block. |
| [x] 2.4 | leftover check | `settings_menu.cpp` keeps the menu classes and UI wiring only. Target: under ~1800 lines. |

## Phase 3 — `web.cpp` (3009 lines, rewritten by the fork)

| Step | New file | What moves there |
|---|---|---|
| [x] 3.1 | `source/web_pages.hpp` | All `constexpr std::string_view` HTML/JS blobs: `LIGHTBOX_CONTENT`, `CONFIRM_MODAL_HTML`, `CONFIRM_MODAL_JS`, `FOLDER_PAGE_HEADER`, `FOLDER_PAGE_JS`, `PROGRESS_PAGE` (~lines 404–1880). Header-only, `namespace sphaira::webpages`. This alone removes ~1200 lines. |
| [x] 3.2 | `source/web_qr.hpp/.cpp` | `class QrCode` (~2507). |
| [x] 3.3 | `source/web_upload.hpp/.cpp` | `UploadState` (~57) and `SocketStream` (~1296) plus their helpers, successfully extracted. |

## Phase 4 — `ui/menus/save_menu.cpp` (2923 lines, heavily diverged fork file)

| Step | New file | What moves there |
|---|---|---|
| [x] 4.1 | `ui/menus/save/save_paths.hpp/.cpp` | Backup naming/path builders: `GetSaveFolder`, `GetSaveTypeSubdir`, `GetDbiTypeLetter`, `ParseDbiBackupNameTimestamp`, `ParseBackupNameTimestamp`, `GetSaveTypeLabel`, `SaveTypeIndex`, `SaveEntryKey`, `IsSystemLikeSave`, `DisplayEntryKey`, `BuildSaveName`, `BuildSavePathName`, `BuildSaveBasePathLegacy`, `BuildSaveBasePath`, `BuildDbiGameFolderName`, `BuildDbiSavePath`, `IsDbiBackupName`, `DbiBackupMatchesEntry`, `CollectDbiBackups`, `NormalizeBackupRoot`. |
| [x] 4.2 | `ui/menus/save/save_locations.hpp/.cpp` | WebDAV/location helpers: `WebdavLocationKey`, `GetWebdavLocations`, `MakeSdCardDumpLocation`, `MakeDumpLocationFromFsEntry`, `MakeLocationLabel`, `MakeSdLocationLabel`, `SerializeRecentBackupDir`, `MakeLocationKey`, `ParseRecentBackupDir`, `RecentBackupDirExists`, `MakeDumpLocationFromRecent`. |
| [x] 4.3 | leftover check | `save_menu.cpp` keeps `Menu`/UI code. Target: under ~1800 lines. |

## Phase 5 — `ui/menus/kefir_menu.cpp` (2434 lines, fork-owned)

| Step | New file | What moves there |
|---|---|---|
| [x] 5.1 | `ui/menus/kefir/kefir_changelog.hpp/.cpp` | `ChangelogSegment` (~523), `AddChangelogSegment` and all changelog parsing/rendering helpers (~330–700). |
| [x] 5.2 | `ui/menus/kefir/kefir_firmware.hpp/.cpp` | `FirmwareValidation` and version-string parsing helpers (`Trim`-style loops, digit parsing, ~150–310). |
| [x] 5.3 | optional | `DowngradeHoldConfirmBox` widget → `ui/hold_confirm_box.hpp/.cpp` only if the reviewer approves making it reusable; otherwise leave it. |

## Phase 6 — `ui/menus/filebrowser.cpp` (2621 lines)

| Step | New file | What moves there |
|---|---|---|
| 6.1 | `ui/menus/filebrowser_assoc.hpp/.cpp` | File-type/rom helpers from the anon namespace: extension tables (`AUDIO_EXTENSIONS` … `ZIP_EXTENSIONS`), `IsExtension` (both overloads), `ExtDbEntry`, `RomDatabaseEntry`, `GetRomDatabaseFromPath`, `GetRomIcon`, `GetNxmpPath`, `HasNxmp`. Namespace `sphaira::ui::menu::filebrowser::detail`. |
| 6.2 | `ui/menus/filebrowser_forwarder.hpp/.cpp` | `struct ForwarderForm` (~55) + `ForwarderForm::LoadNroMeta` (~433) and `FsView::InstallForwarder` if it is only a thin caller — otherwise leave `InstallForwarder` in place. |
| 6.3 | `ui/menus/filebrowser_ops.cpp` | Heavy `FsView` operation bodies (same class, second TU — this is fine): `InstallFiles`, `UnzipFiles`, `ZipFiles`, `UploadFiles`, `ShareFolder`, `OnDeleteCallback`, `OnPasteCallback`, `CheckIfUpdateFolder`. UI/navigation methods (`Update`, `Draw`, `Scan`, `Sort`, selection) stay in `filebrowser.cpp`. |

## Phase 7 — `app.cpp` (2366 lines)

The `App` class stays one class; its method definitions get split across TUs.

| Step | New file | What moves there |
|---|---|---|
| 7.1 | `source/app_settings.cpp` | The long run of trivial option accessors: `GetNxlinkEnable`, `GetHddEnable`, `GetWriteProtect`, `GetWebdavUrl/User/Pass`, `GetLogEnable`, `GetReplaceHbmenuEnable`, `GetInstall*`, `GetThemeMusicEnable`, `GetAnimatedWavesEnable`, `GetWaveColor*`, and every sibling Get/Set that just reads/writes an option (roughly lines 671–1100+). Also `NormalizeWebdavUrl`. |
| 7.2 | `source/app_theme.cpp` | Theme handling: `ThemeData`, `ThemeIdPair`, `LoadThemeMeta`, `GetThemeMetaList`, `SetTheme`, `GetThemeIndex`, theme scan/load helpers, `DEFAULT_MUSIC_PATH`, `GetDefaultImage*`. |
| 7.3 | leftover check | `app.cpp` keeps init/deinit, `Loop`, `Push*`, `Notify*`, frame-buffer and input plumbing. Target: under ~1200 lines. |

## Phase 8 — `utils/devoptab_common.cpp` (1660 lines)

| Step | New file | What moves there |
|---|---|---|
| 8.1 | `utils/devoptab_buffered.hpp/.cpp` | `BufferedData` and `LruBufferedData` (declarations may currently live in a header — if so, only the definitions move). |
| 8.2 | `utils/devoptab_curl_thread.hpp/.cpp` | `PushPullThreadData`, `PushThreadData`, `PullThreadData` with their callbacks and `thread_func`. |
| 8.3 | `utils/devoptab_curl_device.cpp` | `MountCurlDevice` methods: `Mount`, `CreatePushData`, `CreatePullData`, `curl_set_common_options`, the write/read callbacks, `html_decode`, `url_decode`, `build_url`. |

## Backlog (only after Phases 1–8 are reviewed)

Candidates in the 1000–1700 line range, to be re-scoped later:
`yati/yati.cpp` (1700 — installer core, riskiest, needs its own plan),
`ui/menus/appstore.cpp` (1444), `ui/menus/themezer.cpp` (1407),
`download.cpp` (1306), `ui/menus/gc_menu.cpp` (1058), `owo.cpp` (1032),
`ui/menus/game_menu.cpp` (1009).

---

## Verification checklist per step (the reviewer runs this)

- `git diff --stat` shows only: lines removed from the big file, lines added to the new
  file(s), CMakeLists entry, include lines.
- Moved code is identical (verify with `git diff --color-moved=dimmed-zebra`).
- No symbol is defined twice; no anon-namespace function is referenced across TUs.
- Build passes (Agent 1).
