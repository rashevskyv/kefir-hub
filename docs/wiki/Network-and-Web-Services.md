# Network & Web Services

Kefir Hub embeds a powerful suite of network services and client integrations, turning the Nintendo Switch into a versatile networked device.

---

## 1. Web File Manager & Screenshot Gallery

Start the web server by pressing **Plus (+)** in the File Browser or Tools menu and choosing **Start Web Server**. Access the interface on your PC or smartphone at `http://kefir.local` (via mDNS) or `http://<switch-ip>` on standard HTTP port 80 (with automatic 8080–8090 fallback).

### Features:
- **Single Page App (SPA) Interface:** Smooth navigation without browser reloads; active upload and download queues persist as you move between folders.
- **Sequential Transfer Queue:** Transfers files one by one with individual progress bars, real-time speed metrics, and per-item cancellation.
- **Direct Streamed Game Installation:** Check "Install directly" when uploading NSP, NSZ, XCI, or XCZ packages to stream data directly into the console's game installer without using intermediate SD space.
- **Browser-Generated ZIP Downloads:** Select multiple files or entire directories and download them packaged as a single ZIP archive. The archive is created on-the-fly inside your browser's RAM, offloading CPU and memory pressure from the Switch.
- **Dedicated Screenshot Gallery (`/album`):** View screenshots and captured MP4 gameplay videos chronologically. File names are decoded to human-readable dates and Title IDs are mapped to official game titles.
- **Remote Text & Input Handoff (`/input`):** Send URLs, API keys, or text directly from your phone or PC clipboard to the Switch.

---

## 2. Ownfoil Client

Kefir Hub includes an integrated client for [Ownfoil](https://github.com/a1ex4/ownfoil), the self-hosted Nintendo Switch library manager:
- **Zero-Config LAN Discovery:** Automatically discovers local Ownfoil instances on the local network; supports manual local and remote server address configurations with authenticated user logins.
- **Content-Based Browsing:** Browse curated categories including *New Games*, *Updates*, *DLC*, and *All Games*, or perform keyword searches.
- **Granular Package Selection:** View full version histories and DLC lists, selecting only desired updates and components to install.
- **Resilient Range Downloads:** Transfers use HTTP Range requests, automatically resuming interrupted connections for up to 60 seconds without restarting the transfer.

---

## 3. NX-Link (Wireless NRO Transfer & Execution)

Deploy and launch homebrew NRO binaries directly over local Wi-Fi from command-line tools or development environments:
- **Filesystem Commit Protection:** Flushes FAT32/exFAT allocation tables and dirty blocks via explicit device commits before handoff, ensuring binaries are fully synced to SD.
- **Memory Safety:** Direct socket operations prevent standard I/O collisions and memory fragmentation during network execution handoffs.

---

## 4. Wi-Fi Connection Manager

Manage wireless networks directly from **Tools -> Tools -> Wi-Fi** without leaving custom firmware:
- **Saved Networks List:** Displays all saved access points, signal details, and security types (`WPA2-PSK (AES)`, `WPA3`, `Open`). The currently connected network is highlighted at the top.
- **One-Click Connect (Button A):** Press **A** on any saved network to instantly connect to it.
- **View Stored Passwords:** In the context menu, select **View password & details** to reveal the Wi-Fi passphrase in plaintext.
- **Edit Network Profiles:** Rename SSIDs or update passphrases directly on-device using the software keyboard (`swkbd`).
- **Bulk Network Deletion:** Select multiple networks using **Button X** (or **Select All** in options) to delete old or obsolete access points in bulk.
- **Wireless Toggle:** Toggle the console's Wi-Fi radio on or off instantly from the sidebar.
