# Installing games

Kefir Hub installs games, game updates and DLC from package files. This page helps you pick a method and explains the settings that apply to all of them.

!!! warning
    Installing games that you did not buy can get your console banned from Nintendo's online services.

## Ban warning on sysMMC {#ban-warning}

Consoles running sysMMC display a warning before any installation begins to highlight what Nintendo can detect, which online services face potential bans, and why emuMMC offers a safer environment. The dialog provides three options: selecting [[Back]] cancels the installation; choosing [[Install]] proceeds with the process while causing the warning to appear again during future attempts; and clicking [[Don't show again]] disables the reminder entirely and starts the installation.

This warning precedes installations initiated via the File Browser (such as using Install, Install recursively, or pressing **A** on an NSP or XCI file), the Install Forwarder within both the File Browser and Homebrew, Ownfoil, Console Transfer when receiving games, and PC Install via USB. By contrast, MTP and network stream methods—including FTP and the web server—bypass this prompt.

To re-enable the warning later, navigate to [[Tools]] → [[Settings]] → [[Install]] → [[Ban warning on sysMMC]], or access the same option through the sidebar menu via Menu (**Y**) → Advanced → Install options. This corresponds to the `install_ban_warning` configuration key, which defaults to on.

<!-- shot: install-index-ban-warning | Ban warning dialog on sysMMC with Back, Install and Don't show again buttons -->

Following a successful installation, Kefir Hub preserves its own HOME Menu icon in its initial position when launched from that shortcut, preventing the previous behavior where every installation pushed it to the end of the list.

## Choose a method {#methods}

| Method | You need | Review before install | Page |
|---|---|---|---|
| From the microSD card or a USB drive | The files on the card or drive | Yes | [Install from the microSD card](sd-card.md) |
| From a PC over USB | A USB cable and a PC app (DBI Backend, ns-usbloader, Fluffy) | Yes | [Install from a PC over USB](usb.md) |
| From a PC over MTP | A USB cable; the PC file manager | No, it installs while the file is copied | [Install over MTP](mtp.md) |
| Over the network | Wi-Fi; FTP client, web browser or Ownfoil server | Depends on the method | [Install over the network](network.md) |
| From a game card | The card in the slot | — | [Game cards](gamecard.md) |

The microSD and USB methods open the same install queue: every package is checked first, you see how much space it needs and where it will go, then you choose what to install. MTP installs each file as soon as the PC starts copying it, so the file is never stored on the console.

## Supported files {#formats}

- **NSP** and **NSZ** (NSZ is a compressed NSP).
- **XCI** and **XCZ** (XCZ is a compressed XCI).

Compressed files are unpacked during the install, so they need more memory. In Applet Mode memory is limited and NSZ packages are unlikely to install. Start Kefir Hub in Title Mode instead: hold **R** while you launch an installed game until hbmenu opens, or use the Kefir Hub icon on the HOME Menu (see [Getting started](../getting-started.md)).

## Where games go {#location}

Each package goes either to the microSD card or to the system memory (NAND). [[Install location]] decides which:

| Value | What it does |
|---|---|
| [[microSD card only]] | Always the microSD card. |
| [[System memory only]] | Always the system memory. |
| [[System first, then SD]] | System memory until it is full, then the microSD card. |
| [[SD first, then system]] | The microSD card until it is full, then system memory. |
| [[Automatic]] | The storage that leaves free space on the two most even. If the package fits on only one, that one. Default. |

Kefir Hub keeps 500 MB free on each storage when it plans where packages go. Change this in the queue's [[Options]] with [[Reserve free space (system)]] and [[Reserve free space (microSD)]] (see [Queue options](sd-card.md#queue-options)). If the selected packages do not fit after the reserve, the queue asks [[Selected packages may not fit after the configured reserve. Continue?]].

In the install queue you can also set the target of one package: highlight it and press **L3** ([[Package target]]) to switch between [[Auto]], [[microSD]] and [[System memory]].

## Install options {#install-options}

**Where:** [[Tools]] → [[Settings]] → [[Install]]

<!-- shot: install-index-settings-install | Settings, Install page with the three groups visible -->

[[Where and when]]

| Option | What it does | Default |
|---|---|---|
| [[Ban warning on sysMMC]] | Warns before every install while the console runs sysMMC, see [Ban warning on sysMMC](#ban-warning). | On |
| [[Install location]] | Where packages go, see [Where games go](#location). | [[Automatic]] |
| [[Allow downgrade]] | Lets you install a game update older than the one installed. When off, an older update is skipped. | Off |
| [[Skip if already installed]] | What to do with content that is already installed: [[Reinstall]], [[Skip]], or [[Prompt]] (ask [[Already installed. Reinstall?]] each time). | [[Skip]] |
| [[Save options globally]] | On: changes you make in the queue's [[Options]] are saved to Settings. Off: they apply to the current queue only. | On |
| [[Boost CPU during transfer]] | Runs the CPU faster during transfers. | On |
| [[Screen off (Minus)]] | What **−** does while a queue runs, see [Turn the screen off](sd-card.md#screen-off). | — |

<!-- draft
- new option [[Several updates of one game]] in this table, after [[Allow downgrade]]: values [[Newest]] and [[Ask]], default [[Newest]]
- [[Newest]]: when the queue holds two or more updates of one game (version from the file name, e.g. [v196608]), only the highest one installs; the others are skipped with "a newer update is in the queue"
- [[Ask]]: each of those updates asks "The queue has N updates for this game. Install vX?"; the first **Yes** picks that update, the other updates of the game are skipped
- an update older than the one on the console, and the only update of that game in the queue, is always asked about (a translation or mod may need it), in both modes; with several updates and [[Newest]] the console's newer update stays
- the queue does not wait for any answer: a file with a question ([[Ask]] with several updates, an update older than the console, "Already installed. Reinstall?") goes to the end of the queue at once, no box, and the next file starts; the log says [[Asked after the queue: ]]
- after the queue the page [[Skipped during the queue]] opens: one row per game (a Reinstall row per file), with [[On the console: ]] and [[In the queue: ]] versions; A = [[Change]] (choices [[Keep ]]vN / [[Skip]], [[Install ]]vN, [[Reinstall]]), X = [[Resolve]] installs the choices, B = [[Skip all]]; default of every row: keep the console as it is
- shot: the Resolve page with two games (an older update and two updates in the queue)
- MTP, FTP and the web upload send each file once, so they cannot defer: the question box waits 10 seconds, then the file is skipped and the console keeps what it has (also for "Reinstall?")
- [[Allow downgrade]] on still installs an older update without a question
- the Install recursively order no longer decides which update stays (before: the last file in alphabetical order won)
-->

[[What to install]]

| Option | What it does | Default |
|---|---|---|
| [[Install tickets only]] | Installs only the tickets (licences). Content that is missing is still written. Use it when a game is installed but its ticket is missing or damaged, see [Repair tickets](#repair-tickets). | Off |
| [[Skip base game]] | Does not install the base game. | Off |
| [[Skip game updates]] | Does not install updates. | Off |
| [[Skip DLC]] | Does not install DLC. | Off |
| [[Skip DLC updates]] | Does not install DLC updates. | Off |
| [[Skip tickets]] | Does not install tickets. Not recommended: the game may not start. | Off |

[[Verification and conversion]]

| Option | What it does | Default |
|---|---|---|
| [[Skip NCA hash verify]] | Skips the check that the game data is not corrupted. | On |
| [[Skip RSA header verify]] | Skips the check that the data headers are signed by Nintendo. Needed for converted NSP/XCI files. | On |
| [[Skip RSA NPDM verify]] | Not implemented; has no effect. | On |
| [[Ignore origin flag]] | Ignores the mark that says content came from a game card or from the eShop. Content marked as game card normally does not start when installed. | Off |
| [[Convert ticket on install]] | Turns personalised tickets into common ones. Needs the console keys. | On |
| [[Convert to standard crypto]] | Installs the game so it does not need a ticket. Needs the console keys. | Off |
| [[Re-encrypt to master key 0]] | Re-encrypts the game keys so any firmware can read them. The game may still not work; updating the firmware is the better fix. Needs the console keys. | Off |
| [[Lower required firmware]] | Lowers the firmware version the game asks for. Does not help if the game needs keys your firmware does not have. | On |

The queue's [[Options]] menu (press **+** in the queue) has [[Skip if already installed]], [[Install location]] and the reserve settings, so you can change them for one session. See [Queue options](sd-card.md#queue-options).

MTP, FTP and web-browser installs do not use [[Skip if already installed]], [[Convert to standard crypto]] or [[Re-encrypt to master key 0]]: content is always installed again, and the game is not converted.

### Repair tickets {#repair-tickets}
For installs from the microSD card, a USB drive or a PC over USB, [[Skip if already installed]] set to [[Skip]]
skips the whole package of a game that is already installed, ticket included. [[Install tickets only]] alone
therefore does nothing for an installed game.

1. Set [[Install tickets only]] to On.
2. Set [[Skip if already installed]] to [[Reinstall]].
3. Install the game's package again.

MTP, FTP and web-browser installs always import the tickets.

## Problems {#problems}

Upon successfully registering all content records within a package on HOS 6.0.0 or later, Kefir Hub automatically resets the local launch-required version of the base game to zero for each installed update without requiring manual intervention or configuration. This behavior functions during normal and streamed installations, though skipped updates remain unqueued. Should a subsequent content record fail or get canceled, any updates registered prior will undergo a best-effort reset while preserving the original installation error message. Package versions remain unchanged, meaning this process neither bypasses online version checks enforced by Nintendo nor eliminates requirements dictated by DLC, save files, or system firmware. Any failures encountered during the reset are logged as installation errors while keeping the already registered content intact, and readback values along with any associated errors are recorded in the log for console verification.

After an install from any source that leaves an update or DLC without its base game, Kefir Hub waits about five seconds after the last file and shows a box listing those titles as "Installed without the base game". Select [[Keep]] to leave them or [[Delete]] to remove them, noting that saves are kept. Keep them if the base game is on a game card or if you plan to install the base game afterwards. This box also appears for the DBI and PC queue.

**"The title was installed successfully, but cannot run on this console yet".** The game needs newer system firmware than the console has. Update the firmware and Kefir (see [Updater](../updater.md)). You do not need to install the game again.

**A package is skipped with "already installed".** [[Skip if already installed]] is set to [[Skip]]. Set it to [[Reinstall]] or [[Prompt]].

**An update is skipped.** It is older than the installed update. Turn on [[Allow downgrade]] if you want to go back to it.
<!-- draft
- with [[Several updates of one game]] = [[Newest]] an older update is also skipped when the queue has a newer update of that game ("a newer update is in the queue") or when the console has a newer one and the queue had several updates
- the only update of a game that is older than the console's is asked about instead of skipped
-->

**NSZ files fail in Applet Mode.** Start Kefir Hub in Title Mode.

<!-- draft
- an install fails with the error "Your keys are missing or outdated" when the console keys in /switch/prod.keys are missing, wrong or older than the game needs; nothing is installed
- after the error box is closed and the install has stopped (MTP and FTP: about five seconds after the last file), Kefir Hub asks one question: "Reboot and dump the keys?"; buttons [[No]] and [[Reboot]]
- [[No]]: nothing else happens
- [[Reboot]]: the console restarts; Kefir's hekate starts Lockpick_RCM before the boot logo, Lockpick saves new /switch/prod.keys, and the console starts as usual; Kefir Hub does not open by itself; install the game again
- how: Kefir Hub deletes /config/autokeys.bin (hekate's record of the last key dump); hekate then dumps the keys on the next autoboot
- /bootloader/payloads/Lockpick_RCM.bin missing: Kefir Hub says so and does not restart
- /config/autokeys.disable on the microSD: automatic key dump is off; Kefir Hub says so and does not restart
- the dump runs only when hekate autoboots (Kefir default); a hekate without Kefir's patches restarts without a dump
- a game that needs newer system firmware than the console has gives the same error after a fresh dump: update the firmware (see [Updater](../updater.md))
-->
<!-- TODO(verify): which result code a too-new game gives on old firmware (MasterKey missing vs. the other key codes) -->
<!-- shot: install-keys-dump | the question "Reboot and dump the keys?" with No / Reboot after a failed install -->

**You need the error details later.** Install errors are written to `/config/kefir/errors.txt` on the microSD card, even when logging is off.
