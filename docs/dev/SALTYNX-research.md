# SaltyNX and DBI: research for Kefir Hub

Labels: **VERIFIED** (read in a source), **LIKELY** (inferred), **UNKNOWN** (not found).
No console was used. DBI behavior comes from strings in DBI 921 (`D:\git\dev\dbi_patcher\DBI.921.ru.nro`) and `dbi_patcher\data\dictionary.xlsx`.
Note: `dbi_patcher` is at `D:\git\dev\dbi_patcher`, not inside the kefir-hub repo.

## Main finding

DBI does NOT show FPS, resolution or docked/handheld values. DBI has no "edit SaltyNX values" menu that I can find.
DBI only does two SaltyNX things:

| What | Evidence | Label |
|---|---|---|
| Menu line "Add to SaltyNX exceptions" (RU "Добавить в исключения SaltyNX") | dictionary.xlsx and NRO strings | VERIFIED |
| Toasts "Games added to the exceptions list" / "No new games were added to the exceptions list" | dictionary.xlsx | VERIFIED |
| Format string `X{:016X}` (a line "X" + 16-digit Title ID) | NRO strings | VERIFIED string; meaning LIKELY |
| Path format `/SaltySD/patches/{:016X}/{}` | NRO strings | VERIFIED |
| Many `*.asm32`/`*.asm64` names: `nn::nifm::IsNetworkAvailable`, `nn::account::ShowNintendoAccountNnidLinker`, `nn::oe::DisableRecording`, `nn::oe::DisableScreenShot`, `nn::oe::SetPerformanceConfiguration`, `SetCpuBoostMode`, etc. | NRO strings | VERIFIED |
| Toggles "Patch names (HOS 21+)", "Patch video recording", "Patch screenshots", "Patch NNID requirement", "Patch version in meta" | dictionary.xlsx | VERIFIED strings |

Searches for "Reverse", "FPS", "Docked", "Handheld", "Resolution" in the dictionary found only unrelated text ("B-Frames", codec strings).
Menu items, value ranges and defaults of a per-game SaltyNX screen: **UNKNOWN**. They do not exist in the strings.

LIKELY reading of the above:
- "Add to SaltyNX exceptions" appends the game's Title ID to `/SaltySD/exceptions.txt`, so SaltyNX does not inject into that game. LIKELY (matches the SaltyNX README below).
- The `.asm64` files let DBI write per-game SaltyNX patches that stub network, NNID, recording and screenshot functions. The write path is `/SaltySD/patches/<TID16>/<symbol>.asm64`. LIKELY. The toggle list (NNID, video, screenshots) fits these symbols.
- The user may have seen the exceptions menu and the patch toggles, and read them as "SaltNX values". UNKNOWN: which exact DBI screen the user means. Ask for a screenshot.

## 1. What SaltyNX is

Source: https://github.com/masagrator/SaltyNX (README).

- A sysmodule that injects custom code into retail games. VERIFIED.
- By default it injects "Core": NX-FPS (FPS stats and unlock) and ReverseNX-RT (docked/handheld switching). VERIFIED.
- It also handles custom refresh rates. VERIFIED.
- Install: copy the `atmosphere` and `SaltySD` folders to the SD root. VERIFIED.
- Needs firmware 10.0.0 or newer. "No technical support for anything else than stock Atmosphere." VERIFIED.
- Plugins are ELF files in `/SaltySD/plugins` (example: `NX-FPS.elf`). VERIFIED (NX-FPS README).
- Title ID of the sysmodule in the Hub catalog: `0000000000534C56`. VERIFIED (`assets/romfs/modules/homebrew_sysmodules.json`).
- Part of the Kefir pack: UNKNOWN. Hub only lists it as a known sysmodule. No Hub source code uses SaltySD (grep of `source/`, `include/` found nothing).

Files and folders SaltyNX reads (VERIFIED from README unless noted):

| Path | Purpose |
|---|---|
| `/SaltySD/exceptions.txt` | Blacklist of Title IDs |
| `/SaltySD/patches/*.asm64` / `*.asm32` | Patches for all games |
| `/SaltySD/patches/<titleid>/*.asm64` | Patches for one game |
| `/SaltySD/plugins/*.elf` | Plugins |
| `/SaltySD/plugins/FPSLocker/patches/<TITLEID>/<BUILDID>.bin` | FPSLocker patch, written by the overlay |
| `/SaltySD/plugins/FPSLocker/ExtDisplays/<ConfigID>` | External display config (format UNKNOWN) |

## 2. Related plugins and their settings

| Tool | What it does | Settings come from | Label |
|---|---|---|---|
| NX-FPS (plugin) | FPS read-out and lock | Overlay (FPSLocker) | VERIFIED |
| FPSLocker (Tesla overlay) | FPS target 15-60 (docked table up to 120), Interval Mode 1-4, buffering, display refresh, per-game "delete settings/patches" | Overlay UI; YAML config converted to a binary patch | VERIFIED |
| ReverseNX-RT (Tesla overlay) | Force docked or handheld in a running game; "system control" mode | Overlay; saved settings; per-title flags via ReverseNX-Tool | VERIFIED (concept); file path UNKNOWN |

Sources: https://github.com/masagrator/FPSLocker, https://github.com/masagrator/NX-FPS, https://github.com/masagrator/ReverseNX-RT.

## 3. Global or per game

- FPSLocker patches: per Title ID and per Build ID (file path above). VERIFIED.
- FPSLocker FPS target: set while a game runs. Whether it is stored per title: UNKNOWN. The overlay has per-game "Delete settings", so some per-game storage exists. LIKELY.
- SaltyNX patches and exceptions: per Title ID (folder or line). VERIFIED.
- ReverseNX-RT flags: per title (README mentions "per-title flags"). Storage path UNKNOWN.

## 4. Compatibility and risks (VERIFIED from READMEs)

- 32-bit games: plugins are ignored by default.
- Witcher 3 GOTY 3.2: not compatible (heap).
- FF VIII Remastered, FF X/X-2, LEGO Harry Potter Collection: NX-FPS does not work (framework in NRO).
- Cheats that use HEAP+offset may break.
- Online games (example Splatoon 3): ban risk, because code is injected.
- NX-FPS README: kernel panic risk if you close a game too early after the logo.
- FPSLocker: forced triple buffering fails or crashes on 4 listed games; "Sync Wait" off can crash; high refresh rates can crash; the author takes no responsibility for display damage.
- ReverseNX-RT: games that never check their mode show "Game doesn't support changing modes!"
- After a SaltyNX update that misbehaves, the README advises deleting the whole `SaltySD` folder and reinstalling.

## 5. Config format Hub could write

| Item | Format | Label |
|---|---|---|
| `exceptions.txt` | One Title ID per line, 16 hex digits. Prefix `X` = no Core injection at all. Prefix `R` = no Core when a romfs mod exists. No prefix = no plugins only. | VERIFIED (README text); exact whitespace/case rules UNKNOWN |
| `patches/<TID>/<symbol>.asm64` | Raw instruction bytes ("write with hex editor instructions") for the function named by the file | VERIFIED text; byte encoding of DBI's payload UNKNOWN |
| FPSLocker config | YAML, up to 32 kB, from FPSLocker-Warehouse; overlay converts to `.bin` | VERIFIED; schema UNKNOWN |
| NX-FPS / ReverseNX-RT per-title files | Not documented in the README text I fetched | UNKNOWN |

Not checked: the Tesla/Ultrahand overlay source code and the FPSLocker-Warehouse YAML schema. Fetch them before writing any FPS file.

## 6. Proposal

Minimal useful feature, low risk, no overlay clone:

1. Game list sidebar or per-game page: one row "SaltyNX: excluded from injection" (toggle).
   - On: append `<TID>` to `/SaltySD/exceptions.txt` (plain form, or `X<TID>`).
   - Off: remove that line.
   - Show the row only if `/SaltySD/` exists on the SD card.
2. Optional second row, same page: "Delete FPSLocker patches for this game" (remove `/SaltySD/plugins/FPSLocker/patches/<TID>/`).
3. Do not offer FPS or resolution values in Hub. The overlay owns them, they depend on the Build ID, and a wrong value can crash a game.
4. Do not write `.asm64` patches. The payload bytes are UNKNOWN and a wrong patch can crash a game.

Needs on disk: `/SaltySD/exceptions.txt` (create if missing, keep unknown lines, keep line order and endings).

Testable without a console (host tests, new pure-logic header):
- Parse and rewrite `exceptions.txt` content (add, remove, `X`/`R` prefixes, blank lines, comments, CRLF, duplicates).
- Title ID formatting to 16 uppercase hex digits.

Needs the console (user): whether SaltyNX accepts the line, and whether the game starts with or without Core.

Open questions for the user: which DBI screen did you see? Is it the "Add to SaltyNX exceptions" line? A screenshot ends the UNKNOWN items in the Main finding.
