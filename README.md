# Sphaira

A homebrew menu for the Nintendo Switch.

[See the GBATemp thread for more details / discussion](https://gbatemp.net/threads/sphaira-hbmenu-replacement.664523/).

[We have now have a Discord server!](https://discord.gg/8vZBsrprEc) Please use the issues tab to report bugs, as it is much easier for me to track.

## Showcase

|                          |                          |
:-------------------------:|:-------------------------:
![Img](assets/screenshots/homebrew.jpg) | ![Img](assets/screenshots/games.jpg)
![Img](assets/screenshots/appstore.jpg) | ![Img](assets/screenshots/appstore_page.jpg)
![Img](assets/screenshots/file_browser.jpg) | ![Img](assets/screenshots/launch_options.jpg)
![Img](assets/screenshots/themezer.jpg) | ![Img](assets/screenshots/web.jpg)

## Bug reports

For any bug reports, please use the issues tab and explain in as much detail as possible!

Please include:

- CFW type (i assume Atmosphere, but someone out there is still using Rajnx);
- CFW version;
- FW version;
- The bug itself and how to reproduce it.

## FTP

FTP can be enabled via the network menu. It uses the same config as ftpsrv `/config/ftpsrv/config.ini`. [See here for the full list
of all configs available](https://github.com/ITotalJustice/ftpsrv/blob/master/assets/config.ini.template).

## MTP

MTP can be enabled via the Network menu.

### Web File Manager

Sphaira includes an HTTP-based Web File Manager (accessed via port 8080 when enabled under the file options via the **Start Web Server** action, which is localized across all 14 languages) to browse, download, upload, delete, and view files on the console directly from a web browser:
- **Single Page App (SPA) Navigation:** Transitioning between folders is completely dynamic and does not trigger browser page reloads. The interface queries directory listings via JSON dynamically, keeping the upload/download queue state active even when navigating through folders.
- **Sequential Queue with Cancellation:** Features a robust upload/download queue that runs transfers sequentially one after the other. Each entry in the queue displays its own individual progress bar, speed tracker, and an independent cancel button (marked as an 'X') on the right to terminate transfers on the fly.
- **Direct Game Installation (NSP/NSZ/XCI/XCZ):** When adding game files to the upload queue, you can check the "Install directly" option. The web server will stream the incoming HTTP upload socket data directly to the Switch's internal game installer (`yati`) on the fly, installing the game directly on the console without saving the intermediate file onto the SD card.
- **Dynamic Storage Target Selection:** Automatically determines the target storage (SD Card vs System Memory) for each game installation. It estimates the uncompressed size of the package (`1.6 * compressed_size` for compressed formats like NSZ/XCZ, or the file size for NSP/XCI) and checks the available NAND USER space. If installing to System Memory leaves at least 500 MB of free space, it selects System Memory; otherwise, it defaults to the microSD Card. This applies to network, USB, and local installations.
- **Touch-Interactive Stop Button:** The console's server wait dialog includes a prominent touch-enabled red **Stop** button and a sub-label helper ("Press B to Stop Server") to easily terminate the server and exit the dialog.
- **Checkbox Selection & Batch Operations:** Displays checkboxes next to files and directories in list and grid views, allowing bulk selection. The bottom toolbar provides options to delete all selected items or download them collectively.
- **Recursive Directory Deletion:** Select folders and delete them recursively directly from the browser window (performing safe recursive deletion on the console's filesystem).
- **Bulk Download as ZIP:** Select multiple files or entire folders to download them as a single packaged ZIP file. The ZIP archive is generated on-the-fly directly in the client browser's memory without compression, shifting the processing load entirely to the user's computer and keeping the console's CPU and RAM free.
- **Direct Image Viewer:** Image files (PNG, JPG, JPEG, GIF, BMP) are highlighted as `[I]` in the directory listing and open directly on the page in a seamless lightbox viewer.
- **Dedicated Screenshot Gallery:** Serves a beautiful, interactive gallery at `/album` that scans the console's `/Nintendo/Album` folder. The screenshots and videos are sorted chronologically by date (newest first). The interface automatically decodes the Nintendo Switch screenshot filename structure (`YYYYMMDDHHMMSS00-TITLEID.ext`) to show formatted human-readable dates (e.g., `YYYY-MM-DD HH:MM:SS`) and looks up the Title ID to retrieve the game's actual display name. It features built-in video playback controls for MP4 captures, an adaptive grid view (4 columns on mobile), and quick switching links between the file browser and screenshots gallery.
- **Tools Menu Integration:** Press **Plus** (START) in the console's **Tools** menu (or any other option-enabled menu) to open the Network Server or context options. Launching the Web Server starts the universal shared instance providing both file browsing and screenshot management.

## File association

Sphaira has file association support. Let's say your app supports loading .png files, then you could write an association file, then when using the file browser, clicking on a .png file will launch your app along with the .png file as argv[1]. This was primarly added for rom loading support for emulators / frontends such as RetroArch, MelonDS, mGBA etc.

```ini
path=/switch/your_app.nro
supported_extensions=jpg|png|mp4|mp3
```

The `path` field is optional. If left out, it will use the name of the ini to find the nro. For example, if the ini is called mgba.ini, it will try to find the nro in /switch/mgba.nro and /switch/folder/mgba.nro.

See `assets/romfs/assoc/` for more examples of file assoc entries.

## Installing (applications)

Sphaira can install applications (nsp, xci, nsz, xcz) from various sources (sd card, gamecard, ftp, usb).

For informantion about the install options, [see the wiki](https://github.com/ITotalJustice/sphaira/wiki/Install).

### Usb (install)

The USB protocol is the same as tinfoil, so tools such as [ns-usbloader](https://github.com/developersu/ns-usbloader) and [fluffy](https://github.com/fourminute/Fluffy) should work with sphaira. You may also use the provided python script found [here](tools/usb_install_pc.py).

### DBI Backend (install)

Sphaira supports the official **DBI Backend** (DBI0) USB protocol. This allows installing games from your PC using the official Python backend server script `dbibackend.py` (or companion executables). DBI Backend uses custom 16-byte headers and supports on-demand random-access block reading for efficient transfers. Select "DBI Install" from the network/install options to connect.

### Ftp (install)

Once you have connected your ftp client to your switch, you can upload files to install into the `install` folder.

## Fan curves & Fan sysmodule

Sphaira includes a "Fan curve" settings menu under "Kefir Settings" to dynamically configure custom fan speed tables (Handheld and Docked modes).
- **Live Apply:** If the `sphaira_fan` sysmodule (`00FF46554E43544C`, `FunControl`) is installed and enabled, curve changes apply dynamically without rebooting.
- **Save and Reboot:** If the sysmodule is disabled or not installed, curves are saved to `/atmosphere/config/system_settings.ini` for the next boot.
- **Conflict-Free Thermal Sensing:** Uses Nintendo Switch `ts` (Thermal Measurement) service (`tsOpenSession`) to read SoC temperature without IPC session collisions in `ptm`.
- **Live Sysmodule Telemetry:** The background module exports state telemetry (`/switch/sphaira/fan_status.bin`), providing real-time hardware status to Sphaira.
- **Physical Fan Motor Inertia Modeling:** UI graph markers smoothly model physical motor spin-up and spin-down acceleration/deceleration response.

## Themes & Translations

Sphaira features customizable theme options and multi-language support:
- **Theme Options:** Choose interface themes, configure background music, and set time formats under "Settings -> Appearance -> Sphaira theme options".
- **Interface Translations:** Manage and download translation files to customize your console interface language under "Kefir Settings -> Translate Interface".
- **Full Localization & Sync:** Multi-language interface translations (14 supported languages: English, Japanese, French, German, Italian, Spanish, Chinese, Korean, Dutch, Portuguese, Russian, Swedish, Vietnamese, Ukrainian) are fully synchronized and translated, providing seamless native navigation for all interface texts and settings.
- **Themezer Favorites:** Add any theme pack from Themezer to your favorites list by pressing **R3** (Right Stick click) in the Themezer download menu. Favorites are instantly shown on the main "Themes" tab alongside built-in options, marked with a star icon for easy access and offline viewing.
- **Themezer Filters:** Browse themes easily by applying filters directly in the "Themezer Options" sidebar. You can filter themes by target layout (e.g., Home Menu, Lock Screen, All Apps, Settings, Player Select, User Page, News) and input custom search tags (separated by spaces or commas). When a target filter is active, the app queries individual themes instead of packs and lists them dynamically, matching them with their parent pack previews.

## Image Viewer

Sphaira provides an integrated image viewer with dedicated legend and controls:
- **Custom Legend:** Clear bottom-bar indicators (`Prev / Next Image` for D-Pad Left/Right, `Zoom Up / Down` for ZL + Stick Up/Down, and `Full Screen` for ZR).
- **Zoom & Navigation:** Holding ZL with Analog Stick / D-Pad Up or Down zooms in or out without accidentally changing images.
- **Stick Panning:** Releasing ZL while zoomed in enables smooth pan/scroll across the zoomed image using analog sticks or D-Pad without scale changes or switching files.

## File Browser

Sphaira includes a robust file manager with standard operations (Cut, Paste, Rename, Delete, Create File/Folder, Extract/Compress zip, Install/Forwarder) and write protection handling:
- **Looping Menu Navigation:** Option sidebar lists feature looping circular navigation (pressing UP on the first item wraps to the last, and vice-versa).
- **Enhanced Selection Checkboxes:** Checkboxes shown when marking multiple files (triggered by X/Y) are enlarged to 20px, shifted left into the empty margin (-30px) to prevent overlapping filenames, and feature a larger 18px checkmark icon for improved readability.
- **User-Friendly Error Mapping:** When filesystem operations fail (e.g., target file locked due to taking a screenshot, path too long, invalid characters, write protection), the error popup displays a helpful, localized description of the problem and how to resolve it.
- **Polished Option Dialogs:** Option boxes and confirmation popups (such as the web folder sharing QR code) feature optimized text line-height spacing (`1.4f`), dynamic height auto-scaling to eliminate excess empty space, and vertical centering next to images/QR codes.
- **Write Protection Support:** If a file or folder is marked as Read-Only (and "Ignore read only" is disabled in Advanced Settings), destructive or modification actions such as **Cut**, **Rename**, **Delete**, **Paste**, **Create File**, and **Create Folder** are automatically disabled and grayed out in the options sidebars, clearly showing the reason when selected.

## Game Installer

Sphaira features a built-in game installer supporting multiple formats (NSP, NSZ, XCI, XCZ) with configurable storage destination priority:
- **Storage Destination Priority:** Choose where titles are installed in Settings -> Install (defaults to **Automatic** for new installations):
  - **microSD card only:** Always install to microSD storage.
  - **System memory only:** Always install to NAND storage.
  - **System first, then SD:** Install to NAND; if NAND does not have enough free space (taking the reserve threshold into account), automatically fall back to microSD.
  - **SD first, then system:** Install to microSD; if microSD space is below the reserve threshold, fall back to NAND.
  - **Automatic:** Install to whichever storage has the most free space (NAND or microSD) after verifying that both satisfy the reserve threshold.
- **Background MTP Installation:** When MTP is enabled, you can copy game files directly to the virtual `install` folder from your PC via USB MTP at any time. Sphaira will automatically detect the file transfer, launch the installer in the background, and display a progress overlay with real-time transfer speed and estimated remaining time. If another installation or storage operation is already in progress, it will safely reject the MTP file transfer and notify you.
- **Customizable Reserve Threshold:** Set the free space reserve threshold in Megabytes (MB) via Settings -> Install -> "Reserve free space" (opens an on-screen numpad). If a target storage doesn't meet the reserve limit during installation, the installer falls back to the secondary storage or warns the user.

## Theme Creator

Sphaira includes a built-in theme creator that allows you to easily convert any image into a custom Nintendo Switch theme (`.nxtheme` format) directly from the console:
- **Interactive Cropping:** Open any image in the File Browser, press the options button, and select "Create Switch Theme". You can zoom using ZL/ZR and pan using the Left Stick or D-Pad to select the perfect crop window (constrained to the native 16:9 aspect ratio).
- **Target Selection:** Configure theme properties including target system menu (Home Menu, Lock Screen, All Apps, Settings, User Page, News, or Player Select), theme name, and author name.
- **Auto Installation:** After generation, Sphaira switches to a confirmation screen where you can hold **A** (3s) to install the theme or hold **Y** (3s) to install and reboot. It triggers `NXThemesInstaller` in the background with appropriate arguments (`--auto-install` and optionally `--reboot`).

## Display Layouts

Sphaira supports multiple display layouts for homebrew and games, customizable to suit your preference:
- **Storage Status Bar:** The status bar displays the current network IP address (or a localized "No Internet" status), dual NAND and SD storage capacity bars (color-coded green, yellow, and red based on usage), and clock and battery details (with a static green lightning bolt icon during charging).
- **NACP v2 Support:** Added compatibility for parsing the new compressed NACP metadata format introduced in Nintendo Switch firmware 20.0+, ensuring titles and authors display correctly.
- **Grid & Icon Views:** Grid and Icon views now support seamless row-to-row navigation. Pressing **Right** on the last item of a row moves the cursor directly to the next row, and **Left** on the first item of a row moves it back to the previous row.
- **HB Menu Layout:** Replicates the classic Nintendo Switch Homebrew Menu style. It displays a large icon of the selected app on the left along with detailed metadata (Name, Author, Version) on the right, and lists all available applications in a horizontal row at the bottom. The horizontal row uses custom dual-banner cards (showing the clean filename in a white banner on top, and the full-sized icon below).
- **Animated Waves:** An animated wave background (reproducing the classic hbmenu background) runs along the bottom of the screen. This can be enabled or disabled via "Settings -> Appearance -> Animated waves". Its colors are fully customizable in `/config/sphaira/config.ini` by specifying `wave_color_dark` (for dark themes) and `wave_color_light` (for light themes) as hex values (e.g. `0x00FFC8`). If left blank, it automatically resolves to the active theme's highlight colors.
- **Pulsing Charging Indicator:** When charging, the battery percentage numbers smoothly animate with a pulsing yellow-to-orange gradient transition, and a custom lightning bolt icon is rendered directly on the right using NanoVG vector paths to ensure compatibility across all system font configurations.

## Building from source

You will first need to install [devkitPro](https://devkitpro.org/wiki/Getting_Started).

Next you will need to install the dependencies:
```sh
sudo pacman -S switch-dev deko3d switch-cmake switch-curl switch-glm switch-zlib switch-mbedtls
```

Also you need to have on your environment the packages `git`, `make`, `zip` and `cmake`

Once devkitPro and all dependencies are installed, you can now build sphaira.

```sh
git clone https://github.com/ITotalJustice/sphaira.git
cd sphaira
cmake --preset MinSizeRel
cmake --build --preset MinSizeRel
```

The output will be found in `build/MinSizeRel/sphaira.nro`

## Credits

- [borealis](https://github.com/natinusala/borealis)
- [stb](https://github.com/nothings/stb)
- [yyjson](https://github.com/ibireme/yyjson)
- [nx-hbmenu](https://github.com/switchbrew/nx-hbmenu)
- [nx-hbloader](https://github.com/switchbrew/nx-hbloader)
- [deko3d-nanovg](https://github.com/Adubbz/nanovg-deko3d)
- [libpulsar](https://github.com/p-sam/switch-libpulsar)
- [minIni](https://github.com/compuphase/minIni)
- [GBATemp](https://gbatemp.net/threads/sphaira-hbmenu-replacement.664523/)
- [hb-appstore](https://github.com/fortheusers/hb-appstore)
- [haze](https://github.com/Atmosphere-NX/Atmosphere/tree/master/troposphere/haze)
- [nxdumptool](https://github.com/DarkMatterCore/nxdumptool) (for gamecard bin dumping and rsa verify code)
- [Liam0](https://github.com/ThatNerdyPikachu/switch-010editor-templates) (for ticket / cert structs)
- [libusbhsfs](https://github.com/DarkMatterCore/libusbhsfs)
- [libnxtc](https://github.com/DarkMatterCore/libnxtc)
- [oss-nvjpg](https://github.com/averne/oss-nvjpg)
- [nsz](https://github.com/nicoboss/nsz)
- [themezer](https://themezer.net/)
- Everyone who has contributed to this project!
