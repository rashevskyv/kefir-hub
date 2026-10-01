# System Utilities & Customization

Kefir Hub includes an extensive collection of system utilities, diagnostics, and customization tools to tune and personalize your Nintendo Switch.

On first launch, choose an interface language from the bundled translation JSON files. Kefir Hub lists their language names alphabetically, saves the selected language code, and opens the picker again if the saved language is missing or unavailable. The same picker is available under **Settings -> General -> Language**. The current bundle has 26 languages; Russian is not offered. This setting is separate from the system interface translation tools below.

---

## 1. Module Manager

Manage background Atmosphere sysmodules directly from **Tools -> Tools -> Module Manager**:
- **Real-Time RAM Telemetry:** Displays total system RAM used and free, alongside individual memory consumption for each running sysmodule using libnx kernel queries (`svcGetSystemInfo`).
- **Independent State Tracking:**
  - **Now: On / Off:** Live execution status in memory.
  - **After reboot: Enabled / Disabled:** Boot-time autostart configuration (`/atmosphere/contents/<tid>/flags/boot2.flag`).
- **Rich Module Catalog:** Module descriptions and metadata are loaded from an embedded catalog and synchronized in the background with ndeadly's community repository (`/config/kefir/modules.json`).
- **Reboot Protection:** Detects modules that cannot be hot-toggled safely in memory, prompting the user with a reboot notification instead of causing crashes.

### Catalog generator (developers)

The project includes a developer-focused Python utility (`tools/module_catalog/update_module_catalog.py`) to automatically fetch, verify, and compile a catalog of homebrew sysmodules from online repositories:
- **Automatic Merging:** Combines sysmodule lists from ndeadly's repository and the Switch Homebrew App Store.
- **Manual Overrides:** Integrates custom verified rules (TIDs, canonical names, and repository paths) from `manual_overrides.json` to guarantee highly reliable results.
- **Evidence Verification:** Automatically verifies `tid_evidence` links to ensure they return a valid HTTP status and contain the exact 16-character Title ID.
- **Runtime Generation:** Generates the offline modules catalog (`assets/romfs/modules/homebrew_sysmodules.json`) and localization key suggestions for the main application.
- **Runtime Integration:** Module Manager loads the embedded catalog immediately, refreshes a validated SD index directly from ndeadly's maintained list in the background, and resolves descriptions through the regular i18n files with an English fallback.

---

## 2. Fan Curves & Hardware Thermal Control

Customize thermal profiles under **Tools -> Tools -> Fan curves** (also available in Kefir Settings):
- **Handheld & Docked Profiles:** Set custom fan speed curves independently for handheld and docked modes.
- **Dynamic Live Apply:** When the companion `sphaira_fan` sysmodule (`00FF46554E43544C` / `FunControl`) is active, fan curve modifications apply immediately without rebooting.
- **Fallback Persistence:** If the sysmodule is inactive, curves are saved to `/atmosphere/config/system_settings.ini` to take effect on subsequent boots.
- **Non-Conflicting Thermal Queries:** Reads SoC temperatures via the native `ts` (Thermal Measurement) service, avoiding IPC collisions with Atmosphere's `ptm`.
- **Live Sysmodule Telemetry:** The background module exports state telemetry (`/switch/sphaira/fan_status.bin`), providing real-time hardware status to Sphaira.
- **Physical Fan Motor Inertia Modeling:** UI graph markers smoothly model physical motor spin-up and spin-down acceleration/deceleration response.

---

## 3. System Interface Translations

Manage custom system interface localizations under **Settings -> Translate Interface**:
- **Automated Firmware Matching & Policy Resolution:** Automatically detects installed system firmware (`hats::getSystemFirmware()`) and maps it to upstream release tags:
  - **Exact Match:** When installed firmware directly matches a known translation release (including the newly added `FW22.5.0-TR2.01`), Sphaira uses that release directly without warning prompts.
  - **Latest Translation Fallback (FW 22.5.0+):** When running on firmware newer than the latest known translation release (`22.5.0`), Sphaira automatically utilizes the latest release (`FW22.5.0-TR2.01`) with an advisory confirmation dialog instead of marking the firmware unsupported, eliminating arbitrary version lockouts.
  - **Concrete Intermediate Mapping:** Robust range mappings ensure older and intermediate firmware versions (16.0.0–16.1.0, 18.0.0–18.1.0, 19.0.0–19.0.2, 20.0.0–20.5.0, 21.0.0–22.4.x) cleanly map to tested stable releases.
- **Dynamic Localized Warnings:** Compatibility warning dialogs dynamically insert the target firmware version from the release tag into localized messages across all 26 supported interface languages.
- **Live Diagnostics & Header Stats:** The Translate Interface menu header displays active firmware and target release versions continuously (`FW ...`). A dedicated diagnostics entry at the top of the menu provides full details including detected console region, target release tag, metadata API endpoints, and GitHub release URLs.
- **Transparent Source Previews:** Before downloading translation lists or installing specific language archives, prompts and progress transfers explicitly show the target firmware tag, replacement language variation (`replaces_...`), and full GitHub download URLs.
- **Graceful Fallback & Replacement Choice:** If an exact match for the current console language and region combination is not found, Sphaira allows selecting from all available language replacement variations instead of failing, enabling seamless custom setups.
- **Seamless Background Replacement (Single Reboot):** When installing a new translation while an old one is already present on the SD card, Sphaira purges the previous translation files silently in the background without interrupting the user or forcing an intermediate restart. The new translation is downloaded, extracted, and installed, requiring only a single reboot at the end.
- **Manual Removal with Deferred Reboot Option:** Selecting **Remove installed translation** safely deletes installed translation files from `/atmosphere/contents` and presents an advisory prompt allowing the user to choose between **Reboot now** (recommended) or **Reboot later**. A warning informs the user that while loaded in-memory strings won't crash the console, rebooting now is recommended to avoid text or interface display inconsistencies. If deferred, users can continue browsing and working in the app freely.
- **Locked File Bypass & Reboot Recovery:** During interface translation removal, file lock exceptions (`FsError_TargetLocked`) are safely bypassed. Locked files are left behind while deleting everything else, and the system reboots immediately, releasing all filesystem handles and completing the removal process smoothly.
- **Empty Translation Folder Filtering:** The translations menu dynamically hides translation entries that do not have their corresponding JSON localization layout metadata file on the SD card, avoiding empty selection directories and preventing errors like `Result_FsEmpty`.
- **Automatic Localized Interface Switching:** When installing a system interface translation, Sphaira automatically checks if the corresponding language is supported in its own settings (e.g., matching Ukrainian, Portuguese, or Vietnamese). If supported, Sphaira switches its own language layout configuration to match the installed translation on the fly, allowing a seamless experience right after the system reboot without manual settings adjustments.

---

## 4. Forwarder Editor & SteamGridDB

Create custom HOME Menu forwarder NSPs for homebrew applications via **Tools -> Create Forwarder** (or the options menu on any NRO / homebrew item):
- **SteamGridDB Icon Picker:** Search [SteamGridDB](https://www.steamgriddb.com) directly from the console to select high-quality vertical icons. You can paste an API key manually or use the built-in **Web Handoff** (`/apikey` endpoint) by scanning a QR code with a phone on the same Wi-Fi. The API key request endpoint is securely gated (`404 Not Found` when handoff is inactive) and saved to `/config/kefir/config.ini` in plain text (`[steamgriddb] api_key`).
- **Automatic Forwarder Detection & Silent Background Installation:** On application startup, Sphaira automatically checks whether a Homebrew Menu or Kefir Hub forwarder is already installed on the console (checking standard title IDs `010000000000100D`, `050000000000100D`, as well as generated forwarder IDs). If no forwarder is found, Sphaira transparently generates and installs a Kefir Hub HOME Menu forwarder in a background thread via Yati without interrupting the user or prompting to restart. To prevent Nintendo account and hardware bans, Sphaira checks whether an EmuNAND is configured on the system: if an EmuNAND exists on the SD card but the console is currently booted into SysNAND or Semi-stock (ending with 'S' in System Settings), automatic forwarder installation is strictly suppressed so the clean SysNAND remains untouched.
- **Accurate EmuNAND vs NAND Environment Indicators:** The top status bar storage indicator dynamically checks whether custom firmware is currently executing inside an emulated environment (EmuMMC). The label displays **EmuNAND** strictly when booted in EmuNAND ('E' in console settings), and displays **NAND** when booted in SysNAND or Semi-stock ('S' in console settings), replacing legacy static labels.
- **Custom Launch Flags:** New forwarders use 39-bit address space and 3 CPU cores by default. Choose 36-bit for compatibility, or explicitly enable 4 cores after a warning that core 3 is shared with system services. Configure these defaults in Forwarder Options, or enable **Ask every time** to adjust them per forwarder. Profile selection, screenshots, video capture, and `svcDebug` are also available. Changing defaults does not update forwarders already installed on the HOME Menu; recreate those forwarders to apply new settings.
- **Dual-Pane D-Pad Navigation & Safety:** Features intuitive focus switching between the left icon preview and the options list (press **DOWN** or **RIGHT** to enter settings, **LEFT** or **UP** from the top row to return to icon focus), with robust pointer safety across all controller and touch update loops.

---

## 5. Themes, Theme Creator & Themezer

- **On-Console Theme Creator:** Convert any image (PNG, JPG) into an official `.nxtheme` package directly from the File Browser. Pan, zoom, and crop to native 16:9 aspect ratios, select target menus (Home Menu, Lock Screen, Settings, User Page), and automatically queue for installation in NXThemesInstaller.
  - **Interactive Cropping:** Open any image in the File Browser, press the options button, and select "Create Switch Theme". You can zoom using ZL/ZR and pan using the Left Stick or D-Pad to select the perfect crop window (constrained to the native 16:9 aspect ratio).
  - **Target Selection:** Configure theme properties including target system menu (Home Menu, Lock Screen, All Apps, Settings, User Page, News, or Player Select), theme name, and author name.
  - **Auto Installation:** After generation, Sphaira switches to a confirmation screen where you can hold **A** (3s) to install the theme or hold **Y** (3s) to install and reboot. It triggers `NXThemesInstaller` in the background with appropriate arguments (`--auto-install` and optionally `--reboot`).
- **Theme Options:** Choose interface themes, animated background visuals, and time formats under "Settings -> Appearance -> Sphaira theme options".
- **Theme Packages & Instant Installation:** Download bundled theme packages (such as Mario BG Dark and Switch 2 Theme by alexwak) directly from the "Themes" menu under Tools. Once downloaded and extracted to `/themes/`, Sphaira automatically prompts to install the theme immediately via `NXThemesInstaller.nro` with all extracted `.nxtheme` files queued.
- **Themezer Favorites:** Add any theme pack from Themezer to your favorites list by pressing **R3** (Right Stick click) in the Themezer download menu. Favorites are instantly shown on the main "Themes" tab alongside built-in options, marked with a star icon for easy access and offline viewing.
- **Themezer Filters:** Browse themes easily by applying filters directly in the "Themezer Options" sidebar. You can filter themes by target layout (e.g., Home Menu, Lock Screen, All Apps, Settings, Player Select, User Page, News) and input custom search tags (separated by spaces or commas). When a target filter is active, the app queries individual themes instead of packs and lists them dynamically, matching them with their parent pack previews.

---

## 6. DBI Management & Fan Translations

- **DBI Management & Fan Translations:** Under "Settings -> Software -> DBI", Sphaira provides integrated management for DBI installer builds and translations:
  - **Pinned Utility Controls:** Primary operations ("Download DBI translations list" / "Update DBI translations list", "Russian latest DBI", and "Reset DBI config") remain pinned at the top of the menu.
  - **Dynamic List Action:** The translations action dynamically toggles between "Download DBI translations list" and "Update DBI translations list" once package definitions are loaded.
  - **Visual Divider & Alphabetical Ordering:** When translations are downloaded, a clear divider separates the top utility actions from the fan translations list, with entries sorted alphabetically for effortless browsing.

---

## 7. AppStore

Sphaira includes a built-in, high-performance Homebrew AppStore client designed for seamless package discovery and maintenance:
- **Installed vs Store Version Tracking:** App cards display both the repository version (`version: ...`) and the locally installed version (`installed: ...`), determined from `.info` metadata or parsed directly from NRO NACP headers. When an update is available, the installed version is highlighted with the active theme's accent color.
- **LibRetro Nightly Buildbot Integration:** For RetroArch (`RetroNX`), downloads and updates are automatically routed to the official LibRetro Nightly builder (`https://buildbot.libretro.com/nightly/nintendo/switch/libnx/RetroArch.7z`), ensuring modern Atmosphere and Horizon OS compatibility. If an outdated store build or non-Nightly package is detected, the `Launch` button is replaced by an `Update` action.
- **Graceful Download Cancellation:** Cancelling a download or uninstall operation at any point cleanly aborts transfer threads and notifies the user with a friendly dialog without triggering false-positive network error alerts.
- **Informative Error Handling & Network Gate:** Download and network operations verify active connectivity before transfer (`RequireConnection`), guiding the user to system Wi-Fi settings when offline instead of failing with raw result codes. Error dialogs feature user-friendly titles (`An error occurred`), localized explanatory guidance, secondary diagnostic codes, and suppress unnecessary bug report prompts on expected network or filesystem conditions.

---

## 8. Automatic Silent Update & About

Sphaira / Kefir Hub includes an intelligent, non-blocking **Automatic Silent Update** system:
- **Background Release Detection & Download:** On startup, the application queries GitHub Releases (`https://api.github.com/repos/rashevskyv/kefir-hub/releases/latest`) in the background. An update is only offered or installed if the remote release version is strictly newer than the installed application version (`version::IsNewer(APP_VERSION, remote_version)`), preventing test-mode regressions, inadvertent downgrades, or false updates triggered by empty/malformed versions or rate-limit HTTP responses. Parsing uses a zero-allocation, stream-free scanner independent of C++ locale/iostreams, correctly handling multi-part numbers over 255 and stripping `v`/`V` prefixes. If a strictly newer release is detected, it selects the matching binary asset (`kefir-hub.nro` / `sphaira.nro`), downloads it asynchronously to staging cache without UI stalls, and prioritizes `/switch/kefir-hub/kefir-hub.nro` during installation.
- **Safe Atomic Replacement:** Once verified, the update replaces the active executable path (`App::GetExePath()`) atomically. If `Replace hbmenu on exit` is enabled, `/hbmenu.nro` is also seamlessly kept up to date. The update is applied completely silently in the background without popups or prompts; the new version will seamlessly run the next time the application is opened.
- **Configurable in Settings:** Can be enabled or disabled via **Settings -> General -> Auto-update** (enabled by default).
- **About Screen & Changelog Viewer:** View the current application version, repository link, and browse release notes / changelogs directly under **Settings -> General -> About**. Includes rich markdown rendering, smooth scrolling (analog sticks, d-pad, L/R page jumping, and touch dragging), and an instant refresh option (press **X**).

---

## 9. Safe Homebrew NRO Customization

Customize NRO metadata and icons directly from the Homebrew menu via **Customize Homebrew**:
- **Atomic 4-Step Update & Memory Optimization:** Modifies NRO RomFS assets streaming 64 KiB chunks into a temporary file (`.sphaira.tmp`) without duplicating the NRO vector in RAM (reducing peak heap usage from 2x to 1x NRO size). Uses a 4-step atomic rename algorithm (`tmp -> bak -> path`) with pre-deletion of stale `.bak` files and on-disk size verification before touching original files, protecting against SD write failures.
- **Asynchronous Worker:** Runs NRO updates asynchronously in a background progress box while synchronously releasing the editor UI.

---

## 10. Homebrew Loader Safety (HBL & Forwarder Isolation)

The built-in Homebrew Loader (embedded within forwarder NSPs and chainloader pipelines) includes hardened NRO parsing and memory isolation:
- **Exact Code Segment Reading:** Sequentially reads exact `NroStart` (16 bytes), `NroHeader` (112 bytes), and payload data strictly up to `header->size`. Appended RomFS assets, NACP metadata, and icons are never read into memory buffers, preventing payload overflow into target program address spaces.
- **Deterministic BSS Memory Zeroing:** The unmapped BSS memory range (`[header->size, (header->size + header->bss_size + 0xFFF) & ~0xFFF]`) is explicitly zeroed before memory mapping (`svcMapProcessCodeMemory`), ensuring target homebrew applications (such as `NX-Activity-Log` and `pipensx`) start with clean uninitialized data sections and eliminating invalid pointer dereferences / Data Abort (`2168-0002`) cache maintenance crashes.
- **Clean OverrideHeap Placement:** Accurately places `OverrideHeap` boundaries (`nro_heap_start` and `nro_heap_size`) immediately following the verified NRO code and BSS segments, guaranteeing that memory allocators (`dlmalloc`) initialize from clean, uncorrupted heap space.

---

## 11. File Association

Sphaira has file association support. Let's say your app supports loading .png files, then you could write an association file, then when using the file browser, clicking on a .png file will launch your app along with the .png file as argv[1]. This was primarly added for rom loading support for emulators / frontends such as RetroArch, MelonDS, mGBA etc.

```ini
path=/switch/your_app.nro
supported_extensions=jpg|png|mp4|mp3
```

The `path` field is optional. If left out, it will use the name of the ini to find the nro. For example, if the ini is called mgba.ini, it will try to find the nro in /switch/mgba.nro and /switch/folder/mgba.nro.

See `assets/romfs/assoc/` for more examples of file assoc entries.
