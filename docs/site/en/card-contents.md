# What is on the microSD card

Kefir puts these folders and files on the memory card. This page tells what each one is for, so you know what is
safe to touch. Files that the console or an app creates later are marked *created on the console*.

!!! tip
    Do not delete a folder because you do not know it. Look it up here first. When in doubt, leave it.

## Card root

| Path | What it is |
|---|---|
| `payload.bin` | hekate, the boot menu. This is the payload you send with a modchip, TegraRcmGUI or a reboot from Kefir Hub. |
| `boot.dat`, `boot.ini` | For SX-type modchip loaders: `boot.ini` tells them to start `payload.bin`. Other chips and RCM do not use them. |
| `hbmenu.nro` | Homebrew Menu, started from the Album. Kefir Hub takes this place when it is installed as the menu. |
| `exosphere.ini` | Low-level Atmosphère settings: debug mode and hiding the console serial (blank PRODINFO) in emuMMC. |
| `install.bat` | Windows script of the Kefir package: copies the files to the card and removes leftovers of old versions. Not used on the console. |
| `atmosphere/` | Atmosphère: the custom firmware. See [atmosphere/](#atmosphere). |
| `bootloader/` | hekate: the boot menu and its settings. See [bootloader/](#bootloader). |
| `config/` | Settings of Kefir, Kefir Hub and system modules. See [config/](#config). |
| `switch/` | Homebrew apps and overlays. See [switch/](#switch). |
| `themes/`, `games/`, `warmboot_mariko/` | See [other folders](#other-folders). |
| `emuMMC/` | *Created on the console.* The emuMMC image (a copy of the system) and `emummc.ini`. Never delete it: it is your emuMMC. |
| `Nintendo/` | *Created on the console.* Games and saves that sysMMC keeps on the card. emuMMC keeps its own copy inside `emuMMC/`. |
| `backup/<serial>/` | *Created on the console.* NAND and key backups made by hekate. |
| `dumps/` | *Created on the console.* Game and save dumps made by Kefir Hub. |
| `startup.te` | *Created on the console.* A one-shot TegraExplorer script written by Kefir Hub (profile transfer). TegraExplorer runs and deletes it. |

## atmosphere/ {#atmosphere}

| Path | What it is |
|---|---|
| `package3` | Atmosphère itself: kernel patches and all system modules in one file. hekate starts it. |
| `stratosphere.romfs` | Part of Atmosphère, shipped together with `package3`. |
| `reboot_payload.bin` | The payload that "Reboot to payload" starts. A copy of the boot payload. |
| `hbl.nsp` | Homebrew Loader: runs `.nro` apps from the Album or in place of a game. |
| `hbl_html/` | Files the Homebrew Loader uses when it is started from the Album. |
| `splash.png` | The picture shown while Atmosphère starts. |
| `config/override_config.ini` | Which button and which title start the Homebrew Loader. |
| `config/system_settings.ini` | System switches that Atmosphère applies at boot. [[Kefir Settings]] writes [[40MB Memory]] and [[USB 3.0]] here. |
| `config/stratosphere.ini` | Options of Atmosphère's own modules. |
| `config/system_settings_stock.ini` | A second copy of `system_settings.ini` kept by Kefir. |
| `config_templates/` | Atmosphère reference copies with every key explained. Not read at boot. |
| `contents/<id>/` | One folder per system module or game. A game id holds mods (`exefs/`, `romfs/`) and cheats. A module id holds the module (`exefs.nsp`), `flags/boot2.flag` (start at boot) and `toolbox.json` (its name in the Sysmodules overlay). |
| `exefs_patches/` | Patches Atmosphère applies to system modules in memory. `disable_ca_verification` lets the console talk to servers with their own certificates; `bluetooth_patches` and `btm_patches` are needed by MissionControl; `fatal_force_extra_info` shows full data on a fatal error. |
| `nro_patches/` | The same, for `.nro` homebrew. |
| `hosts/default.txt` | Host rules Atmosphère applies to the console's network (block or redirect a domain). Empty in Kefir. |
| `crash_reports/`, `fatal_reports/`, `erpt_reports/` | *Created on the console.* Reports written after a crash. Kefir Hub reads `fatal_reports/` for [Troubleshooting](troubleshooting.md). Safe to delete. |
| `automatic_backups/` | *Created on the console.* Backups of PRODINFO and BIS keys that Atmosphère makes on its own. Keep them. |

System modules shipped by Kefir, by folder name under `contents/`:

| Folder | Module | What it does |
|---|---|---|
| `00FF46554E43544C` | FunControl | Fan curve. |
| `010000000000bd00` | MissionControl | Third-party Bluetooth controllers. |
| `420000000000000B` | sys-patch | Signature and other patches applied at boot. |
| `420000000007E51A` | nx-ovlloader | Loads Tesla and Ultrahand overlays. |
| `690000000000000D` | sys-con | USB controllers. |

## bootloader/ {#bootloader}

| Path | What it is |
|---|---|
| `hekate_ipl.ini` | Main hekate settings: boot entries (Atmosphere, Full Stock), autoboot, boot wait, backlight. `hekate_ipl_.ini` is a spare copy. |
| `ini/` | More boot entries: `atmostock.ini` (Semi-stock, legacy), `kefir_updater.ini` (Update Kefir, starts TegraExplorer), `!kefir_updater.ini` (the entry the auto-update uses). |
| `nyx.ini` | Settings of Nyx, hekate's touch interface. `nyx.ini_` is a spare copy. |
| `payloads/` | Payloads listed in hekate → Payloads: `fusee.bin` (Atmosphère), `TegraExplorer.bin` (Kefir update and NAND tools), `Lockpick_RCM.bin` (console keys). |
| `sys/` | Parts of hekate: `nyx.bin` (interface), `res.pak` (its images), `emummc.kipm`, `libsys_lp0.bso` (sleep), `libsys_minerva.bso` (RAM training), `thk.bin`, `l4t/` (Linux and Android). |
| `res/` | Icons of the boot entries. |
| `update.bin` | hekate itself. hekate starts this file when it is newer than the payload that was sent. |
| `bootlogo_kefir.bmp`, `updating.bmp` | Pictures shown at boot and during a Kefir update. |

## config/ {#config}

| Path | What it is |
|---|---|
| `kefir/` | Kefir Hub: settings, network locations, logs, profile transfer data. See [Where settings are stored](settings.md#where-settings-are-stored). |
| `sphaira/` | Old name of the Kefir Hub folder. Kefir Hub moves it into `kefir/` on first start. |
| `kefir-updater/` | Kefir updater: `custom_packs.json`, `hide_tabs.json`, `kefir_updater.ini`. |
| `.skip` | Folders the Kefir updater skips when it scans the card (`roms`, `retroarch`, `tico`). |
| `oc/` | Overclock files: sys-clk, `kefir.kip`, the overlay. [[Overclock status]] copies them into place; `oc_bkp/` holds the backup while it is off. |
| `8gb/` | TegraExplorer scripts and files for [[8GB DRAM status]]. Only for consoles with 8 GB RAM soldered on. |
| `semistock/` | TegraExplorer script that undoes Semi-stock (restores `emummc.ini` and `exosphere.ini`). |
| `MissionControl/` | Settings of MissionControl (Bluetooth controllers). |
| `sys-con/` | Settings of sys-con (USB controllers), one file per controller type. |
| `ultrahand/` | Settings, themes and downloads of the Ultrahand overlay menu. |
| `sys-clk/` | *Created on the console.* sys-clk profiles, when overclock is on. |
| `redirect.bin` | *Created on the console* by [[Redirect Emunand saves to SD]]. |

## switch/ {#switch}

| Path | What it is |
|---|---|
| `.overlays/` | Overlays (open with **L + D-pad down + R-stick**): `ovlmenu.ovl` (Ultrahand menu), `ovlSysmodules.ovl`, `ovlEdiZon.ovl` (cheats), `NX-FanControl.ovl`, `sys-patch-overlay.ovl`. |
| `.packages/` | Ultrahand packages: the Kefir Menu (Settings, Software, Theme, Translate Interface). |
| `kefir-hub.nro` | Kefir Hub. |
| `kefir-updater/` | Kefir updater: `kefir-updater.nro`, `update.te` (the script TegraExplorer runs), `version` (installed Kefir version). |
| `DBI/` | DBI, the installer app. |
| `daybreak/` | Daybreak, the firmware updater from the Atmosphère team. |
| `linkalho/` | Linkalho, links a user to an account without a network. |
| `NX-Activity-Log/` | NX-Activity-Log, play time statistics. |
| `NxThemesInstaller/` | NXThemesInstaller, installs HOME menu themes. |
| `TorrentShopNX/` | TorrentShopNX. |
| `appstore/.get/` | *Created on the console.* What the [[Homebrew App Store]] installed: one folder per app with its file list. |
| `sphaira/cache/` | *Created on the console.* Kefir Hub cache: store lists, icons. Safe to delete. |

## Other folders {#other-folders}

| Path | What it is |
|---|---|
| `themes/systemPatches/` | Patches NXThemesInstaller needs for HOME menu themes, one file per firmware. |
| `games/Homebrew menu […].nsp` | A forwarder that puts Homebrew Menu on the HOME screen. Install it with Kefir Hub or DBI. |
| `warmboot_mariko/wb_XX.bin` | Sleep-mode firmware for Mariko consoles (V2, Lite, OLED), one file per firmware range. hekate needs them. |
