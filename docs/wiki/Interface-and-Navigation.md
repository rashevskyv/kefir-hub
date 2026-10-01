# Interface & Navigation

Layouts, navigation rules, the Tools hub, the image viewer and the file browser.

---

## 1. Tools Hub

The **Tools** tab provides quick access to core utilities and settings in an organized grid:
- **Row 1:** **File Browser** (manage SD card files), **Games** (installed titles manager), **Themes** (theme packs).
- **Row 2:** **Updater** (Kefir & firmware updates), **Saves** (Save Hub & DISA backup/restoration), **Software** (Homebrew App Store, DBI installer, and community mod utilities).
- **Row 3:** **Cheats** (cheat databases & dmnt cheats manager), **Kefir Settings** (fan curves & system switches), **Settings** (app settings).
- **Row 4:** **Tools** (sysmodule & background module manager, fan curves, Wi-Fi connections, and user profile manager).

---

## 2. Navigation

- **Dynamic Navigation & Minus Button:** Pressing **Minus (-)** dynamically detects the current location: if already on the Homebrew screen, it exits the application; if inside any nested file or folder picker (such as avatar or path selection), it cleanly cancels the picker and returns to the calling menu; if pressed from any other screen, tool, submenu, or sidebar, it immediately navigates straight back to the Homebrew screen in a single press.

---

## 3. Display Layouts

Sphaira supports multiple display layouts for homebrew and games, customizable to suit your preference:
- **Storage Status Bar:** The status bar displays the current network IP address (or a localized "No Internet" status), dual NAND and SD storage capacity bars (color-coded green, yellow, and red based on usage), clock, and battery percentage details. Long Wi-Fi SSID and IP strings automatically scroll in a dedicated bounded marquee slot above the clock/battery block, preventing any overlap with the NAND/SD storage indicators. The positioning is static in both states to prevent interface shift, displaying a static green lightning bolt icon after the numbers during charging, and a standard percent symbol when discharging.
- **NACP v2 Support:** Added compatibility for parsing the new compressed NACP metadata format introduced in Nintendo Switch firmware 20.0+, ensuring titles and authors display correctly.
- **Grid & Icon Views:** Grid and Icon views support seamless row-to-row navigation (pressing **Right** on the last item of a row moves the cursor directly to the next row, and **Left** on the first item moves back). In File Browser and file picker icon layouts, folder and file names remain consistently centered under tiles in all states (unfocused and selected), preventing unwanted label shifts to the left edge when hovering with the cursor.
- **HB Menu Layout:** Replicates the classic Nintendo Switch Homebrew Menu style. It displays a large icon of the selected app on the left along with detailed metadata (Name, Author, Version) on the right, and lists all available applications in a horizontal row at the bottom. The horizontal row uses custom dual-banner cards (showing the clean filename in a white banner on top, and the full-sized icon below).
- **Animated Waves:** An animated wave background (reproducing the classic hbmenu background) runs along the bottom of the screen. This can be enabled or disabled via "Settings -> Appearance -> Animated waves". Its colors are fully customizable in `/config/kefir/config.ini` by specifying `wave_color_dark` (for dark themes) and `wave_color_light` (for light themes) as hex values (e.g. `0x00FFC8`). If left blank, it automatically resolves to the active theme's highlight colors.
- **Charging Indicator:** When charging, the battery percentage numbers are displayed in a clean green color with a static lightning bolt icon on the right, maintaining a consistent size and layout to align perfectly with other status bar elements.

---

## 4. Game Details & Header

- **Header Storage Bars, Services & System Info:** Real-time NAND and SD card storage indicators with compact, high-legibility font sizing and right-aligned status indicators (clock, battery, Wi-Fi SSID and IP address with anti-overlap marquee scrolling). Prominent **MTP**, **FTP**, **USB 3.0**, and **EmuNAND / SysNAND status badges** sit directly above the storage meters (adaptively displaying `[ ● EmuNAND ]` in full or compact `[ ● E ]` when USB 3.0 is active). System information in the version block displays pure versioning (`<Kefir> · <FW>|AMS <AMS>`) with 3-way symmetric spacing across the storage span ($M = (W_{span} - (W_1 + W_2)) / 3$).
- **Logical Stat Blocks:** Game details statistics are neatly organized into 4 logical blocks (Title ID & Version, Languages & Mods folder, Play time & Last played, Components/Tickets/Saves & Save quota). All values within each block align strictly to a single vertical column, with long translated labels automatically scrolling when exceeding 1/3 of the row width.

---

## 5. Image Viewer

- **Uncluttered Header & Full-Width Title:** NAND and SD storage bars are cleanly omitted in the image viewer, giving the filename the full header space from the left margin up to the clock.
- **Adaptive Title Scaling & Scrolling:** If an image or menu title is long (e.g. Switch screenshot filenames like `2026081517021600-57B4628D2267231D57E0FC1078C0596D.jpg`), the font size automatically scales down by up to 40% (down to 16.8px) to fit the available space, and seamlessly scrolls if it still exceeds the space at minimum font size.
- **Two-Row Pixel-Balanced & Justified Footer Legend:** When an extensive set of actions is active or when translated text exceeds single-line width, footer action hints automatically format across two rows balanced by occupied pixel width (minimizing width disparity between rows). Each row dynamically distributes spacing between items to occupy the full width of the footer (`30px` to `1220px`), providing large fonts and complete edge-to-edge touch hitboxes with zero dead zones.
- **Custom Legend & Standard Chrome:** Clear bottom-bar indicators (unified `Prev / Next Image` with `\uE0ED / \uE0EE` for D-Pad Left/Right, `Zoom Up / Down` for ZL + Stick Up/Down, and `Full Screen` for ZR). Normal view preserves standard screen header/footer chrome above image content, while Full Screen mode expands to full display without chrome.
- **Zoom & Navigation:** Holding ZL with Analog Stick / D-Pad Up or Down zooms in or out without accidentally changing images.
- **Stick Panning:** Releasing ZL while zoomed in enables smooth pan/scroll across the zoomed image using analog sticks or D-Pad without scale changes or switching files.
- **On-the-Fly Rotation:** Press **L** to rotate counter-clockwise (90°) or **R** to rotate clockwise (90°). Rotation dynamically adapts viewport framing, zoom, and panning bounds in memory without modifying image files on disk.

---

## 6. File Browser

Sphaira includes a robust file manager with standard operations (Cut, Paste, Rename, Delete, Create File/Folder, Extract/Compress zip, Install/Forwarder) and write protection handling:
- **Nested File Browser Cancel (Minus Button):** When the File Browser is launched as an in-app file or folder picker (such as selecting custom avatar images, firmware directories, or backup search paths), pressing **Minus (-)** cleanly cancels the picker and returns to the previous menu rather than exiting the application.
- **View Options & Vector Iconography:** Toggle between **List** and **Icon** / thumbnail preview mode in the **View** submenu alongside sorting (by size or name) and visibility settings. In grid view mode, folder entries render a sharp, resolution-independent vector silhouette with consistent label centering under tiles in all hover states.
- **Network Storage Sources (SMB, WebDAV, FTP, HTTP):** Mount and browse network folders directly in the file manager. Select "+ Add network location" in the "Sources" settings category or directly in the file browser sources picker. Supported protocols include Samba (SMB), WebDAV (HTTPS/HTTP), FTP, and HTTP. Connection is established asynchronously using a progress screen and locations are saved to `/config/kefir/locations.ini` (Note: credentials are saved in plain text for compatibility with NXMP). You can browse network folders as native directories, perform file operations (Copy, Paste, Delete, Rename, Create Folder), play audio or video files from them using NXMP, and upload files to them.
  - **Hierarchical System Root Navigation:** Pressing **Back (B)** at the root of microSD card or any mounted storage navigates one level up to a virtual **System Root** view instead of exiting the file browser. This view lists the microSD card, system partitions (NAND/SD Image if God Mode is enabled), and all configured network locations.
  - **Connection Status Badges:** Configured network locations in the System Root view display a visual connection status indicator in the bottom-right of their folder icon: Green (connected/mounted), Grey (unknown/disconnected), and Red (failed/error). Selecting a disconnected location automatically triggers a connection attempt and mounts it.
  - **Right-Aligned Sidebar Context Menu:** Pressing **Plus** (START) on any network location in Settings opens a modern right-aligned sidebar options menu (replacing the old bottom popup list) to quickly Connect, Edit, Rename, Test, View Properties, or Delete the location.
  - **Connection Testing:** Includes a dedicated "Test Connection" tool both in the location options sidebar and the individual edit menu. It executes a real-time connection check (using `CSMB2FS` query for SMB and light HTTP/FTP metadata queries via `curl` for other protocols) and displays real-time progress followed by success/failure notifications.
  - **Auto URL Formatting on Input:** When adding a new network location, if you enter a URL-like string (containing schemes, hostnames, or local IP addresses) in the "Location Name" Swkbd prompt, it is automatically parsed and copied over as the target Server URL with the correct protocol scheme prefix (e.g., `smb://`, `webdav://`, `ftp://`, or `http://`) pre-filled.
- **NXMP Media Player Integration:** When selecting audio (MP3, OGG, FLAC, WAV, etc.) or video (MP4, MKV, AVI, TS, etc.) files on the SD card, you can choose "Play with NXMP" from the options sidebar. It will launch the external NXMP media player directly, passing the file's SD card path as an argument. If NXMP is not installed on the console, it prompts the user to open the App Store to download it.
- **Looping Menu Navigation:** Option sidebar lists feature looping circular navigation (pressing UP on the first item wraps to the last, and vice-versa).
- **Enhanced Selection Checkboxes:** Checkboxes shown when marking multiple files (triggered by X/Y) are enlarged to 20px, shifted left into the empty margin (-30px) to prevent overlapping filenames, and feature a larger 18px checkmark icon for improved readability.
- **User-Friendly Error Mapping:** When filesystem operations fail (e.g., target file locked due to taking a screenshot, path too long, invalid characters, write protection), the error popup displays a helpful, localized description of the problem and how to resolve it.
- **Polished Option Dialogs:** Option boxes and confirmation popups (such as the web folder sharing QR code) feature optimized text line-height spacing (`1.4f`), dynamic height auto-scaling to eliminate excess empty space, and vertical centering next to images/QR codes.
- **Write Protection Support:** If a file or folder is marked as Read-Only (and "Ignore read only" is disabled in Advanced Settings), destructive or modification actions such as **Cut**, **Rename**, **Delete**, **Paste**, **Create File**, and **Create Folder** are automatically disabled and grayed out in the options sidebars, clearly showing the reason when selected.
