# Kefir Hub Wiki

Welcome to the **Kefir Hub** documentation wiki. Kefir Hub is a high-performance, feature-packed homebrew menu, application installer, save manager, and system utility suite for the Nintendo Switch, built upon the upstream Sphaira project.

---

## Table of Contents

- [Home](Home.md) — Documentation index and overview
- [Installation & USB](Installation-and-USB.md) — USB PC Install, live SPHQ queue synchronization, recursive folder installs, Ownfoil, and streaming transports
- [Save Management](Save-Management.md) — Multi-source backups (Kefir Hub, DBI, JKSV, Checkpoint), folder-based restores, uninstalled game save creation, Game Tools save slot manager, and WebDAV cloud sync
- [User Profiles & Account Linking](User-Profiles-and-Account-Link.md) — Profile management, RomFS donor offline Nintendo Account linking, Official vs Fake status detection, avatar customization, and portable backups
- [Console Transfer & TegraExplorer](Console-Transfer.md) — Over-the-Air (OTA) wireless migration of user profiles, playtime hours, and save packs between Nintendo Switch consoles
- [System Firmware & Downgrade Recovery](Firmware-and-Downgrades.md) — System firmware updates, ZIP/folder manual installs, automated post-downgrade recovery script, and theme/translation cleanup
- [Network & Web Services](Network-and-Web-Services.md) — Web File Manager, Ownfoil client, NX-Link, FTP server, and Wi-Fi connection manager
- [Interface & Navigation](Interface-and-Navigation.md) — Tools hub, layouts, header, image viewer, file browser and network sources
- [System Utilities & Customization](System-and-Tools.md) — Module Manager with RAM tracking, custom Fan Curves, System Interface Translations, Forwarder Editor, and Theme Creator

---

## Core Architecture & Highlights

1. **Non-Blocking Multitasking:**
   - Background USB and network installations can be minimized to a compact badge (**L3**) while navigating file systems, game libraries, or settings.
   - Built-in drift-capable screensaver (**Minus (-)**) protecting OLED panels and battery during long transfers.

2. **Bidirectional Protocol Integration (SPHQ):**
   - Seamless integration with PC clients (such as DBI Backend Qt) featuring live queue updates, package reordering, storage re-evaluation, and status reporting during active transfers.

3. **Multi-Source Save Hub:**
   - Unified indexing of backups created by Kefir Hub, DBI, JKSV, and Checkpoint.
   - Full support for both unpacked directory structures and ZIP archives.
   - Restoring saves onto clean systems or uninstalled games by synthesizing save slot metadata from backup archives on the fly.

4. **Offline Account Linking & Identity Classification:**
   - Link Nintendo Accounts entirely offline using built-in RomFS donor packages without needing third-party tools.
   - Real-time distinction between Official Nintendo Accounts and offline-linked accounts via Horizon BaaS Administrator IPC.

5. **Automated Payload Automation:**
   - Bundles TegraExplorer in RomFS with automated payload version synchronization.
   - Stages automated `/startup.te` scripts for dumping/restoring locked system saves (`0010`, `00F0`) and post-downgrade fixes (`0073`), with automatic return to Hekate.
