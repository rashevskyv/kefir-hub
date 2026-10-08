# System tools

Manage sysmodules, set the fan curve, manage saved Wi-Fi networks and user profiles.

**Where:** [[Tools]] → [[Tools]]

<!-- shot: system-tools-list | Tools submenu: one list under three captions — Diagnostics, Settings, Maintenance -->

<!-- draft
- the list has three captions (v0.14.018): [[Diagnostics]], [[Settings]], [[Maintenance]]; the cursor skips a caption, tapping one does nothing
- v0.14.027: compact rows, every caption and item fits on one screen without scrolling; the item name is on the left, its description on the right of the same row
- the NAND zero-fill item was removed in v0.14.018; [[Fill free SD space with zeros]] stays, now under [[Maintenance]]
-->

| Group | Item | What it is |
|---|---|---|
| [[Diagnostics]] | [[System information]] | Console, Atmosphère, storage, power, battery, hardware and play activity. See [System information](#system-information). |
| [[Settings]] | [[Module Manager]] | Start, stop and set autostart for installed sysmodules. |
| [[Settings]] | [[Fan curve]] | Fan speed curves for handheld and docked mode. |
| [[Settings]] | [[Wi-Fi]] | Saved Wi-Fi networks. |
| [[Settings]] | [[Users]] | Console user profiles. See [Users](users.md). |
| [[Maintenance]] | [[Clean system junk]] | Deletes leftovers. See [Clean system junk](#clean-system-junk). |
| [[Maintenance]] | [[Fill free SD space with zeros]] | Overwrites the unused space of the memory card. See [Fill free SD space with zeros](#fill-free-sd-space-with-zeros). |
| [[Maintenance]] | [[Remove parental controls]] | Planned; opens a "Coming soon" message. |

## [[System information]]

**Where:** [[Tools]] → [[Tools]] → [[System information]]

<!-- shot: system-tools-system-info | System information: Console group open, parameter → value rows, other groups closed -->

<!-- draft
- replaces the text report (v0.14.019): no file is written to the memory card any more
- one list of groups: [[Console]], [[Atmosphere]], [[Storage]], [[Power]], [[Battery]], [[Hardware]], [[Play activity]]; the first group is open at start
- press **A** on a group, or tap it, to open or close it; **A** on a row opens or closes the group the row belongs to; **Y** opens all groups or closes all
- each open group shows rows parameter → value; the number on the right of a closed group is its row count
- v0.14.027 look: a group is a filled band, the open group has a coloured bar on its left and its title in the accent colour; parameter names are bold, values are in the accent colour
- [[Console]]: firmware version, name and hash; model; hardware type and SoC; retail or development unit; burnt fuses; DRAM id; device id; kiosk; charger HiZ; serial number; console nickname; language; region; parental controls
- [[Serial number]] is the real one. [[Serial number source]] says where it was read: [[System settings]], [[PRODINFO partition]] or [[Backup file]] with the file path (Atmosphère `/atmosphere/automatic_backups`, hekate `/backup`). When the system returns a blank serial (Atmosphère blank_prodinfo, Incognito), the row [[Serial number (system)]] shows that blank value. The number is never computed or guessed; if no source has it, the value is [[Not available]]
- [[Atmosphere]]: version, key generation, target firmware, supported firmware, git commit, RCM bug patched, emuMMC (partition or file), blank PRODINFO, PRODINFO writes allowed, USB 3.0 forced
- [[Storage]]: system memory used and free; microSD used and free, speed mode, user and protected area; microSD CID: maker, OEM id, product, revision, serial, month made
- [[Power]]: charger, charging allowed, enough power, charging now, fast charging, USB charger type, USB power role, every current and voltage limit, HiZ, controller power supply, OTG, power delivery state (firmware 17.0.0 or newer for most rows)
- [[Battery]]: charge, raw charge, health, temperature, voltage; from the fuel gauge: design capacity, full capacity now, remaining capacity, charge cycles, age, current, average current, cell voltage, cell temperature, time to empty
- [[Hardware]]: Bluetooth and Wi-Fi MAC, configuration id, battery lot, serial number from the calibration data
- [[Play activity]]: installed games, total play time, total launches, most played game
- a group whose service cannot be read is not shown
-->

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
- FunControl (the fan module) is started by Kefir Hub when you use [[Fan curve]] and stopped automatically.

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

The first time, Kefir Hub offers to install the Kefir fan module. With it, a new curve applies without a reboot
and the screen shows live sensor readings. Choose [[Install]] (recommended) or [[No]]. If the module needs a reboot
to start, Kefir Hub asks [[Later]] or [[Reboot]].

### Edit the curve

1. Press **X** ([[Mode]]) to switch between the handheld and the docked curve.
2. Select a point with **D-pad** up/down, or tap it on the graph.
3. Press **A** ([[Edit]]). Use **D-pad** left/right to change the temperature and up/down to change the fan speed. Press **A** or **B** ([[Done]]) to stop editing.
4. **L** ([[Add Point]]) adds a point; **R** ([[Remove Point]]) removes the selected point. A curve needs at least two points.
5. Press **+** ([[Apply]]) to save both curves and apply them.

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

## Clean system junk

Deletes what installs and removed games leave behind.

**Where:** [[Tools]] → [[Tools]] → [[Clean system junk]]

Turn off what you want to keep, then choose [[Run selected]]. The message at the end tells how much space came free.

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

## Problems

**"Could not start this module."** Some modules load only at boot. Set [[Autostart]] and reboot.

**"Failed to activate fan module."** The curve is saved but not active. Hold **A** to reboot and apply it, or apply it after the next reboot.

**"No saved Wi-Fi networks".** Add a network in the console's System Settings first.
