# Video tutorials

One folder per video: `NN-topic/` containing
- `script.md`: setup notes and the scene table below. The text is the voiceover; the docs pages are the source of truth.
- `subs.uk.srt`, `subs.en.srt`: draft subtitles generated from the table by `python docs/video/srt.py [NN-topic]`.
  The timing is a reading-speed estimate. After recording, re-time it against the voice track; the text stays the same.

## script.md format
```
# NN. Title
**Length:** ~N min · **Docs:** saves.md, …
**Before recording:** console state needed (UI language Ukrainian, which games, saves, backups, network…)

| # | Shot | Action | Voiceover (UK) | Voiceover (EN) |
|---|---|---|---|---|
| 1 | saves-list | Tools → Game Tools → Saves | … | … |
```
- `Shot` is a shot id from the docs (`python docs/site/shotlist.py`) when the scene shows that screen. Otherwise give a short
  description (`talking head`, `PC: Explorer window`).
- `Action` is what to press on the console during the scene, so the recording can follow the script.
- No `|` inside cells. One scene is one table row, 1-3 sentences of voiceover.

## Videos
| # | Topic | Docs |
|---|---|---|
| 01-overview | What Kefir Hub is: launching it, the screen, buttons, tabs, language | index, getting-started |
| 02-install-games | Installing games: from the microSD card, from a PC over USB, over MTP | install/index, sd-card, usb, mtp |
| 03-network | Network installs and file access from a PC or phone: web page, FTP, Ownfoil | install/network, sharing |
| 04-saves | Saves: backup, restore, DBI/JKSV/Checkpoint backups, WebDAV | saves |
| 05-games | Managing games: move NAND↔SD, delete, dump, save slots | games |
| 06-update | Updating Kefir, Kefir Hub and firmware; downgrade | updater, settings |
| 07-users | Profiles, offline account link, moving to another console | users, console-transfer |
| 08-cheats-themes | Cheats and themes | cheats, themes |
| 09-tools | File browser, homebrew, App Store, Module Manager, fan curve, Wi-Fi, Kefir Settings | file-browser, homebrew, software, system-tools, kefir-settings |
