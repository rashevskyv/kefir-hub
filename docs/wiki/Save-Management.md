# Save Data Management

Kefir Hub provides a comprehensive, multi-source save management subsystem designed to inspect, back up, restore, synchronize, and repair save data across Horizon OS user profiles and system partitions.

---

## 1. Save Hub & Categories

Entering **Saves** from the Tools tab presents 3 dedicated categories navigable via shoulder buttons **L** and **R**:
1. **Installed Games:** Active save data for games currently installed on the console.
2. **Deleted Games:** Orphaned saves remaining on NAND for games that have been uninstalled. Deleting a save here cleanly cleans up unused space.
3. **Backups:** Discovered backup archives and directory structures stored across the microSD card (`/dumps`, `/switch/DBI/saves`, `/JKSV/`, `/switch/Checkpoint/`, and custom paths configured in settings).

**Backup destination:** **Saves → Backup → Default location** sets the folder preselected in Backup Options; you can choose another folder for an individual run. Game saves are written beneath the selected folder as DBI-compatible ZIP archives. `/switch/DBI/saves` and `/DBISaves` remain sources for restoring existing backups.

---

## 2. Multi-Source Backup Catalog & Grouping

In the **Backups** tab:
- **Organized by Source:** Backups are grouped with clear visual dividers indicating their originating tool:
  - **Kefir Hub** (ZIP archives created by Sphaira / Kefir Hub with `sphaira v` header comments)
  - **DBI** (DBI save ZIP archives and metadata descriptors)
  - **JKSV** (JKSV ZIP archives and directory backups)
  - **Checkpoint** (Checkpoint folder structures)
  - **Other** (Third-party or custom archives)
- **Compact Layout:** First-row and inter-source spacing is tightly optimized with crisp section label positioning and seamless circular navigation (jumping from the first entry directly to the last).
- **Icon Lookup:** If a game's local NACP icon is missing, Sphaira asynchronously queries and caches cover art by Title ID via NLib without stalling interface rendering.

---

## 3. Unpacked Folder Backups & Restore

Unlike traditional tools that only accept monolithic ZIP files, Kefir Hub natively discovers **unpacked save folders** created by JKSV or Checkpoint:
- When a folder backup containing a valid save payload and metadata is selected, Sphaira automatically stages the folder into a verified in-memory ZIP package.
- It validates archive integrity, creates necessary save slot targets on NAND, and writes the contents safely through closed filesystem handles and commits.

---

## 4. Restoring Saves for Uninstalled Titles

If you attempt to restore a save backup for a game that is not currently installed on the console:
- Sphaira reads the internal Title ID, save type (Account/Device/BCAT), rank, and byte-aligned journal and save data capacities directly from the archive's metadata.
- It safely synthesizes a new `Account` save slot on the target user profile using Horizon OS filesystem APIs before extracting the files.
- The process runs automatically without redundant confirmation prompts ("Create save slot and restore?"), failing closed if metadata is invalid or sizes cannot be verified.

---

## 5. Batch Restore ("Restore All")

When inspecting multiple backup slots for a single title:
- Clicking a game tile opens grouped categories (Account, Device, BCAT) partitioned by user profile and slot.
- Choosing **Restore All** performs pre-flight verification across all archives and target slots.
- Presents a single unified confirmation dialog displaying each source archive and destination slot, then restores all saves sequentially.

---

## 6. Game Tools Save Slot Manager

Under **Games -> Options -> Saves** for any installed game:
- View all active save slots and inspect exact `FsSaveDataInfo` attributes:
  - User Nickname & Type
  - Storage Space (`USER` vs `SYSTEM`)
  - Rank & Index
  - Save ID
  - Allocated Data Size, Journal Size, and Current Free Capacity
- **Create Save Slot:** Create a new primary or auxiliary save slot for any local user profile using NACP default quotas or aligned +16 MiB / +64 MiB presets.
- **Extend Save Slot (Grow Save Quota):** Safely expand save slot capacity on-the-fly (`ExtendSaveDataChecked`) if a game requires more space, avoiding crashes caused by save container exhaustion.

---

## 7. MTP Read-Only Save Protection & Cloud Sync

- **Read-Only MTP Saves:** When exposing save partitions over USB MTP (**Show NAND Saves**), saves are presented in a structured hierarchy (`Game Name [TitleID] / User Profile / ...`) in **read-only** mode to prevent host operating systems from inadvertently corrupting raw save containers.
- **WebDAV Cloud Synchronization:**
  - Synchronize backups with remote WebDAV servers via **Sync with remote**.
  - Enable **Auto-sync saves after backup** in Advanced Options to upload new backups immediately upon creation.
