# Installation & USB Protocols

Kefir Hub features a unified package installation engine supporting local storage, network streaming, and USB host connections. It can install Nintendo Switch packages in **NSP**, **NSZ**, **XCI**, and **XCZ** formats with automatic storage destination management.

---

## 1. PC Install (USB)

Navigate to **Tools -> Install & Share -> PC Install (USB)**. Sphaira automatically negotiates the appropriate protocol when the host PC connects.

### Supported USB Protocols

1. **DBI Backend & SPHQ Protocol (DBI0):**
   - Compatible with `dbibackend.py` and modern GUI clients like **DBI Backend Qt**.
   - Supports random-access block reads and real-time package size queries.
   - **SPHQ Live Queue Synchronization:**
     - **Live Queue Modification:** The host PC can send an updated queue list (`SPHQ` packet with a revision counter) at any time, both while reviewing the queue and during active package installation.
     - **Safe Boundary Polling:** Sphaira inspects for queue updates immediately after finishing individual `FileRange` reads. It never interrupts in-flight reads or corrupts packet boundaries.
     - **Dynamic Future-Part Updates:** Changes (additions, removals, re-ordering) are applied dynamically to the uninstalled portion of the queue, while preserving the active package and completed items.
     - **Revision Acknowledgements:** Sphaira returns revision ACK responses to the host PC upon successfully applying changes.
     - **Empty Queue Handling:** Supports the 9-byte `::SPHQ::\n` marker to indicate an empty queue with clean DBI ACK confirmation.
     - **Live Per-Package Target Storage Re-evaluation:** In **Auto** storage mode, Sphaira re-evaluates the destination (NAND vs microSD) immediately before installing each package, checking the package's actual uncompressed size against real-time free capacity.
     - **Bidirectional Telemetry:** Reports package completion statuses (`Installed`, `User Skipped`, `Already Installed`, or `Failed`) via `CmdId::PackageStatus` (`0x04`) and current storage capacities via `CmdId::StorageInfo` (`0x05`).

2. **Awoo / TinFoil (TUL0 / TUC0):**
   - PC client pushes the file list, and Sphaira pulls file data ranges.
   - Supported by tools like **ns-usbloader** (in TinFoil mode) and **Fluffy**.

3. **GoldLeaf (GLCI / GLCO):**
   - Sphaira browses the remote virtual drive (`VIRT:/`) exposed by ns-usbloader (GoldLeaf v0.10+ mode) and queues selected packages.

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
