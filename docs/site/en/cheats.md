# [[Cheats]]

Download cheat codes for your games, import your own cheat files, look at what is installed and remove it. Atmosphère loads the cheats from the microSD card when the game starts.

**Where:** [[Tools]] → [[Game Tools]] → [[Cheats]]

<!-- shot: cheats-menu | Cheats menu with all eight entries, Download Kefir Cheats focused -->

## How cheats work
Each game's cheats are a text file named after the game's **Build ID** — a code that changes with every game update:

`/atmosphere/contents/<title id>/cheats/<BUILD ID>.txt`

A cheat file only works for the exact game version it was made for. After a game update, the old file no longer matches; see [Fix cheats after a game update](#fix-cheats-after-a-game-update).

Kefir Hub installs, shows and deletes cheat files. It does not turn single cheats on or off: do that in the game with the EdiZon overlay in the Tesla menu. Kefir opens the Tesla menu with **L** + **R3** + **D-pad** down.

## Install a complete cheat pack
1. Choose [[Download Kefir Cheats]] (cheats for many games) or [[Download 60FPS/GFX Cheats]] (frame rate, resolution and graphics cheats).
2. Confirm with [[Download]].
3. Wait for [[Cheats pack installed]].

The pack comes from the switch-cheats-db project and is unpacked into `/atmosphere`. Existing cheat files with the same name are overwritten. You need an internet connection.

## Download cheats for one game
1. Choose [[Download Exact Cheats]].
2. A list of your installed games opens. Focus a game and press **A**.
3. Kefir Hub finds the game's Build ID and loads the matching cheats. The Build ID is shown next to the game name at the top.
4. Pick cheats:
    - **A** — [[Toggle]] the focused cheat.
    - **X** — [[Select All]] (press again to unselect all).
    - **R** — [[Preview]] the cheat code.
5. Press **Y** ([[Download]]) and confirm.

<!-- shot: cheats-select | Cheat list for one game: game name and Build ID at the top, three cheats ticked [X] -->

New cheats are added to the game's existing cheat file; cheats with the same name are skipped. One file holds at most 128 cheats — Atmosphère reads no more; extra cheats are not installed.

<!-- draft
- replaces the 128 limit sentence above (v0.14.032): one file holds at most 127 cheats plus one master code {..}; Atmosphere reads none of the file when one rule is broken, so Kefir Hub now writes only what it accepts: a second master code, a cheat longer than 256 code words and cheats past 127 are skipped, and a message says how many ("Atmosphere limits: skipped N cheat(s)")
- the whole file is checked again each time cheats are added, also the cheats that were in it before; manual import uses the same check
- Build ID fix (v0.14.032): when the game was running or no prod.keys were found, Kefir Hub read the Build ID with its bytes reversed, so the file got a wrong name and Atmosphere did not load it; files made that way can be renamed with Y (Fix BID) in the game's cheat file list
-->

!!! tip
    Start the game once, close it, and then download. Kefir Hub reads the Build ID most reliably from the running or installed game. If it cannot, it looks the version up online, which can be wrong for some games.

<!-- draft
- v0.14.033: Kefir dumps the console keys (prod.keys) at boot, so this is rare; when Kefir Hub needs them to read the Build ID and /switch/prod.keys is missing, it says so and offers to dump them now with Lockpick_RCM (the console restarts into the payload); if Lockpick_RCM is not in /bootloader/payloads, the message says to put it there
- this replaces the online version guess in the tip above for the no-keys case; the same offer appears for manual import and Fix BID
-->

## Import a cheat file
Use this for a `.txt` cheat file you copied to the microSD card.

1. Choose [[Import From File]].
2. Pick the `.txt` file.
3. Pick the game it belongs to.
4. The dialog shows the target Build ID. If the file is named after another Build ID (or not after a Build ID at all), it is renamed to the target one. Confirm with **Import**.

If the game already has a cheat file for that Build ID, it is replaced.

## View installed cheats
1. Choose [[View Cheats]]. Every installed game that has cheats is listed with its title ID and number of cheat files.
2. Press **A** on a game. Its cheat files are listed by Build ID.
3. Press **A** on a file. The cheats in it are listed.
4. Press **A** ([[View Code]]) to see a cheat's code.

<!-- shot: cheats-files | Cheat files of one game: two files listed by Build ID, footer A View / X Delete / Y Fix BID -->

## Fix cheats after a game update
If a game was updated, its old cheat file has the old Build ID and is not loaded.

1. [[View Cheats]] → the game → focus the cheat file.
2. Press **Y** ([[Fix BID]]).
3. The dialog shows the old and the new Build ID. Confirm.

The file is renamed to the current Build ID. The codes inside are not changed: cheats made for an older version may not work, or may misbehave, after an update.

## Delete cheats
- One file: [[View Cheats]] → the game → focus the file → **X** ([[Delete]]).
- One game: [[View Cheats]] → focus the game → **Y** ([[Delete]]).
- All installed games: [[Delete All Cheats]]. Removes the cheat folders of every game installed on the console.
- Games you no longer have: [[Delete Orphaned]]. Removes cheats of games that are not installed.

## Clear the download cache
[[Clear Cheats Cache]] deletes the cheat database Kefir Hub keeps for [[Download Exact Cheats]] (`/config/hats-tools/cheats-db`). The next download fetches it again. Installed cheats are not touched.

## Files
| What | Path on the microSD card |
|---|---|
| Cheat files | `/atmosphere/contents/<title id>/cheats/<BUILD ID>.txt` |
| Download cache | `/config/hats-tools/cheats-db` |
| Pack download (temporary) | `/config/kefir-updater/cheats.zip` |

## Options
In the game list of [[Download Exact Cheats]] and [[Import From File]], press **+** for [[Cheats Options]]:

| Option | What it does | Default |
|---|---|---|
| [[Layout]] | [[Icon]], [[Grid]] or [[HB Menu]]. | [[Icon]] |

Press **X** ([[Refresh]]) there to scan the games again.

## Problems
**[[Cheats Not Found]].** The database has no cheats for this game, or none for its Build ID (game version).

**The cheats do nothing in the game.** The file's Build ID does not match the installed version. Use [Fix BID](#fix-cheats-after-a-game-update) or download cheats again.

**A dialog says the Build ID could not be determined.** Start the game once, close it, and try again.

**[[No installed cheats found]].** No installed game has a cheat file.

**[[Failed to install cheats pack]].** Check the internet connection and the free space on the microSD card, then try again.
