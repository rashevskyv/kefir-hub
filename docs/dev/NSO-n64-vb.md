# NSO Nintendo 64 and Virtual Boy: how a ROM becomes an entry

Status tags: PROVEN = reproduced byte for byte by `scratch/nso/n64-vb/n64_entry.py --verify` (log: `verify.log`) or read
directly from the official files. LIKELY = fits all evidence, no direct test. UNKNOWN = not testable on this PC.
Nothing here was run on a console.

Sources: official N64 base 0100C9A00ECE6000 + update v1769472 (71 titles), Mature 17+ 0100E0601C632000 + update v131072
(11 titles), Virtual Boy 0100BFC01D976000 + update v196608 (16 titles), and the mod "54 Custom ROMs (RUS, ENG)".
The romfs was unpacked with nca_romfs (base + patch). The files of the mod go under the BASE title id.

## 1. Answer to the owner question (LayeredFS: is ROM + cover enough?)

No. The new game must also be in `lclassics.titlesdb`, and that file is REPLACED as a whole. There is no other index.

Evidence (PROVEN):
- The only code that lists games is `bootapp/resources/scripts/system/titlelistmanager.lua` + `titlesdatabase.lua`.
  They read ONE file, `/titles/lclassics.titlesdb` (JSON, key `titles`). No hash, count, index or signature file
  references the games (grep of the whole official romfs: only that lua file mentions `lclassics`).
- The mod db has 54 entries, the official db has 71. 17 official games are missing in the mod db, so they are hidden
  in the app. So the overlay db replaces the official one; entries you omit disappear.
- Order is not stored. The app sorts at run time with `sort_title` (the lua asserts it exists: "invalid titleInfo").
  The code suffix decides the list: `_e` (NTSC list), `_p*` (PAL list). `hidden_countries:["kr"]` hides by account country.
- No count limit was seen (mod has 54, official has 71). UNKNOWN if there is a hard cap.
- Strings: the game name comes from the db (`title`, `title_ko`, `title_zhHans`, `title_zhHant`; other languages use
  `title`). `bootapp/.../strings/*/strings.lng` is UI text only and is NOT needed.

Minimal complete change sets:

| Case | Files to write | titlesdb |
|---|---|---|
| A. Replace the ROM of an official game (translation) | `titles/<code>/<code>.bin` (raw z64). Optional new PNGs. | none: the official db stays. (PROVEN by the mod: 4 entries are `.bin` only, 17 are PNG only.) |
| B. Add ONE new game to an existing mod | new dir `titles/<newcode>/`: `<newcode>.bin` or `.bnz`, `<newcode>.dtz`, `<newcode>.png`, `<newcode>-details.png` | read the mod db, add one entry, write it back. Keep all other entries. |
| C. Add ONE new game to a clean install | same dir as B | the db must be the FULL official db (71 entries for N64) + the new entry. |

For case C the Hub needs the official db text. Options: read it from the installed title's romfs at run time
(UNKNOWN: not tested whether homebrew can open another title's patched romfs on the console), or embed a snapshot in the
Hub (brittle: every NSO update adds titles; the new official games would vanish until the snapshot is updated).
Safer rule for the Hub: if an overlay db already exists, edit it; else build it from the installed romfs; else refuse.

## 2. N64 on-disk formats (all PROVEN unless marked)

Per game: `/atmosphere/contents/0100C9A00ECE6000/romfs/titles/<code>/`

| File | Content |
|---|---|
| `<code>.bin` | the raw ROM in BIG-ENDIAN `.z64` order (first bytes `80 37 12 40`). 5 of 5 mod files. Size = ROM size, 8 to 64 MB seen. |
| `<code>.bnz` | zlib stream (`78 9c` or `78 da`) of the same z64 ROM. Official games use it. 33 new mod games use it. |
| `<code>.dtz` | zlib stream of an old-binary cpio archive (see 3). Holds the per-game emulator config. |
| `<code>.png` | cover. `<code>-details.png` detail screen. |

Which of `.bin` and `.bnz` the app takes: the db `rom` field says `.bin` for every game, also for official games that
exist only as `.bnz`. So the app finds `.bnz` when `.bin` is absent. The mod puts a `.bin` next to the official `.bnz`/
`.dtz` and the author reports it works (Majora RUS, Mario Kart RUS, Super Mario 64 RUS, Banjo EU RUS), so `.bin` has
priority (LIKELY, attested by the mod README, not reproduced). A NEW code with `.bin` only has no example: the 33 new mod games all use `.bnz`.
Safe default for the Hub: write `.bnz` (zlib, level 9, inflate gives the z64) for new codes. UNKNOWN: `.bin` for a new code.
The compressed stream of `.bnz` does not match zlib 1.3 or zlib-ng at any level/memLevel/strategy (the mod tool used
another deflate). That is irrelevant: the app only inflates. Inflate(.bnz) is a valid z64 for all 33 (PROVEN).

Byte order: `.z64` `80371240`, `.v64` `37804012` (every 16-bit pair swapped), `.n64` `40123780` (every 32-bit word
reversed). The Hub must convert to z64. Round trip z64 -> v64 -> z64 and z64 -> n64 -> z64 is identical on all 38
mod ROMs. No CRC fix and no header change is made by the mod (ROMs are used as they are; the mod holds patched
Russian ROMs). ROM sizes in the mod are multiples of 1 MB (8, 12, 16, 32 MB...). No padding was tested (UNKNOWN).

Region fallback (LIKELY: lua strings `enableTitleRegionFallback`, "NTSC : Keep ... because no ..."; one mod case): a `_p`
entry is shown in the default NTSC list only when no `_e` entry with the same number exists. The mod deleted `N-4868_e`
and kept `N-4868_p` (Banjo RUS), so it appears. To surface a PAL ROM, do not create the `_e` sibling.

Code and region: `N-<4 digits>_e` (8 chars). The mod uses invented numbers 4435-9564 for new games and keeps the
official code when it replaces a game. Use `_e` for any NTSC ROM and `_p` for PAL ROMs shown in the PAL list.
PAL ROMs have `"PAL": 1` in meta `OptionInfo` (39 of 39 PAL cfgs; PROVEN). The region letter is header byte 0x3E.

## 3. The `.dtz` per-game config (the hard part)

Decompressed: old binary cpio (magic 0x71c7, LE). Header 13 x u16: magic, dev, ino, mode, uid, gid, nlink, rdev,
mtime (hi, lo), namesize (incl NUL), filesize (hi, lo); then name (padded to even), data (padded to even); last
entry `TRAILER!!!` (the app binary contains that string). Mod layout: dev `0x8080`, other fields 0, mtime = unix
time of the file, names `<dir>\NN_<tag>.<ext>` with Windows backslash. `<tag>` is `XXXXXX.000` in 31 of the new games; the
official tag is the game code (for example `Unzse0.727`). The directory part is random text ("metapack", "Hybrid Heaven
(USA)", a Jenkins path): it is ignored (LIKELY: 33 different values all work).

Files inside (NN decides the type):
- `00_<tag>.meta` JSON: `{"Hardware":"NUS","TitleCode":"<tag or 00_XXXXXX.000>","DataVersion":"1.0.0","OptionInfo":{"SndSampleRate":32016}}`.
  `OptionInfo` keys seen: SndSampleRate (32016/22052/32005...), NerdStreamFillRatio 0.35, PAL 1, SndReSample*.
- `02_<tag>.cfg` JSON: `RomOption`, `RSP`, `Idle[]`, `SpecialInst[]`, `RendererSetting`, optional `VI`, `AI`, `Cmp`.
- `06_<tag>.lua` optional Lua hook (GoldenEye, Doom 64, Ocarina). `08_*.spv` shader (Ocarina only).
- Official dtz additionally holds `03_*.pcb`, `05_*.nro` (AOT native code, up to 31 MB), `07_*.ppl`, `tex_*.rpt`.
  The mod's new games have none of them; the app runs them with its JIT plugin (`nro/libHovercraftJITPlugin.nro` in romfs). Attested by the mod author (the README lists per-game glitches, so the games were run); not reproduced here.

`RomOption` vocabulary (82 official + mod cfgs):
- `BackupType` (save type): `FLASH` 24, `EPROM4K` 18, `EPROM16K` 11, `SRAM` 7, `NONE` 7, `EEPROM4K` 6, `EEPROM16K` 2.
  The N64 header has NO save-type field. This needs a table keyed by the cartridge id (header 0x3B-0x3E) or by CRC.
- `SIDevice_PakType`: `Rumble Pak` (57), `Controller Pak`, `None`. `RAMSize` `8M`/`16M` (else default 4M). `TickPerInst` 8 (3x 11).
- Optional: `StickLimit` 90, `ScanlineCalculateMode` `Special`, `ScanlineDefaultValue` 10, `AIDMAMode` `Timer`/`TimerEx`,
  `CustomSaveData`/`CustomSaveDataNum`, `FrameTicks`, `InitWith480P`, `JoyStickReset`...
- `Idle[]`: `{ "JmpAddr":"0x8000196C","JmpInst":"0x1000FFFF","IsIdle":"0x1" }` = the address of the game idle loop
  (a `b .` instruction). One mod entry comments "Required for the game to boot". This address is per game, found by
  analysis. 0 entries in 1 cfg, otherwise 1 to 8.
- `RendererSetting` is empty (`{}`) or lists graphics fixes (ModifyCC, VertexHack, Clip*, CopyAtEnd...).

Consequence (the real blocker): for an arbitrary user ROM the Hub cannot invent `BackupType`, `Idle` and graphics fixes.
It needs a database of cfgs keyed by cartridge id / CRC (the mod authors made one per game; 29 are in the mod) or a
"generic" cfg that nobody tested. UNKNOWN: whether `"Idle":[]` and a generic cfg boot a game. Without a `.dtz` at
all: UNKNOWN (all 33 new mod games have one; the 4 `.bin`-only entries use the official `.dtz` beneath).

Proof: `n64_entry.py` rebuilt all 34 mod `.dtz` from (meta, cfg, lua, mtimes, dir, tag, extra) byte for byte with stock
zlib 1.3 (level 9 for 33, level 6 for 1; the level is only a tool detail).

## 4. titlesdb entry (N64)

File: UTF-8, no BOM, no newline, `json.dumps(db, separators=(',',':'), ensure_ascii=False)`. PROVEN: re-serialising
the official file (52492 B) and the mod file (35030 B) gives identical bytes. Key order in a file is irrelevant to the
app (the official file sorts keys; the mod file has its own order); the converter uses the mod order.

Fields of a new N64 entry (23 fields, constants as in 33/33 new mod entries; `make_entry()` rebuilt all 33 byte for byte):

| Field | Value / meaning |
|---|---|
| `code` | `N-xxxx_e` same as folder |
| `title`, `title_ko` | display name (mod copies it into `title_ko`) ; `title_zhHans`,`title_zhHant`: `"ー"` (U+30FC) |
| `sort_title` | lower-case title; the app sorts by it. `sort_publisher`: lower-case publisher |
| `publisher`, `copyright` | text. Mod uses `"©"+publisher` |
| `release_date` | `YYYY-MM-DD`; `lcla6_release_date` : date the entry was "added" (mod: 2022-06-12) |
| `players_count` | 1 to 4 ; `simultaneous` false ; `save_count` 1 (number of save slots; official 1 to 8) |
| `cover`, `details_screen`, `rom` | `/titles/<code>/<code>.png`, `...-details.png`, `...<code>.bin` (always `.bin` even if `.bnz` is used) |
| `cold_reset` | true makes a cold reset on start (11 of 33 new games true) |
| `controller_position` | `"right"` (79) or `"home"` (3): Joy-Con grip side shown in the controls help |
| `control_opt_layout_pattern` | 1..4 : which preset of `kc.dat` the controls menu offers |
| `GPU384MHz` | true = "overclock": GPU at 384 MHz (6 of 82 official games; 0 in mod new) |
| `sram_file_size` | 164096 in all 82+54 entries |
| `volume` | 71 to 118 (mod new: 80). LIKELY the per-game sound gain in percent |
| optional | `keymap`, `keyassign_type`, `disable_handcursor`, `launch_caution`, `hidden_countries`, `display_version` |

Covers: official `.png` 512x357 RGBA8 (landscape), `-details.png` 400x300. The mod uses 341x512 portrait covers (50) and
400x300 details; the app showed them (LIKELY: it scales). Recommend 512x357 cover, 400x300 details, PNG 8-bit.
A missing PNG: the lua loads images through a loading queue and prints "Image loading error"; behaviour UNKNOWN. Always write both.
The cover itself is not converted by the app (no `.xtx.z`) - plain PNG file copy.

## 5. Mature 17+ (0100E0601C632000)

Same format and loader: own `lclassics.titlesdb` (11 entries: Perfect Dark, Shadow Man, Turok 1, Turok 2, regions
e/p/pd/pf/px), `.bnz` + `.dtz` + `.png` + `-details.png`. A user ROM can be added to either app with the same code; the
overlay path is `/atmosphere/contents/0100E0601C632000/romfs/`. The Hub must give the user the choice (two N64 apps
installed) and keep one db per app. The Mature app has its own full db, so case C applies to it separately.

## 6. Virtual Boy (0100BFC01D976000 / 0100BFC01D976800)

Feasible. PROVEN from the unpacked official app (db re-serialises byte-exact like the N64 db; the Mature db too): same "l-classics" framework (`bootapp/` + `titles/lclassics.titlesdb`
+ `titles/V-xxxx_e/`), 16 games. Per game only:
- `V-xxxx_e.vb` = the RAW Virtual Boy ROM, no wrapper, no compression: 512 KB, 1 MB or 2 MB; the standard header
  sits at `size-0x220` (title 20 bytes, maker, game id; "VIRTUAL BOWLING", "JACK BROS"...). A user `.vb` file can be
  copied as it is (LIKELY: header layout is standard, not compared to a No-Intro dump). UNKNOWN: odd sizes (not power of two), 4 MB+.
- `V-xxxx_e.png` cover 512x512 (2 games 512x411), `V-xxxx_e-details.png` 768x224 (all 16).
- No `.dtz`, no `.bnz`, no per-game cfg: nothing to look up. Save: `save_count` 1.
titlesdb fields (16 entries): `code, title, title_ko, title_zhHans, title_zhHant, sort_title, sort_publisher, publisher,
copyright, release_date, lcla6_release_date, players_count, save_count, volume (88..118), rom, cover, details_screen,
rewind_interval 1.5`; optional `ipd_offset` (-2.0, 2 games), `fadein` ([1], Teleroboxer), `display_version`.
Same rules as section 1: the overlay db replaces the official one; clean install needs the official 16 entries + 1.
Note: no sample VB mod exists on this PC, so an added VB game is LIKELY, not PROVEN. Hub steps: copy `.vb`, add entry, PNGs.

## 7. Hub implementation (C++ on the console)

N64 add (needs only zlib, libnx fs, a JSON writer with the same byte output):
1. read the ROM, detect the order by the first 4 bytes, convert to z64 (two loops, section 2);
2. from header 0x3B-0x3E pick the cfg template from a built-in table (UNKNOWN content: needs a cfg database);
3. write `<code>.bnz` (`compress2(level 9)`) or `.bin`, `<code>.dtz` (cpio builder = 30 lines + `compress2`), the two PNGs;
4. read the target db (overlay if present, else official), insert the entry, write compact UTF-8 JSON.
Ambiguous extension `.bin` for NES/Genesis/N64: for N64 a `.bin` ROM is identified by the first 4 bytes (z64/v64/n64 magic).

## 8. Files

- `scratch/nso/n64-vb/n64_entry.py` converter + `--verify` + `--demo`; `n64cpio.py` cpio parse/build.
- `scratch/nso/n64-vb/verify.log`, `demo.log` proof output; `unpack.sh`, `romfs.sh`, `ticket_keys.py` unpack helpers (keys stay outside the repo);
  `nso_strings.py` string search in NSO binaries.
- Run: `python3 -I n64_entry.py --verify <mod romfs/titles> <official romfs/titles>` (use stock zlib; Windows zlib-ng matches 31 of 34 dtz).
- `--demo`: rebuilt 3 entries (N-4435_e, N-9269_e, N-6724_e) as files from a v64 ROM + meta/cfg: `.bnz` inflates to the original z64,
  `.dtz` byte-exact, entry equal, output db = official 71 + 3 = 74 entries. `add_game()` writes `.bnz` by default (`raw_bin=True` for `.bin`).
- Verify result: titlesdb round trip OK (mod + official); 33/33 new entries rebuilt byte for byte from 11 inputs;
  34/34 `.dtz` rebuilt byte for byte; 5 `.bin` and 33 `.bnz` ROMs = valid z64, v64/n64 round trip OK; 116 checks, 0 fail.
- Not proven: `.bnz` bytes (stream differs, content identical), `.bin` for a new code, running anything on a console,
  that an app accepts a cfg that is not from the mod.
