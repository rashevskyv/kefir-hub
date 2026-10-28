# Install from a PC over USB

Send games from a PC to the console over a USB cable. Kefir Hub works with the PC apps DBI Backend (including DBI Backend Qt), ns-usbloader (Awoo/Tinfoil or GoldLeaf mode) and Fluffy. It finds out which app is on the other end by itself.

**Where:** [[Tools]] → press **+** ([[Install & Share]]) → [[PC Install (USB)]]

The same item is in the **+** menu of the Homebrew tab.

On sysMMC, Kefir Hub shows the ban warning when you choose [[PC Install (USB)]]. See [Ban warning on sysMMC](index.md#ban-warning).

<!-- shot: install-usb-sidebar | Install & Share menu with PC Install (USB) highlighted -->

## What you need {#requirements}

- A USB cable between the console and the PC.
- One of the PC apps above. Its stream mode must be off: the console checks every package before installing, and a stream-mode app cannot send them that way.
- On Windows, a USB driver for the console. Windows has none of its own, so a PC app cannot open the console until one is installed.
    - DBI Backend Qt 2.9.0 or newer installs the WinUSB driver itself. The first time it finds the console without a driver, it asks to install it and Windows asks for administrator permission once. You can also start it from **Help → Install USB Driver** in DBI Backend Qt.
    - For ns-usbloader and Fluffy, install the driver as their instructions say (libusbK with Zadig).
    - A driver you already installed with Zadig keeps working; DBI Backend Qt leaves it as it is.
- On Linux, DBI Backend Qt offers to add a udev rule (it asks for your password) if your user cannot open the console. macOS needs nothing.

## Install games {#install}

1. Open [[PC Install (USB)]]. The console shows [[Waiting for PC]] and a badge with the USB speed.
2. If the cable was already in and MTP is running, start the PC install app afterwards. If it cannot reach the console over USB because MTP and the install link are different USB devices, write a file named `kefir-hub.pc-install` to any MTP storage or copy a file with that name to the memory card in Explorer. Kefir Hub asks "A PC install app is waiting. Stop MTP and open PC Install (USB)?": [[PC Install (USB)]] stops MTP and looks for the app for a few seconds as on plug-in, [[No]] keeps MTP; the file is removed from the card.
3. Connect the cable and start the PC app.
4. In the PC app, add the files and start the transfer.
5. The console reads the list and checks every package. Nothing is installed yet.
6. The queue opens. Check the targets and sizes, select what you want, and press **A** ([[Install selected]]).
7. Wait for the [[Session summary]].

When the USB queue finishes, Kefir Hub ends the PC session first and then waits on [[Session summary]]. DBI Backend Qt can stop its server and switch modes while the summary stays open. Press **B** ([[Back]]) to close the screen. To install again over USB, open [[PC Install (USB)]] again and start the PC server. Leaving the session no longer freezes the screen for seconds.

The queue, the progress screen, minimizing, screen off and the summary work the same as for files on the microSD card. See [Review the queue](sd-card.md#queue) and the sections after it.

<!-- shot: install-usb-waiting | PC Install (USB) waiting screen with the USB 2.0 badge -->
<!-- shot: install-usb-queue | Queue with packages received from DBI Backend Qt -->

## Open automatically on connect {#auto-open}

Set [[PC Install on connect]] (on by default) in Settings → Network. When a handheld console is plugged into a computer by cable, after 2 seconds Kefir Hub opens the USB install link and waits a few seconds for a PC app that is already running (DBI Backend, ns-usbloader, Goldleaf). If one answers, [[PC Install (USB)]] opens by itself with the file list already loaded. If nobody answers, MTP starts as before. The PC app must be started before or right after plugging in; Kefir Hub cannot start the app on the computer. Turn the setting off to always get MTP on connect.

## Change the queue from the PC {#live-queue}

With DBI Backend Qt and the queue plan command, the sync runs both ways. A tick removed or a target changed on the console (**X**, **Y**, **L3**) is sent to the PC within a moment and stays; it no longer comes back on the next poll. The PC app shows for every [[Auto]] package where the console will really put it ("Auto → SD" / "Auto → NAND"), from the console's plan. The PC app's NAND and microSD bars are drawn like the console's: before the install the selected packages are projected into the free space (red when they do not fit), the row under the mouse in amber at the head; while installing, the remaining bytes of the active package. **+** → [[Sort]] on the console keeps its order while the PC keeps updating the queue; new keys [[Target]] (microSD first) and [[Status]] (ticked, unticked, already installed, failed analysis).

With DBI Backend Qt the queue stays in sync with the PC app:

- Selecting or unselecting a package, changing the order, and choosing [[Auto]], [[microSD]] or [[System memory]] for a package on the PC is shown on the console.
- This also works while the queue is installing, for packages that have not started yet.
- The console sends its free space to the PC app.

With DBI Backend Qt, when you confirm [[Skip package]] for the active USB package, the Hub sends the skipped status to the PC. The PC row becomes Skipped and its checkbox clears while the queue continues. A skip confirmation for a package that has already finished does not interrupt the next package.

<!-- TODO(verify): which DBI Backend Qt version supports the live queue, and where can users download it? -->

## Releases with ports, translations and mods {#releases}

DBI Backend Qt 2.9.10 and later: drop a folder with ports, port files, translations or mods to open "Choose what to install".

- The window has one block per game and a tick box per option. The boxes are pre-ticked in the PC's language, then English. One mod is ticked only when the game has exactly one. All ports of a shared `switch/` folder (for example Donkey Kong Country 1-3) are ticked.
- "Choose What to Install..." in the queue row's menu changes the choice until the game starts. The choice is kept in presets.
- A mod with its own update installs that update instead of the newer one in the release.
- Each ticked option becomes one queue row, `<game> - <option>.kefir-sd.tar`, after the game's packages. The target is the SD card. Kefir Hub 0.14.100 and later writes the files to the microSD. Archives (zip, 7z, rar) are sent as they are and extracted on the console.
- The Hub never writes `Nintendo/`, `emuMMC/` or `bootloader/`. Save files of a port that are already on the console stay.
- This works over USB only for now. An older Kefir Hub marks such a row as failed and goes on with the queue.
<!-- shot: install-usb-release-dialog | DBI Backend Qt: Choose what to install, Donkey Kong Country Trilogy -->

## The USB speed badge {#usb-speed}

The waiting screen shows the USB mode:

- USB 3.0, when USB 3.0 is turned on in Atmosphère (`usb30_force_enabled` in `/atmosphere/config/system_settings.ini`) or the link runs at USB 3.0 speed.
- USB 2.0 otherwise.

With USB 3.0 turned on, the badge also shows if the cable or port only gives a USB 2.0 link.

## What changes while it runs {#while-running}

- MTP is turned off during a USB install; you see [[Disable MTP for usb install]]. It is turned on again when you close the screen ([[Re-enabled MTP]]).
- USB drives connected to the console are unmounted during the session and mounted again afterwards.

## Problems {#problems}

**The console stays on the waiting screen.** Check the cable, then start the transfer in the PC app. The console keeps looking for the PC until you press **B** ([[Cancel session]]).

**The PC app does not find the console on Windows.** The USB driver is missing. In DBI Backend Qt, choose **Help → Install USB Driver** and allow the administrator prompt. If you declined the prompt or have no administrator rights, ask the PC's administrator to run it.

**The PC app is in stream mode.** The console shows [[USB session failed]] and asks you to turn stream mode off. Turn it off in the PC app and open [[PC Install (USB)]] again.

**The cable comes loose during an install.** The log shows that the connection was lost and the console tries to reconnect and retry the package. If it cannot, the session ends and the remaining packages are skipped. Check the cable and the port, then start again.

**A warning about Applet Mode on the waiting screen.** In Applet Mode memory is limited and NSZ packages are unlikely to install. Start Kefir Hub in Title Mode (see [Supported files](index.md#formats)).
