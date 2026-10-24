# System tools

Manage sysmodules, set the fan curve, manage saved Wi-Fi networks and user profiles.

**Where:** [[Tools]] → [[Tools]]

<!-- shot: system-tools-list | Tools submenu: one list under three captions — Diagnostics, Settings, Maintenance -->

| Group | Item | What it is |
|---|---|---|
| [[Diagnostics]] | [[System information]] | Console, Atmosphère, storage, power, battery, hardware and play activity. See [System information](#system-information). |
| [[Settings]] | [[Module Manager]] | Start, stop and set autostart for installed sysmodules. |
| [[Settings]] | [[Fan curve]] | Fan speed curves for handheld and docked mode. |
| [[Settings]] | [[Wi-Fi]] | Saved Wi-Fi networks. |
| [[Settings]] | [[Users]] | Console user profiles. See [Users](users.md). |
| [[Maintenance]] | [[Battery]] | Fuel gauge state against the saved copy, verdict, backup. See [Battery](#battery). |
| [[Maintenance]] | [[Clean system junk]] | Deletes leftovers. See [Clean system junk](#clean-system-junk). |
| [[Maintenance]] | [[Fill free SD space with zeros]] | Overwrites the unused space of the memory card. See [Fill free SD space with zeros](#fill-free-sd-space-with-zeros). |
| [[Maintenance]] | [[Remove parental controls]] | Planned; opens a "Coming soon" message. |

<!-- draft
- new row under [[Diagnostics]]: [[Check game updates]] ("[[Find installed games that have a newer update.]]"); add it to the table above and give it a short section
- it downloads the list of the latest game updates that Kefir Updater used (switch-cheats-db, about 21 000 games, 3.6 MB) and compares it with the installed version of every game
- the page [[Game updates]] lists only the games with a newer update: name on the left, installed update and newest update on the right, for example v1 → v6 (the update number, as elsewhere in Kefir Hub); the caption on top says how many ([[Updates available: ]])
- games the list does not know (homebrew forwarders, very new or rare games) are not checked; the caption at the bottom counts them ([[Not in the list, not checked: ]])
- with nothing to update: the notice "[[All checked games are up to date]]"
- **B** stops the download; it needs the internet
-->

## [[System information]]

**Where:** [[Tools]] → [[Tools]] → [[System information]]

<!-- shot: system-tools-system-info | System information: Console group open, parameter → value rows, other groups closed -->

Each group shows rows of parameters and values. The list includes [[Console]], [[Atmosphere]], [[Storage]], [[Power]], [[Battery]], [[Hardware]], and [[Play activity]]. The first group opens automatically at start, and a group whose service cannot be read is hidden. Press **A** on a group or tap it to open or close it, or press **A** on any row to open or close its parent group. Press **Y** to open or close all groups at once. Press **left** or **right** to page through the rows, or press **L** ([[Previous group]]) and **R** ([[Next group]]) to jump directly to the previous or next group and open it. The number on the right of a closed group indicates its row count.

- [[Console]]: firmware version, name and hash; model; hardware type and SoC; retail or development unit; burnt fuses; DRAM id; device id; kiosk; charger HiZ; serial number; console nickname; language; region; parental controls. The [[Serial number]] is the real one, while [[Serial number source]] indicates where it was read from, such as [[System settings]], [[PRODINFO partition]], or a [[Backup file]] with its path (Atmosphère `/atmosphere/automatic_backups`, hekate `/backup`). If the system returns a blank serial due to features like Atmosphère blank_prodinfo or Incognito, [[Serial number (system)]] shows that blank value. If no source contains it, the value is [[Not available]].
- [[Atmosphere]]: version, key generation, target firmware, supported firmware, git commit, RCM bug patched, emuMMC type (partition or file), blank PRODINFO, PRODINFO writes allowed, and USB 3.0 forced.
- [[Storage]]: system memory used and free; microSD used and free, speed mode, user and protected area; and microSD CID details including maker, OEM id, product, revision, serial, and month made.
- [[Power]]: charger status, charging allowed, power sufficiency, current charging state, fast charging, USB charger type, USB power role, current and voltage limits, HiZ state, controller power supply, OTG, and power delivery state on firmware 17.0.0 or newer.
- [[Battery]]: charge level, raw charge, health, temperature, voltage, design capacity, full capacity now, remaining capacity, charge cycles, age, current, average current, cell voltage, cell temperature, and time to empty.
- [[Hardware]]: Bluetooth and Wi-Fi MAC addresses, configuration ID, battery lot, and the serial number from the calibration data.
- [[Play activity]]: installed games, total play time, total launches, and most played game.

## [[Module Manager]]

Shows every sysmodule installed in `/atmosphere/contents` on the memory card, whether it runs now, and whether it starts at boot.

**Where:** [[Tools]] → [[Tools]] → [[Module Manager]]

<!-- shot: system-tools-module-manager | Module Manager list: green/grey dots, RAM per running module, "After reboot: Enabled/Disabled", Sysmodule RAM bar at the top -->

Each row shows:

- a green dot when the module runs now, grey when it does not;
- its name and program ID; "[[Applies after reboot]]" when the module can only start at boot;
- the RAM it uses, when it runs;
- "[[After reboot: Enabled]]" or "[[After reboot: Disabled]]" — whether it starts when the console boots.

The bar at the top ([[Sysmodule RAM]]) shows how much of the system memory pool is used and how much is free.

### Start or stop a module

1. Select the module.
2. Press **A** ([[Toggle]]). A running module stops; a stopped module starts.

Some modules cannot be toggled here:

- A module marked [[Applies after reboot]] cannot start while the console runs. Use autostart and reboot.
- sys-patch starts at boot and does not need to be started again.

### Start a module at boot

1. Select the module.
2. Press **Y** ([[Autostart]]). The right column changes to "[[After reboot: Enabled]]" or "[[After reboot: Disabled]]".
3. The change takes effect on the next boot.

### Module details

Press **−** ([[Info]]) to see the name, program ID, current state, RAM use, autostart state and a description (taken from the module's GitHub page when available).

### Options

Press **+** ([[Options]]).

| Option | What it does |
|---|---|
| [[Start]] / [[Stop]] | Same as **A** for the selected module. |
| [[Autostart]] | Same as **Y**. |
| [[Info]] | Same as **−**. |
| [[Filter]] | Show [[All]], [[Running]], [[Stopped]], [[Autostart]] or [[Applies after reboot]] modules. |
| [[Sort]] | Order by [[Name]], [[Running]] or [[Autostart]]. |

Press **X** ([[Refresh]]) to read the states again.

!!! warning
    Stopping a module the system or a running game depends on can make it crash. If something stops working, reboot.

## [[Fan curve]]

Sets how fast the fan spins at each temperature, separately for handheld and docked mode.
The curve is written to Atmosphère's `system_settings.ini`.

**Where:** [[Tools]] → [[Tools]] → [[Fan curve]]

<!-- shot: system-tools-fan-curve | Fan curve screen: graph with points, live temperature, points list, Handheld curve label -->

While this screen is open, Kefir Hub controls the fan itself. The fan follows the curve on the screen,
so you hear each change at once. When you leave the screen, the console controls the fan again.

[[Apply]] saves both curves. When you leave the screen, Kefir Hub starts its fan module with the new curve.
The module runs until the next reboot. After a reboot the console applies the saved curve itself, and the module does not run.
The module leaves no files on the microSD card.

### Edit the curve

1. Press **X** ([[Mode]]) to switch between the handheld and the docked curve.
2. Select a point with **D-pad** up/down, or tap it on the graph.
3. Press **A** ([[Edit]]). Use **D-pad** left/right to change the temperature and up/down to change the fan speed. Press **A** or **B** ([[Done]]) to stop editing.
4. **L** ([[Add Point]]) adds a point; **R** ([[Remove Point]]) removes the selected point. A curve needs at least two points.
5. Press **+** ([[Apply]]) to save both curves. They stay active when you leave the screen.

Points stay in order: a point cannot move past its neighbours.

**Y** switches to [[Bezier]] mode: you move three control points (Min, Mid, Max) and the curve follows them smoothly.
Press **Y** ([[Manual Mode]]) to return to editing single points.

If you press **B** with changes not applied, Kefir Hub asks to [[Discard]] them.

### Presets

- **ZL** ([[Load Preset]]) loads a preset into the current mode: [[Cold console]], [[Quiet]], [[Balanced]], [[Fan off]], [[Fan 100%]], or one of three custom presets.
- **ZR** ([[Save Preset]]) saves the current curve to one of three custom slots. Enter a name.

Custom presets are stored separately for handheld and docked mode.

!!! warning
    [[Fan off]] keeps the fan at 0% at every temperature on the curve (10–90 °C), and [[Fan 100%]] keeps it at full speed.
    Watch the temperature if you use [[Fan off]] while playing.

## [[Wi-Fi]]

Lists the Wi-Fi networks saved on the console. Connect to one, edit or delete it. New networks are added in the console's System Settings.

**Where:** [[Tools]] → [[Tools]] → [[Wi-Fi]]

<!-- shot: system-tools-wifi | Wi-Fi list with two saved networks, one tagged Connected -->

### Connect to a saved network

1. Select the network.
2. Press **A** ([[Connect]]) and confirm.
3. The title shows the result, or "Connection timed out".

### Edit or delete a network

Press **+** ([[Options]]) on a network:

| Option | What it does |
|---|---|
| [[Connect]] | Connect to this network. |
| [[Rename]] | Change the name shown for this network. |
| [[Change password]] | Set a new Wi-Fi password without opening System Settings. |
| [[Edit SSID]] | Change the network name (SSID) the console looks for. |
| [[View password & details]] | Show the SSID, security type and saved password. |
| [[Delete network]] | Remove the saved network. |
| [[Turn Wi-Fi Off]] / [[Turn Wi-Fi On]] | Switch wireless communication off or on. |
| [[Refresh]] | Reload the saved networks. |

!!! warning
    [[View password & details]] shows the password in plain text on screen.

### Delete several networks

1. Press **X** ([[Select]]) on each network, or **Y** ([[Invert]]) to invert the selection.
2. Press **+** → [[Delete selected]] and confirm.

Press **B** to clear the selection.

## [[Users]]

Create, rename, back up and link console user profiles. See [Users](users.md).

## Battery {#battery}

**Where:** [[Tools]] → [[Tools]] → [[Battery]]

<!-- draft
- read only: nothing is written to the fuel gauge chip or to the system settings in this version
- [[Verdict]]: [[No sign of desync on this MMC]], or the signs found: [[Restore flag (MiscCFG bit 15)]] (the chip lost power; HOS writes this MMC's saved copy at the next start), [[Restart flag (Status POR)]], [[Chip vs this MMC's copy]] with the parameters that differ; also [[Battery version]] (CAL0 and set:cal disagree), [[Cycle count]] (Lite: not trusted, charge limit 4.192 V), [[Learned capacity]] below 70 % of design
- "Model: chip / sysMMC|emuMMC": the nine learned parameters (RCOMP0, TempCo, FullCap, FullCapNom, IAvgEmpty, QRTable00..30) as chip / saved copy, hex, capacities also in mAh; a row that differs is marked ≠
- [[Fuel gauge (chip)]]: charge, remaining, full and design capacity, cell and open-circuit voltage, cycles register, MiscCFG / Status
- [[Saved by the system]]: charge cycles and cycle count reliability of the MMC you are in; sysMMC and emuMMC each keep their own copy, so open the page in both
- **X** ([[Back up]]): `/config/kefir/battery/<date>-<sysmmc|emummc>.cal` (24 bytes, the `batt.cal` format of Battery Desync Fix; copy it to `/switch/batt.cal` to use it there) and a `.txt` beside it with all 256 chip registers, the saved copy, battery versions and lot
- every open saves this MMC's copy (`/config/kefir/battery/<mmc>-copy.cal`); opened once in the other MMC, the page also shows [[Chip vs other MMC's copy]]; until then [[Other MMC]]: [[Not checked: boot it and open this page once]]
- [[Charge: shown / by voltage]]: the % the console shows and the % from the cell voltage; 10+ points apart on battery (the console in normal use is fine) = the % is wrong; while charging only a hint; [[Charge on the console screen]] is the % HOS shows
- [[No sign of desync found]] only when the other MMC was checked and the % was read on battery
- first row of [[Verdict]]: [[Full capacity / design]] in mAh and %; more than 10 % under design is drawn orange and counts as desync (" — desync, the saved copy too")
- [[Charge cycles: this / other MMC]] in orange when the two MMCs count a cycle or more apart (Lite: the charge voltage limit follows the count)
- A on a row shows what it means; A on a section caption opens or closes it
- **+** ([[Calibrate]]) opens the same wizard as [[Tools]] → [[Tools]] → [[Battery calibration]] (see that section): the reset to the factory model is its first step; [[Actions]] on its steps page opens [[Back up the chip's values]], [[Align the charge to the voltage]], [[Restore from the last backup]], [[Match the other MMC]] (no chip write: this MMC's copy takes the chip's model and the larger cycle count of the two MMCs) and [[Battery type (PRODINFO)]]
- after a reset: do not let the battery run flat in the other MMC before the two cycles are done; open the page there once and choose [[Match the other MMC]]
- why: HOS writes its saved copy into the chip only after the chip lost power, and each MMC has its own copy, so switching MMCs can leave the chip and the copy apart (wrong %, sudden shutdowns); the calibration wizard and the reset come in later versions
-->
<!-- draft
- the drain page starts the screensaver after the idle time set for the install screen (two minutes when that is off), to keep the panel from burning in: a black page with a drifting readout: the cycle, the battery bar and percent, power (W, mA) and temperature, the energy (Wh), the time on battery and the time to switch-off (on the charger: the charge %)
- any button or a touch wakes it (that press does nothing else); the load and the measuring go on behind it; the stick moves the readout and sets its brightness and drift speed as on the install screen
-->

<!-- draft
- the hekate hop to the other MMC (0.14.163) is no longer a step of the wizard: [[5. Transfer to the other MMC]] writes the calibration there by itself at the next start of the Hub in that system (see [[Battery calibration]])
- [[Battery size]]: when the learned capacity is more than 130 % of the design capacity the Verdict says a larger battery is built in ([[of the design capacity: a larger battery is built in]]); with the battery type LiPo it adds [[The battery type is LiPo (4.2 V): set it to LiHV (4.32 V) to use the whole battery]]; this is not a desync sign
- on the drain page, after the battery ran down: [[Keep the screen on while it charges. After a red battery, leave it on the charger for about 3 minutes before you switch it on.]]
- advice from the community guide: a full cycle once a month keeps the capacity; a drop of about 300 mAh in use comes back with a normal discharge-charge cycle
- **X** ([[Actions]]) → [[Battery type (PRODINFO)]]: sets the battery version byte of PRODINFO: [[LiHV (4.32 V): for a larger battery]] (version 2) or [[Back to the original]]; a backup of PRODINFO (`/config/kefir/battery/prodinfo-<time>.bin`) is read back and compared before anything is written; only the battery block and the body hash change; the result is checked and the old values are put back when the check fails; PRODINFO that fails its own hashes or CRC is never touched
- Atmosphère blocks writing PRODINFO: when it is off, Kefir Hub turns on `allow_writing_to_cal_sysmmc=1` (exosphere.ini) and `enable_hbl_bis_write` (system_settings.ini), restarts the console, finishes the change by itself at the next start and turns both off again; in emuMMC the write may be refused (then restart into sysMMC); the change applies to the PRODINFO of the system it ran in
- after the change: run the calibration wizard (reset, two full cycles); the Battery page then shows the type [[Type]] LiHV (4.32 V) and the full capacity of a larger battery
-->

<!-- shot: system-tools-battery | Battery page with the verdict and the model rows -->

<!-- draft
- [[Storage manager]] (Tools): a ring of what fills the microSD card: [[Saves and album]], [[Atmosphère and Kefir]], [[Homebrew]], [[Dumps and backups]], [[Game files (NSP, XCI)]], [[Emulators and their games]], [[Other files]] and [[Free space]], with the size and the percent of each; the centre shows the used and the total space; the system memory (used / total) is written under the list
- **Up** / **Down** pick a slice; **A** opens its card: what it is, whether it can be deleted, where it is, why it is there
- the folders are counted when the page opens (a progress bar shows the step); a partition emuMMC is outside the card's file system and is not in the ring
- the card is shown whole: the line under the list reads "microSD: total = N in the file system + M outside it"; a partition emuMMC is the part outside the file system, so no folder shows it
- the emuMMC is one container: the list has a line [[emuMMC]] with the sum, and its parts indented under it: [[emuMMC partition]] or [[emuMMC files]] (the emuMMC itself) and [[Games in the emuMMC]]; choosing the line lights all its parts in the ring; **A** on it explains what an emuMMC is
- **Y** ([[What is an emuMMC?]]) opens the explanation of an emuMMC from anywhere on the page
- [[Game files (NSP, XCI)]] counts /games only; [[Emulators and their games]] counts the emulators (RetroArch, Tico) and the ROM folders: /roms, /ROMs, /retroarch, /RetroArch, /tico, /Tico
-->

<!-- draft
- [[Web browser]] (Tools, Settings group): a page with [[Enter an address]] and the [[Favourites]]; the address comes from the console keyboard or is sent from a phone or a computer (the QR page, as in other text fields; "https://" is added when missing); [[Add to favourites]] sits above the list; on a favourite **A** opens it in the console's own browser (the same applet BrowseNX starts), **X** [[Select]]s it and **Y** [[Invert]]s the marks, as in the File Browser; **START** ([[Options]]) offers [[Open]], [[Add to favourites]] and [[Remove]] for the marked favourites, or for the one under the cursor when none is marked; the favourites are kept in `/config/kefir/web_favorites.txt`; it needs the full memory, so start Kefir Hub from a game (hold **R** while launching it); on firmware 20.0.0 to 22.5.0 the system browser asks for a system update and opens no page, so the row only says that firmware 23.0.1 or newer is needed ([[The browser needs firmware 23.0.1 or newer. Update the firmware in the Updater first.]])
-->

## Battery calibration {#battery-calibration}

**Where:** [[Tools]] → [[Tools]] → [[Battery calibration]]

<!-- draft (0.14.203; the interactive guide with every screen is battery-calibration.md; the guide still shows the 0.14.197 order and is to be redrawn)
- a wizard that teaches the fuel gauge again: [[Why this is needed]] (the gauge can switch the console off at 35 %, or show 0 % for an hour), [[How it works]] (the factory battery model is written first, then two full cycles), [[What you do]], [[How long it takes]] (several hours); a second page [[What happens to the console]] explains the off-and-on loop after the cut-off and the charge; [[Start calibration]]
- the steps: [[1. Reset the gauge]], [[2. Cycle 1: drain, then charge]], [[3. Cycle 2: drain, then charge]], [[4. Report]], [[5. Transfer to the other MMC]]; only the next step is active ([[Active]], [[Locked]], [[Done]]); the state is kept on the SD card (`/config/kefir/battery/calibration.txt`: the reset and the cycles; `calib.txt`: the report, the transfer, the drain in progress), so the Hub can be closed and the console used between the steps; [[Actions]] opens the backup, the align to the voltage, the restore, the match and the battery type
- step 1: [[Reset the gauge]]: the charger in, the Hub waits on the charger until the battery is full, makes a backup of the gauge and writes the factory battery model; what the gauge learned before is dropped
- steps 2 and 3: a cycle is a drain and then a charge; it counts by itself when 90 % of the battery was drained since the reset (the console's own cycle total, so a drain by the user's own use counts too) and the console shows 100 % again; [[Discharged in this cycle]] shows the progress; [[Start the drain]]: the charger out, the Hub loads the CPU and GPU at full brightness (the drain page: battery, power, CPU / GPU / temperature charts, the screensaver on Minus or after the idle time) until the console switches itself off; after every switch-off the user switches the console on again and the Hub goes on by itself, now slowly: no load, auto-brightness off, brightness at the minimum ([[Drain after the cut-off]]); every second a line goes to `/config/kefir/battery/calib-log.csv`; when enough is drained the page says [[The battery ran down enough. Now charge it to 100 %. The cycle then counts by itself.]] ([[Start charging]]: the screen stays on, the screensaver works, the Hub may be closed); taking the charger out after the count starts the next drain by itself
- step 4: [[Calibration report]]: [[Learned capacity]] against design, the [[Verdict]], the gauge before each cut-off (per cycle, from the log); step 5 ([[Transfer to the other MMC]]): the calibration is noted in a ledger on the SD (`/config/kefir/battery/ledger.txt`: where it was made, a number, the battery model); at the next start of the Hub in another system (sysMMC, another emuMMC; a file-based one is told by its folder, a partition one by its start sector) that has an older number and the same battery model, the Hub writes it there by itself and shows [[Battery calibration was transferred from]] ... [[to]] ...
- after a drain the console cut short (the chip shows 30 %, its voltage says 3 %) the Hub offers [[Align the charge to the voltage]] at its start, also while a calibration is under way
-->

## Clean system junk

Deletes what installs and removed games leave behind.

**Where:** [[Tools]] → [[Tools]] → [[Clean system junk]]

The list opens from the bottom. Press A on a row to tick or untick it, then choose [[Start cleanup]]. The message at the end tells how much space came free.

| Item | What it deletes |
|---|---|
| [[Old game updates]] | An update when a newer one of the same game is installed. |
| [[Lost content on the SD card]], [[Lost content in system memory]] | Game files no installed game uses. |
| [[Unfinished installs on the SD card]], [[Unfinished installs in system memory]] | Pieces of installs that stopped half way. |
| [[Unused tickets]] | Tickets of games that are no longer installed. |
| [[Error reports]] | Crash reports in `/atmosphere/erpt_reports`. |
| [[Folders of removed games]] | Folders in `/atmosphere/contents` of games that are no longer on the console. Sysmodules are kept. |
| [[Saves of removed users]] | Saves of users that were deleted from the console. Off by default: these cannot be restored. |

## Fill free SD space with zeros

Overwrites the space no file uses on the memory card with zeros. Files, games and saves stay as they are. Use it
before you sell or hand over a card, so deleted data cannot be recovered.

**Where:** [[Tools]] → [[Tools]] → [[Fill free SD space with zeros]]

1. Select the item and confirm with [[Fill]].
2. Wait. The bar shows how much is written; it can take a long time on a large card.
3. **B** cancels; the space written so far is freed again.

Kefir Hub keeps 64 MB free while it writes, so the system can still save its own data.

<!-- draft
- the fill writes one temporary file, /kefir-hub-zero-fill.tmp, that grows until the card is full; nothing is reserved up front
- two steps on screen: "Step 1 of 2: writing zeros" with written / total, speed, time left and a write speed graph with the current ("Now") and the average speed of the whole fill, the time elapsed and the time left at the average speed; "Step 2 of 2: removing the temporary file, do not turn the console off"
- **B** stops within a second; the temporary file is then removed
- **Minus** ([[Screen off]]) shows the screensaver with the step, written / total, current and average speed, time left, time elapsed and the graph; with [[Inactivity timeout]] set it starts by itself; any button brings the progress back
- the figures change once a second
- the console does not sleep and the screen does not dim while the fill runs
- after a crash or a power-off during a fill, Kefir Hub removes the temporary file at its next start
- the card gets real zeros (the file is not encrypted)
-->

## Problems

**"Could not start this module."** Some modules load only at boot. Set [[Autostart]] and reboot.

**"Failed to install the fan module."** The curve is saved, but until the next reboot the fan uses the previous curve. Reboot the console.

**"No saved Wi-Fi networks".** Add a network in the console's System Settings first.
