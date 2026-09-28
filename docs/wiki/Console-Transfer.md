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
- **Embedded RomFS Payload:** Bundles the latest verified TegraExplorer binary directly in RomFS (`romfs:/tegra/TegraExplorer.bin`).
- **Automatic Payload Version Sync:** On launch, Sphaira inspects `/bootloader/payloads/TegraExplorer.bin`. If missing or older than the embedded version (verified by the `KFRP` binary footer), it is updated in-place automatically.
- **Automated Script Staging:** Sphaira stages a non-interactive `/startup.te` script to the root of the microSD card and requests an automated reboot to the payload.
- **Hekate Payload Launch & Swap Fallback:** Communicates with Hekate's one-shot payload launch API (`/config/kefir/hekate-payload-request.ini`). If running under older bootloader configurations, it seamlessly executes a safe payload swap (`/payload.bin` <-> `/bootloader/update.bin`) and configures temporary autoboot in `hekate_ipl.ini` before rebooting.
- **Diagnostic Dashboard UI:** TegraExplorer scripts run inside an animated full-screen diagnostic dashboard displaying hardware spinners, partition mounts, individual save states (`0010`, `0011`, `00F0`, `0041`), files processed, and rolling event logs.
- **5-Second Auto-Reboot:** Upon successful dump or restore, the script cleans up temporary files, disarms the swap, restores the original `payload.bin` and `hekate_ipl.ini`, and reboots back into Hekate automatically after a 5-second countdown.

---

## 3. Cryptography & System Safety

For technical details on Nintendo Switch system save encryption (BIS partition encryption vs save MAC keys) and why Horizon OS internal file extraction is used instead of raw blob cloning, consult the technical guide:
- [account-transfer.md](../account-transfer.md)
