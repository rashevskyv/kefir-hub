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

## Web File Manager

Sphaira includes an HTTP-based Web File Manager (accessed via port 8080 when enabled in the Network menu) to browse, download, upload, and view files on the console directly from a web browser:
- **Direct Image Viewer:** Image files (PNG, JPG, JPEG, GIF, BMP) are highlighted as `[I]` in the directory listing and open directly in a new browser tab for inline viewing instead of forcing a download.
- **Integrated Gallery View:** Any folder containing images features a **Gallery** button in the web interface. Clicking it opens a beautiful, responsive grid gallery displaying thumbnails of all image files in that folder, letting you easily scroll and view screenshots and albums.

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
- **Themezer Favorites:** Add any theme pack from Themezer to your favorites list by pressing **R3** (Right Stick click) in the Themezer download menu. Favorites are instantly shown on the main "Themes" tab alongside built-in options, marked with a star icon for easy access and offline viewing.

## Image Viewer

Sphaira provides an integrated image viewer with dedicated legend and controls:
- **Custom Legend:** Clear bottom-bar indicators (`Prev / Next Image` for D-Pad Left/Right, `Zoom Up / Down` for ZL + Stick Up/Down, and `Full Screen` for ZR).
- **Zoom & Navigation:** Holding ZL with Analog Stick / D-Pad Up or Down zooms in or out without accidentally changing images.
- **Stick Panning:** Releasing ZL while zoomed in enables smooth pan/scroll across the zoomed image using analog sticks or D-Pad without scale changes or switching files.

## File Browser

Sphaira includes a robust file manager with standard operations (Cut, Paste, Rename, Delete, Create File/Folder, Extract/Compress zip, Install/Forwarder) and write protection handling:
- **Write Protection Support:** If a file or folder is marked as Read-Only (and "Ignore read only" is disabled in Advanced Settings), destructive or modification actions such as **Cut**, **Rename**, **Delete**, **Paste**, **Create File**, and **Create Folder** are automatically disabled and grayed out in the options sidebars, clearly showing the reason when selected.

## Theme Creator

Sphaira includes a built-in theme creator that allows you to easily convert any image into a custom Nintendo Switch theme (`.nxtheme` format) directly from the console:
- **Interactive Cropping:** Open any image in the File Browser, press the options button, and select "Create Switch Theme". You can zoom using ZL/ZR and pan using the Left Stick or D-Pad to select the perfect crop window (constrained to the native 16:9 aspect ratio).
- **Target Selection:** Configure theme properties including target system menu (Home Menu, Lock Screen, All Apps, Settings, User Page, News, or Player Select), theme name, and author name.
- **Auto Installation:** After generation, Sphaira switches to a confirmation screen where you can hold **A** (3s) to install the theme or hold **Y** (3s) to install and reboot. It triggers `NXThemesInstaller` in the background with appropriate arguments (`--auto-install` and optionally `--reboot`).

## Display Layouts

Sphaira supports multiple display layouts for homebrew and games, customizable to suit your preference:
- **Grid & Icon Views:** Grid and Icon views now support seamless row-to-row navigation. Pressing **Right** on the last item of a row moves the cursor directly to the next row, and **Left** on the first item of a row moves it back to the previous row.
- **HB Menu Layout:** Replicates the classic Nintendo Switch Homebrew Menu style. It displays a large icon of the selected app on the left along with detailed metadata (Name, Author, Version) on the right, and lists all available applications in a horizontal row at the bottom. The horizontal row uses custom dual-banner cards (showing the clean filename in a white banner on top, and the full-sized icon below).
- **Animated Waves:** An animated wave background (reproducing the classic hbmenu background) runs along the bottom of the screen. This can be enabled or disabled via "Settings -> Appearance -> Animated waves". Its colors are fully customizable in `/config/sphaira/config.ini` by specifying `wave_color_dark` (for dark themes) and `wave_color_light` (for light themes) as hex values (e.g. `0x00FFC8`). If left blank, it automatically resolves to the active theme's highlight colors.

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
