# Roadmap

What Kefir Hub has gained recently and what is planned next. The list can change: items move, shrink or drop out as they are tested on real consoles.

!!! note
    A listed item is a plan, not a promise. Items that touch system data are released only after a test on hardware.

## Recently added {#shipped}

| Feature | What it does |
|---|---|
| System cleanup | One list of checkboxes removes old updates, orphaned content, placeholders, unused tickets and similar leftovers, and shows what each step freed. |
| System information | All console, Atmosphère, SD card, battery and hardware data in grouped tables. See [System tools](system-tools.md#system-information). |
| Game patches | Remove the linked-account requirement, allow screenshots and video capture, at install time or later on an installed game. |
| Language bound to a translation pack | A translation pack can force the game language. You can also force the language per installed game. |
| Game transfer between two consoles | Send games, updates and DLC from one console to another over Wi-Fi or a cable. See [Console transfer](console-transfer.md). |
| Mods | Mod size in the game card, a delete option for mods, and a mods folder per game over MTP. |
| Saves | Check backups, browse a backup read-only, automatic backup before a save is deleted, restore of folders made by JKSV or Checkpoint. |
| File browser | Hex view and extraction of RAR, 7z, xz, tar and gz archives. |
| Delete homebrew | Remove an app together with its own folder and its forwarders. |
| PC install on connect | When you plug in the cable, the Hub looks for an install app on the PC and opens the install list. |
| First-start page | A full page in the console language with the first steps. |

## Planned and in progress {#planned}

### Online play {#online}

| Feature | Status | What it does |
|---|---|---|
| Nextendo built in | Planned | Nextendo online play on emuMMC with no button press. A Nextendo section in the Kefir settings shows the mode, the supported firmware and the supported games. An option for sysNAND is planned for Semi-stock only. |

### Games and installs {#games}

| Feature | Status | What it does |
|---|---|---|
| Game updates | Researching | Find installed games that have a newer update or DLC and fetch them through a download app. |
| Newer firmware required | Deferred | Warn when a game needs a newer SDK than the firmware, and reset the required version. |
| Failed installs | Planned | After an install, the PC app lists the files that failed. The Hub offers to restart in Title Mode and continue with exactly those files. |
| Many small files over MTP | Planned | A hint to pack the folder, a whole-transfer progress line, and extraction of archives dropped over MTP. |
| Mods | In progress | Install mods automatically when a game folder with mods is copied over MTP. Pack mods into `romfs.bin`. |
| Repack with mods | In progress | In the games list, rebuild a game as an NSP with the mod, the update and the DLC included. |
| Game details | Planned | A read-only page with the NACP data of a game (rating ages, screenshot and video capture, save sizes), and a check of a forwarder before it is installed. |

### Console care {#console}

| Feature | Status | What it does |
|---|---|---|
| Battery gauge and calibration wizard | In progress | Read the battery chip, back it up, restore it, and calibrate the gauge step by step. Includes support for enlarged batteries. See [Battery calibration](battery-calibration.md). |
| Storage manager | In progress | A ring chart of what fills the microSD card and the internal memory, with a card for each part. |
| Repair SD card | Researching | Fix a card whose games stopped starting after the files were copied to a bigger card. |
| Joy-Con and Wi-Fi sync | Planned | Copy Bluetooth pairings and Wi-Fi networks between sysMMC and emuMMC. |
| Console colours | Planned | Change the colours the system shows for the Switch Lite body and the Joy-Con. |
| Region change | Researching | Change a Chinese console to a global region, with a backup first. Needs a test on a real unit. |

### Hub {#hub}

| Feature | Status | What it does |
|---|---|---|
| Activity log | Planned | Play time per game, with totals, launches, first and last played, and a timeline. |
| Guided tour for Kefir Updater | Planned | When you open Kefir Updater, the Hub explains that it now updates Kefir and walks you to the update page. |
