# Installation & USB Protocols

Kefir Hub features a unified package installation engine supporting local storage, network streaming, and USB host connections. It can install Nintendo Switch packages in **NSP**, **NSZ**, **XCI**, and **XCZ** formats with automatic storage destination management.

Sources: microSD card, gamecard, FTP, USB, MTP, Web and Ownfoil. For the upstream install options see the [Sphaira wiki](https://github.com/ITotalJustice/sphaira/wiki/Install).

---

## 1. PC Install (USB)

Navigate to **Tools -> Install & Share -> PC Install (USB)**. Sphaira automatically negotiates the appropriate protocol when the host PC connects, so there is nothing to pick on the console; the queue names the detected protocol in the session log.

### Supported USB Protocols

1. **DBI Backend & SPHQ Protocol (DBI0):**
   - Compatible with `dbibackend.py` and modern GUI clients like **DBI Backend Qt**.
   - Supports random-access block reads and real-time package size queries. When the backend understands sphaira's list request it reports each file's size, so the queue shows real totals before the first byte is written.
   - **SPHQ Live Queue Synchronization:**
     - **Live Queue Modification:** The host PC can send an updated queue list (`SPHQ` packet with a revision counter) at any time, both while reviewing the queue and during active package installation.
     - **Safe Boundary Polling:** Sphaira inspects for queue updates immediately after finishing individual `FileRange` reads. It never interrupts in-flight reads or corrupts packet boundaries.
     - **Dynamic Future-Part Updates:** Changes (additions, removals, re-ordering) are applied dynamically to the uninstalled portion of the queue, while preserving the active package and completed items.
     - **Revision Acknowledgements:** Sphaira returns revision ACK responses to the host PC upon successfully applying changes.
     - **Empty Queue Handling:** Supports the 9-byte `::SPHQ::\n` marker to indicate an empty queue with clean DBI ACK confirmation.
     - **Live Per-Package Target Storage Re-evaluation:** In **Auto** storage mode, Sphaira re-evaluates the destination (NAND vs microSD) immediately before installing each package, checking the package's actual uncompressed size against real-time free capacity.
     - **Live Storage Target Selection:** Supports receiving target destination preferences (`Auto`, `microSD`, or `NAND`) per queue entry from PC client list descriptors (`file|size|selected|target`), adjusting planned installation targets and storage allocations dynamically.
     - **Bidirectional Telemetry:** Reports package completion statuses (`Installed`, `User Skipped`, `Already Installed`, or `Failed`) with the Horizon Result code via `CmdId::PackageStatus` (`0x04`) and current storage capacities via `CmdId::StorageInfo` (`0x05`).

2. **Awoo / TinFoil (TUL0 / TUC0):**
   - PC client pushes the file list, and Sphaira pulls file data ranges.
   - Supported by tools like **ns-usbloader** (in TinFoil mode) and **Fluffy**.

3. **GoldLeaf (GLCI / GLCO):**
   - Sphaira browses the remote virtual drive (`VIRT:/`) exposed by ns-usbloader (GoldLeaf v0.10+ mode) and queues selected packages. Browsing the PC's own filesystem (`HOME:/`) is not supported.

The queue reviews every package before it installs any of them, so a host in **stream mode** is refused with a message rather than served — turn stream mode off in the PC app.

### USB waiting screen

- **USB 3.0 & Link Speed Badge:** Automatically detects whether USB 3.0 is force-enabled in Atmosphère configuration (`system_settings.ini`) and queries real-time hardware link speeds, rendering a dedicated badge with vector USB iconography (e.g. `USB 3.0 SuperSpeed (5 Gbps)` vs `USB 2.0 High Speed (480 Mbps)`). When USB 3.0 is enabled in settings, an ambient `[ USB 3.0 ]` indicator also appears in the top status bar above the NAND/SD storage meters.
- **Dynamic Anti-Overlap Spacing:** All text boxes and Applet Mode warning cards dynamically calculate vertical rendered bounds (`nvgTextBoxBounds`), ensuring instructions, connection badges, and warning boxes remain perfectly spaced and never overlap across all screen orientations and translated languages.

### MTP file destinations

MTP can be enabled via the Network menu. You can configure which MTP storages are visible and set custom display names for them under **Settings -> Network -> MTP storages**. This allows you to toggle the visibility of the microSD card or the Install folder, and customize how they appear on your PC (e.g. setting a custom label instead of the default "microSD card"). If all storages are disabled, the MTP server will refuse to start and notify you.

- Copy directly to the **Nintendo Switch device** in Windows Explorer to install NSP, NSZ, XCI, or XCZ files; other files and folders are copied to the microSD card. The matching Install or microSD storage must be enabled.
- Copy NSP, NSZ, XCI, or XCZ files to the microSD storage to keep the files on the card. This applies to its root and subfolders; ordinary MTP copy progress is shown.
- Copy a package to the separate virtual **Install** storage to stream it to the installer without storing the package file on the microSD card. The minimized installation badge shows progress for the current package when its size is known.
- If another installation or storage operation is already in progress, an MTP install transfer is rejected with a notification.

### MTP drives and controls

- **Games Drive (read-only NSP dumping over USB):** Enabling **Show Games (read-only)** adds a drive that lists every installed title as a folder (`Game Name [TitleID]`), holding one NSP per installed component - the base game, its update and each DLC. The NSP does not exist on the microSD card: it is built from the installed content the moment you open the folder and streamed straight out of content storage, so copying one to the PC dumps that title without needing any free space on the console. Tickets are fetched and patched exactly as the Games menu dump does.
- **External MTP Devices (MTP Host Drive Support):** Connecting a smartphone or external media device in MTP mode via a USB OTG cable mounts its internal storage and SD card directly in the root of Sphaira's File Manager (`System Root`), alongside the microSD card and USB Mass Storage drives. You can browse, view, copy files between your phone and the console's SD card, and install games directly from external MTP devices.
- **Dynamic MTP Control in Tools:** The context menu in **Tools -> Install & Share** features a dynamic **Mount MTP** button. Once MTP is connected, the label automatically changes to **MTP: Active** (rendered in bold for high visibility). Clicking it again stops the MTP connection and reverts the label dynamically.
- **Robust Repack Installations:** The installation engine features enhanced error recovery when installing repacked or trimmed NSP/NSZ files via USB MTP. If a file is slightly truncated or missing non-critical padding bytes at the end of a stream (common in repacked titles), the installer automatically handles the EOF condition gracefully instead of failing with `Unexpected EOF` or `Invalid Read Size` errors, completing the installation successfully.
- **Streamlined Streaming Installation Controls (MTP, FTP, HTTP):** While installing via MTP, FTP, or HTTP Web Install, press **X** to cancel the installation session (protected by a confirmation prompt). Button **B** is unused during streaming installations to prevent interrupting in-flight network/USB host streams. When all packages conclude and no incoming transfer is active, a 3-second grace period allows the installer to settle before transitioning to the Summary screen.
- **MTP Install Cancellation:** Cancelling an active MTP installation on the console stops the current USB transfer without receiving the rest of the package. MTP reconnects in device mode so another transfer can be started without unplugging the cable.

---

## 2. Review Queue Controls

In the USB and batch installation queue review screen:
- **Button A:** Begin installing all selected packages.
- **Button X:** Toggle selection of the focused item and automatically advance cursor to the next item for rapid bulk selection.
- **Button Y:** Invert selection across all packages.
- **Button B:** Cancel and exit the queue review.

During active installation:
- **Button B:** Skip only the currently installing package and proceed immediately to the next queued item (guarded by confirmation dialog).
- **Button X:** Cancel the entire remaining queue (guarded by confirmation dialog).

---

## 3. Recursive Folder Installation ("Install Recursively")

You can install all packages inside a directory hierarchy in one step:
1. Open **File Browser**.
2. Highlight any folder, or mark multiple folders using **Button X**.
3. Press **Plus (+)** to open the sidebar context menu.
4. Select **Install recursively**.
5. Sphaira recursively scans all subdirectories, aggregates every discovered NSP, NSZ, XCI, and XCZ file, and presents them in the standard Review Queue.

---

## 4. Background Minimization & Multitasking

During USB PC installations or network streaming transfers:
- **Press L3 (Left Stick Click):** Minimizes the installation screen into a compact top-right status badge (e.g. `USB · 2/5 (68%)  Expand`).
- While minimized, you can freely browse file directories, inspect game libraries, view system information, and adjust settings.
- **Tap or Press L3:** Instantly restores the full-screen installation interface.
- Minimize/Expand is also available during the USB connection wait and review queue stages.

---

## 5. Screen-Off & Drifting Screensaver

To conserve battery and prevent OLED burn-in during lengthy installation queues:
- **Press Minus (-)** during installation to activate the screensaver.
- Configurable under **Settings -> Install -> Screen off (Minus)**:
  - **Lower brightness:** Dims the panel.
  - **Cut backlight:** Turns off panel backlighting on LCD models.
  - **Drifting readout:** Displays an animated black-background status monitor (time, package counter, progress percentage, transfer speed graph, ETA, and battery status).
- **Interactive Screensaver Controls:**
  - **Left Analog Stick:** Pan and steer the status readout anywhere across the display.
  - **Right Analog Stick (Up / Down):** Dynamically adjust display brightness.
  - **Right Analog Stick (Left / Right):** Increase or decrease drifting animation speed.
  - **Any Button:** Wakes the screen and restores the active installation view.
- While the screensaver is active, system auto-sleep and display turn-off are inhibited. OLED consoles keep the configured brightness against true black pixels; LCD consoles reduce brightness to conserve battery.
- **Clean Game Title Display:** The screensaver and installation view strip internal NCA/NCZ hash strings (e.g. `dd38de587cb690a36b1d4b6ca4.nca`), presenting clean, human-readable game titles (or package filenames when metadata is pending) and descriptive stage notices (such as database updates).
- **Adaptive Title Rendering:** The screensaver features an expanded 840px display track with adaptive font scaling and left-edge anchoring for lengthy titles, ensuring game names are always readable in full and never clipped at the start.

---

## 6. Storage Destination & Reserve

- **Storage Destination Priority:** Choose where titles are installed in Settings -> Install (defaults to **Automatic** for new installations):
  - **microSD card only:** Always install to microSD storage.
  - **System memory only:** Always install to NAND storage.
  - **System first, then SD:** Install to NAND; if NAND does not have enough free space (taking the reserve threshold into account), automatically fall back to microSD.
  - **SD first, then system:** Install to microSD; if microSD space is below the reserve threshold, fall back to NAND.
  - **Automatic:** Install to whichever storage has the most free space (NAND or microSD) after verifying that both satisfy the reserve threshold.
- **Customizable Reserve Threshold:** Set the free space reserve threshold in Megabytes (MB) via Settings -> Install -> "Reserve free space" (opens an on-screen numpad). If a target storage doesn't meet the reserve limit during installation, the installer falls back to the secondary storage or warns the user.
- **Dynamic Storage Target Selection:** Automatically determines the target storage (SD Card vs System Memory) for each game installation. It estimates the uncompressed size of the package (`1.6 * compressed_size` for compressed formats like NSZ/XCZ, or the file size for NSP/XCI) and checks the available NAND USER space. If installing to System Memory leaves at least 500 MB of free space, it selects System Memory; otherwise, it defaults to the microSD Card. This applies to network, USB, and local installations.
