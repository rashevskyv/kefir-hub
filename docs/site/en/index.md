# Kefir Hub

Kefir Hub is the homebrew app that comes with Kefir. It launches your homebrew, installs games, manages saves,
cheats, themes and user profiles, and updates Kefir and the firmware, all from one place on the console.

<!-- shot: index-main-screen | Homebrew tab right after launch, header and footer visible -->

## Start Kefir Hub
Kefir Hub is a homebrew app (`.nro`), so it starts from the Homebrew Menu like any other homebrew.
It looks for itself at `/switch/kefir-hub/kefir-hub.nro` and `/switch/kefir-hub.nro` on the memory card.

1. Open the Homebrew Menu: hold **R** while you open the Album or start any installed game, and keep holding
   it until the Homebrew Menu opens. (This is Kefir's setting. On plain Atmosphère the Album opens it without **R**.)
2. Select Kefir Hub and press **A**.


The way you start it matters:

- **From the Album (applet mode).** Kefir Hub gets little memory. Large compressed games (NSZ) are unlikely to
  install, and the web server is slower.
- **From a game with R held (title mode), or from a HOME Menu icon.** Kefir Hub gets the full memory.
  Use this mode for installing games.

!!! tip
    To get a HOME Menu icon that always starts Kefir Hub in title mode, go to the [[Tools]] tab,
    press **+** and choose [[Install Title Mode forwarder]]. When it is done, return to HOME and start the new icon.

### Make Kefir Hub open instead of the Homebrew Menu
[[Settings]] → [[Homebrew]] → [[Replace hbmenu on exit]]. When this is on, Kefir Hub copies itself to
`/hbmenu.nro` each time you exit it, so the Album and the R-held game open Kefir Hub directly.
Turn it off to get the old Homebrew Menu back: Kefir Hub asks [[Restore hbmenu?]] and restores it from
`/switch/hbmenu.nro`. Default: Off.

## First start

<!-- shot: index-first-start | First start page: Kefir Hub title, HOME Menu icon advice, A = Choose language -->

<!-- draft
- new in v0.14.025: the first start opens a full page in the console's language, not the main menu with a list on top; the menu is not drawn until the language is chosen
- the page says: Kefir Hub adds its own icon to the HOME Menu; start it from that icon from now on (all memory, USB port); the Homebrew Menu and the Album give only a part
- press **A** ([[Choose language]]) to open the [[Language]] list; it opens on the console's language
- a language other than the page's own: a dialog says Kefir Hub restarts now, start it from the HOME Menu icon if it does not come back; [[OK]] restarts; the same language: no restart, the main screen appears at once
-->
1. Pick your language. The list opens on the console language. Kefir Hub restarts once so that every
   menu, tile and dialog uses it. You can change it later, see
   [Getting started](getting-started.md#change-the-language).
2. If some user profiles on the console are not linked to a Nintendo Account, Kefir Hub offers to link them.
   See [Users](users.md).

## The two tabs
Kefir Hub has two tabs. Press **R** to go to [[Tools]], press **L** or **B** to go back to [[Homebrew]].

- **[[Homebrew]]** lists the homebrew apps on your memory card. See [Homebrew](homebrew.md).
- **[[Tools]]** is a grid of everything else:

| Tile | What it opens |
|---|---|
| [[File Browser]] | Files on the memory card, USB drives and network shares. [File Browser](file-browser.md) |
| [[Game Tools]] | [[Games]], [[Saves]] and [[Cheats]]. [Games](games.md), [Saves](saves.md), [Cheats](cheats.md) |
| [[Themes]] | Theme packs for the HOME Menu. [Themes](themes.md) |
| [[Updater]] | Kefir and firmware updates. [Updater](updater.md) |
| [[Software]] | App Store, DBI and other software. [Software](software.md) |
| [[Tools]] | [[Module Manager]], [[Fan curve]], [[Wi-Fi]], [[Users]]. [System tools](system-tools.md), [Users](users.md) |
| [[Kefir Settings]] | Console settings that belong to Kefir. [Kefir settings](kefir-settings.md) |
| [[Settings]] | Settings of Kefir Hub itself. [Settings](settings.md) |
| [[Console Transfer]] | Share files and backups with another device. [Console transfer](console-transfer.md) |

<!-- shot: index-tools-tab | Tools tab grid, File Browser tile selected -->

## What can I do
- Launch, sort and remove homebrew: [Homebrew](homebrew.md)
- Copy, move, unpack and edit files: [File Browser](file-browser.md)
- Install games from the memory card, a PC, a USB drive, the network or a game card: [Installing games](install/index.md)
- Back up and restore saves: [Saves](saves.md)
- Download cheats: [Cheats](cheats.md)
- Update Kefir and the firmware: [Updater](updater.md)
- Share the memory card with a PC over MTP, FTP or a browser: [Sharing](sharing.md)
- Fix a problem: [Troubleshooting](troubleshooting.md)
