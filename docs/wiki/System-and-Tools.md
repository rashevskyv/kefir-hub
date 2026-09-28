# System Utilities & Customization

Kefir Hub includes an extensive collection of system utilities, diagnostics, and customization tools to tune and personalize your Nintendo Switch.

---

## 1. Module Manager

Manage background Atmosphere sysmodules directly from **Tools -> Tools -> Module Manager**:
- **Real-Time RAM Telemetry:** Displays total system RAM used and free, alongside individual memory consumption for each running sysmodule using libnx kernel queries (`svcGetSystemInfo`).
- **Independent State Tracking:**
  - **Now: On / Off:** Live execution status in memory.
  - **After reboot: Enabled / Disabled:** Boot-time autostart configuration (`/atmosphere/contents/<tid>/flags/boot2.flag`).
- **Rich Module Catalog:** Module descriptions and metadata are loaded from an embedded catalog and synchronized in the background with ndeadly's community repository (`/config/kefir/modules.json`).
- **Reboot Protection:** Detects modules that cannot be hot-toggled safely in memory, prompting the user with a reboot notification instead of causing crashes.

---

## 2. Fan Curves & Hardware Thermal Control

Customize thermal profiles under **Tools -> Tools -> Fan curves** (also available in Kefir Settings):
- **Handheld & Docked Profiles:** Set custom fan speed curves independently for handheld and docked modes.
- **Dynamic Live Apply:** When the companion `sphaira_fan` sysmodule (`00FF46554E43544C` / `FunControl`) is active, fan curve modifications apply immediately without rebooting.
- **Fallback Persistence:** If the sysmodule is inactive, curves are saved to `/atmosphere/config/system_settings.ini` to take effect on subsequent boots.
- **Non-Conflicting Thermal Queries:** Reads SoC temperatures via the native `ts` (Thermal Measurement) service, avoiding IPC collisions with Atmosphere's `ptm`.

---

## 3. System Interface Translations

Manage custom system interface localizations under **Settings -> Translate Interface**:
- **Automated Upstream Firmware Matching:** Queries the official `NX-Family/NX-Translation` repository and automatically maps the console's firmware version (16.0.0 through 22.5.0+) to the appropriate release tag.
- **Diagnostic Transparency:** The menu header displays detected console region, target release tag, metadata endpoints, and direct download links.
- **Seamless Replacement:** When switching translations, old translation overlays are purged cleanly in the background before unpacking the new package, requiring only a single system reboot.
- **Offline Removal with Deferred Reboot:** Allows removing installed translations safely, offering either an immediate reboot or deferred continuation.

---

## 4. Forwarder Editor & SteamGridDB

Create custom HOME Menu forwarder NSPs for homebrew applications via **Tools -> Create Forwarder**:
- **SteamGridDB Integration:** Search SteamGridDB directly from the console for high-resolution vertical cover art. Paste an API key manually or use the built-in mobile handoff (`/apikey`) via QR code.
- **Automatic Forwarder Detection & EmuNAND Safeguard:** On startup, Kefir Hub checks if a forwarder is installed. If missing, it installs one silently in the background on EmuNAND. To protect clean setups, automatic forwarder installation is strictly suppressed on SysNAND and Semi-Stock.
- **Custom Launch Flags:** Configure address space limits (Automatic / 36-bit / 39-bit), video capture, and `svcDebug` flags per forwarder.

---

## 5. Theme Creator & Themezer Favorites

- **On-Console Theme Creator:** Convert any image (PNG, JPG) into an official `.nxtheme` package directly from the File Browser. Pan, zoom, and crop to native 16:9 aspect ratios, select target menus (Home Menu, Lock Screen, Settings, User Page), and automatically queue for installation in NXThemesInstaller.
- **Themezer Favorites & Filters:** Press **R3** on any Themezer pack to bookmark it to your favorites list for offline access. Filter by target layout and search tags.
