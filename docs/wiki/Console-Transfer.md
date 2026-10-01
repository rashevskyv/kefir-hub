# Console Transfer & TegraExplorer Automation

Kefir Hub features a dedicated **Console Transfer** system allowing complete migration of user profiles, playtime hours, and save packs between Nintendo Switch consoles wirelessly over local Wi-Fi, paired with automated TegraExplorer execution to safely dump and restore locked system saves.

---

## 1. Over-the-Air (OTA) Wireless Console Transfer

No manual SD card swapping or PC intervention is required to move data between two consoles on the same local Wi-Fi network:

### Sending Console:
1. Navigate to **Manage Backups** (in Users or Saves options).
2. Press **Plus (+)** to open the context sidebar and select **Send to another console**.
3. Kefir Hub starts a lightweight HTTP sharing server and displays the local IP address and connection instructions.

### Receiving Console:
1. Navigate to **Manage Backups** or **Restore profiles & play hours**.
2. Press **Plus (+)** and select **Receive from another console** (or **Restore from another console**).
3. Enter the sender console's IP address.
4. Browse available remote backup packs, select target packs (or choose all), and download them directly to `/config/kefir/nand_transfer/`.
5. Sphaira verifies download integrity and immediately prompts to restore profiles and playtime via automated TegraExplorer staging.

---

## 2. TegraExplorer Payload Integration

Certain system saves—notably `8000000000000010` (Account definitions) and `80000000000000F0` (Play History & Play Events)—are locked by active Horizon OS system services (`FsError_TargetLocked`) while the operating system is running. Kefir Hub bypasses this limitation safely using an automated TegraExplorer payload handoff.

### Key Capabilities:
- **Embedded RomFS TegraExplorer & Version Synchronization:** Sphaira bundles the latest compiled TegraExplorer payload in its RomFS (`romfs:/tegra/TegraExplorer.bin`). Before launching any payload operation (`ensureTegraExplorerPayload`), Sphaira checks `/bootloader/payloads/`:
  - If TegraExplorer is missing from the SD card, it is automatically installed from RomFS.
  - If a copy exists, Sphaira reads and parses the binary payload footer (`KFRP`). If the SD card copy is older than the RomFS version, it is safely upgraded in-place.
  - If the SD card already contains a matching or newer version, it remains untouched.
- **Seamless Automated Reboot to TegraExplorer:** When performing **Backup profiles & play hours**, Sphaira automatically stages the dump package metadata and writes the automation script to the root of the SD card as `/startup.te`. Without requiring intermediate confirmation dialogs, it automatically executes a clean reboot into TegraExplorer.
- **Hekate Payload Launch & Swap Fallback:** Payload launching (`utils::rebootToPayload`) natively communicates with Hekate's one-shot payload launch API (`/config/kefir/hekate-payload-request.ini`). If the capability marker is missing or writing fails, Sphaira seamlessly falls back to swapping `/payload.bin` with the target payload while safely preserving Hekate in `/bootloader/update.bin`, alongside configuring temporary autoboot in `hekate_ipl.ini` (safely backed up to `.bak`) before requesting a system reboot via `appletRequestToReboot`/`spsm`/`bpc`.
- **Diagnostic Dashboard UI & Live Spinner:** Both dump and restore automation scripts in TegraExplorer run inside a full-screen diagnostic dashboard featuring a custom pixel-rendered header banner (`setpixels`), an animated hardware spinner (`spinner(1, 77, 0)`), real-time status tables (Source NAND, Target Pack, individual save status for `0010`, `0011`, `00F0`, `0041`, files written, and error counts), safe line-length protected activity tickers, and rolling event logs (`Event Log`).
- **Result Verification, Payload Restore & 5-Second Auto-Reboot:** When dump or restore scripts run in TegraExplorer, they immediately disarm the swap and restore Hekate from `sd:/bootloader/update.bin` back to `sd:/payload.bin`, as well as restoring `hekate_ipl.ini`. Upon completion (or failure), they display a clear color-coded summary, cleanly clean up temporary files, ensure `/payload.bin` is restored, and automatically reboot back into Hekate (`sd:/bootloader/update.bin`, `sd:/payload.bin` on Kefir builds) after a 5-second countdown grace period without requiring manual button presses.
- **Flushed SD Synchronization & Automatic Cleanup:** Staged `/startup.te` scripts are flushed via `fflush`, `fsdevCommitDevice("sdmc")`, and native filesystem commits before reboot commands are issued, ensuring zero-byte corruption is prevented even during sudden hardware restarts. Upon completion in TegraExplorer or cleanup inside Kefir Hub, temporary `/startup.te` and handshake files are automatically purged and the original `hekate_ipl.ini` is restored.

---

## 3. Cryptography & System Safety

For technical details on Nintendo Switch system save encryption (BIS partition encryption vs save MAC keys) and why Horizon OS internal file extraction is used instead of raw blob cloning, consult the technical guide:
- [account-transfer.md](../account-transfer.md)

---

## 4. Manage Backups Context Menu

- **Manage Backups Context Menu, Remote Transfer & Legend Parity:** Entering **Manage Backups** (for both individual user backups and NAND profiles & play hours packs) provides full context menu support via **Plus (+)** (or tapping **Options** on the touch bar), mirroring all legend actions with clean vector iconography:
  - **Open & Direct Restore:** Open pack details to inspect accounts or trigger a direct **Restore** immediately from the context menu (with profile only or profile + playtime options).
  - **Custom Renaming:** Easily rename backup folders or archives with on-screen keyboard (`swkbd`) validation and sanitization.
  - **Send to Another Console:** Start the Console Transfer share server directly from the backup menu to transfer user backups or profiles & playtime packs to another Nintendo Switch or PC over local Wi-Fi.
  - **Receive & Restore from Another Console (Over-the-Air Console Move):** Transfer profiles and playtime packs directly between consoles over local Wi-Fi without manual SD swapping:
    - **Receive from another console:** In **Manage Backups** (`+` Options) or the Tools -> Users sidebar, enter the sending console's IP address to browse remote packs and download selected backups (or all backups at once) to `/config/kefir/nand_transfer/`.
    - **Restore from another console:** In **Restore profiles & play hours** (`+` Options), individual pack details, or the Tools -> Users sidebar, enter the sender's IP address to select a remote backup. Sphaira downloads the pack locally to SD first, then immediately prompts to restore profiles (or profiles + play hours) via automated TegraExplorer staging.
  - **Complete Selection & Legend Parity:** Full access to **Select / Deselect** (toggling focused item, mirroring Button **X**), **Select All**, **Clear selection** (mirroring Button **B**), **Invert** (mirroring Button **Y**), and **Delete** (mirroring Button **Minus** / Select) directly from the options menu for inattentive users who prefer using the context menu over gamepad button shortcuts.
