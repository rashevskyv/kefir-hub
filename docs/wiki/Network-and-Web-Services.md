# Network & Web Services

Kefir Hub embeds a powerful suite of network services and client integrations, turning the Nintendo Switch into a versatile networked device.

---

## 1. Web File Manager & Screenshot Gallery

Start the web server by pressing **Plus (+)** in the File Browser or Tools menu and choosing **Start Web Server**. Access the interface on your PC or smartphone at `http://kefir.local` (via mDNS) or `http://<switch-ip>` on standard HTTP port 80 (with automatic 8080–8090 fallback).

### Features:
- **Single Page App (SPA) Navigation:** Transitioning between folders is completely dynamic and does not trigger browser page reloads. The interface queries directory listings via JSON dynamically, keeping the upload/download queue state active even when navigating through folders.
- **Sequential Queue with Cancellation:** Features a robust upload/download queue that runs transfers sequentially one after the other. Each entry in the queue displays its own individual progress bar, speed tracker, and an independent cancel button (marked as an 'X') on the right to terminate transfers on the fly.
- **Direct Game Installation (NSP/NSZ/XCI/XCZ):** When adding game files to the upload queue, you can check the "Install directly" option. The web server will stream the incoming HTTP upload socket data directly to the Switch's internal game installer (`yati`) on the fly, installing the game directly on the console without saving the intermediate file onto the SD card.
- **Touch-Interactive Stop Button:** The console's server wait dialog includes a prominent touch-enabled red **Stop** button and a sub-label helper ("Press B to Stop Server") to easily terminate the server and exit the dialog.
- **Checkbox Selection & Batch Operations:** Displays checkboxes next to files and directories in list and grid views, allowing bulk selection. The bottom toolbar provides options to delete all selected items or download them collectively.
- **Recursive Directory Deletion:** Select folders and delete them recursively directly from the browser window (performing safe recursive deletion on the console's filesystem).
- **Bulk Download as ZIP:** Select multiple files or entire folders to download them as a single packaged ZIP file. The ZIP archive is generated on-the-fly directly in the client browser's memory without compression, shifting the processing load entirely to the user's computer and keeping the console's CPU and RAM free.
- **Direct Image Viewer:** Image files (PNG, JPG, JPEG, GIF, BMP) are highlighted as `[I]` in the directory listing and open directly on the page in a seamless lightbox viewer.
- **Dedicated Screenshot Gallery:** Serves a beautiful, interactive gallery at `/album` that scans the console's `/Nintendo/Album` folder. The screenshots and videos are sorted chronologically by date (newest first). The interface automatically decodes the Nintendo Switch screenshot filename structure (`YYYYMMDDHHMMSS00-TITLEID.ext`) to show formatted human-readable dates (e.g., `YYYY-MM-DD HH:MM:SS`) and looks up the Title ID to retrieve the game's actual display name. It features built-in video playback controls for MP4 captures, an adaptive grid view (4 columns on mobile), and quick switching links between the file browser and screenshots gallery.
- **Tools Menu Integration:** Press **Plus** (START) in the console's **Tools** menu (or any other option-enabled menu) to open the Network Server or context options. Launching the Web Server starts the universal shared instance providing both file browsing and screenshot management.
- **Remote Text & Input Handoff (`/input`):** Send URLs, API keys, or text directly from your phone or PC clipboard to the Switch.

---

## 2. Ownfoil Client

Kefir Hub includes an integrated client for [Ownfoil](https://github.com/a1ex4/ownfoil), the self-hosted Nintendo Switch library manager:
- **Zero-Config LAN Discovery:** Automatically discovers local Ownfoil instances on the local network; supports manual local and remote server address configurations with authenticated user logins.
- **Content-Based Browsing:** Browse curated categories including *New Games*, *Updates*, *DLC*, and *All Games*, or perform keyword searches.
- **Granular Package Selection:** View full version histories and DLC lists, selecting only desired updates and components to install.
- **Resilient Range Downloads:** Transfers use HTTP Range requests, automatically resuming interrupted connections for up to 60 seconds without restarting the transfer.
- **Content, Not Files:** Pick a game, a version and the DLC you want; Sphaira works out which files it needs however they are bundled (several NSP, or a multi-content NSP/XCI). Only what you picked is downloaded and installed. NSP, NSZ, XCI and XCZ are supported.
- **Servers & Artwork:** Each server can have both a local and a remote address, over HTTP or HTTPS. Lists are paginated and sortable. All artwork is served by the Ownfoil server, so the Switch never connects to Nintendo's servers.

---

## 3. NX-Link (Wireless NRO Transfer & Execution)

Deploy and launch homebrew NRO binaries directly over local Wi-Fi from command-line tools or development environments:
- **Filesystem Commit & Corruption Protection:** After receiving, writing, and renaming incoming NRO binaries, Sphaira executes explicit filesystem commit operations (`fs.Commit` and `fsdevCommitDevice("sdmc")`) before execution handoff (`launch_internal` / `envSetNextLoad`) and during process exit (`userAppExit`), guaranteeing FAT32/exFAT allocation tables and dirty blocks are fully flushed to the microSD card controller and preventing SD card drops or filesystem corruption on slow storage.
- **Race-Free Logging & Heap Protection:** Network log broadcasting in the background flusher utilizes direct non-blocking `send()` calls over socket descriptors rather than shared stdio streams (`stdout`), preventing newlib memory allocator collisions and eliminating heap chunk corruption (`_malloc_r` Data Abort `0x4A8`) during startup image decoding.
- **Path Normalization:** Received file paths are automatically sanitized (stripping redundant `sdmc:/` prefixes and enforcing native absolute paths), ensuring full compatibility with Horizon OS filesystem services.
- **Safe Buffer Bounds:** Arguments and connection packets are bounds-checked with guaranteed null-termination.

---

## 4. Wi-Fi Connection Manager

Manage wireless networks directly from **Tools -> Tools -> Wi-Fi** without leaving custom firmware:
- **Saved Networks List:** Displays all saved access points, signal details, and security types (`WPA2-PSK (AES)`, `WPA3`, `Open`). The currently connected network is highlighted at the top.
- **One-Click Connect (Button A):** Press **A** on any saved network to instantly connect to it.
- **View Stored Passwords:** In the context menu, select **View password & details** to reveal the Wi-Fi passphrase in plaintext.
- **Edit Network Profiles:** Rename SSIDs or update passphrases directly on-device using the software keyboard (`swkbd`).
- **Bulk Network Deletion:** Select multiple networks using **Button X** (or **Select All** in options) to delete old or obsolete access points in bulk.
- **Wireless Toggle:** Toggle the console's Wi-Fi radio on or off instantly from the sidebar.

---

## 5. FTP Server

FTP can be enabled via the network menu. It uses the same config as ftpsrv `/config/ftpsrv/config.ini`. [See here for the full list
of all configs available](https://github.com/ITotalJustice/ftpsrv/blob/master/assets/config.ini.template).

Once you have connected your ftp client to your switch, you can upload files to install into the `install` folder.

---

## 6. Remote Input & Direct Downloads

Sphaira provides an interactive **Remote Input** system that allows users to send URLs, API keys, or arbitrary text fragments to the console directly from a smartphone or PC:
- **Dual Input Modes:** Prompts offer **Manual (Keyboard)** for typing on the Switch's on-screen keyboard, or **From Phone / PC** for scanning a QR code or visiting a local web link (e.g. `http://kefir.local/input` or `http://<ip>/input`, with automatic 8080–8090 fallback).
- **Web Input Interface:** The mobile-responsive `/input` page features clipboard paste integration, live configuration reflection, and support for multiline text payloads.
- **Direct NRO & ZIP Downloads:** The **Custom Link / Direct Download** utility accepts both `.zip` archives (extracted to root with prompt to keep/delete) and standalone `.nro` binaries (saved directly to `/switch/` with an instant launch prompt).
