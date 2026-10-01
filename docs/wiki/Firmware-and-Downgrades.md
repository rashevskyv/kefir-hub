# System Firmware Updates & Downgrade Recovery

Kefir Hub provides comprehensive management for updating and downgrading Nintendo Switch system firmware directly on-device through **Kefir Updater**, featuring automated error prevention and post-downgrade recovery.

---

## 1. Firmware Installation

- **Online Downloads:** Download official firmware update archives directly from configured community repositories.
- **Manual Installation:** Select local firmware packages from the microSD card using either:
  - Standalone `.zip` archives (extracted and validated in staging cache)
  - Unpacked firmware folders holding individual `.nca` / `.cnmt` components

---

## 2. Automated Post-Downgrade Fix (`downgrade_fix.te`)

When installing an older system firmware version than the one currently active (a downgrade), Horizon OS system services lock system save `8000000000000073`. If this save remains on the partition after rebooting into the older firmware, the console will crash during the boot sequence with a fatal system error.

### How Kefir Hub Automates Recovery:
1. **Detection:** Sphaira detects that the target firmware version is lower than the active running firmware.
2. **Script Staging:** Automatically stages `/assets/romfs/tegra/downgrade_fix.te` to the SD root as `/startup.te`.
3. **Automated Payload Boot:** Automatically configures target partition pointers (`SYSTEM` on EmuNAND or SysNAND) and reboots into TegraExplorer via Hekate.
4. **Execution & Auto-Reboot:** The script disarms itself immediately to prevent bootloops, mounts the target partition, deletes save `8000000000000073`, verifies removal, cleans up temporary files, and reboots cleanly back into Hekate without requiring any manual button presses.

---

## 3. Automatic Themes & Translations Cleanup

Installing a system firmware update over existing Atmosphere custom themes or system interface translations frequently triggers fatal crashes (`2162-0002` or qlaunch panics) due to layout and bytecode incompatibilities:
- **Unconditional Safe Purge:** Immediately upon completing any firmware upgrade or downgrade, Sphaira automatically purges all installed custom themes (`0100000000001000`, `0100000000001013`, `0100000000001007`, `00FF007468656D65`) and interface translations (`0100000000000803` through `0100000000001015`) from `/atmosphere/contents/`.
- **Application Files Preserved:** Application-specific homebrew resources (such as DBI translations) are explicitly preserved.

---

## 4. Maintenance Mode Recovery

If boot issues arise following a downgrade, the downgrade warning dialog provides detailed instructions for entering Horizon Recovery / Maintenance Mode:
1. Turn on the console and wait for Nintendo and Kefir boot logos to pass.
2. Press and hold both **Volume (+)** and **Volume (-)** buttons simultaneously until Maintenance Mode opens.
3. Select **Initialize Console Without Deleting Save Data** (resets installed game titles and system settings while strictly preserving your game save files). After initialization the SD card's `Nintendo` folder becomes invalid and the console prompts to delete it; agreeing does NOT affect saved games.
4. Direct guide access is available via the scan-ready QR code pointing to `https://switch.customfw.xyz/downgrade_fw`.

---

## 5. Localization

- **Fully Localized Update & Reboot Notifications:** All firmware update confirmation prompts, validation error messages, downgrade recovery notices, theme/translation cleanup warnings, and post-installation reboot requests are fully localized through Sphaira's `i18n` translation engine across supported languages.
