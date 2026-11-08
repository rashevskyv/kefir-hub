# NSO Nintendo 64: can the per-game config come from a public database?

Status tags: PROVEN = measured by the scripts in `scratch/nso/n64-db/` (logs next to them). LIKELY = fits the evidence.
UNKNOWN = needs a console. Nothing here was run on a console. Format background: `docs/dev/NSO-n64-vb.md`.

## 0. Answer in five lines

1. Save type, rumble, player count and PAL flag CAN be derived from public databases keyed by the header CRC pair.
   Match against the 82 official configs: save type 95 %, player count 99 %, PAL 99 %, pak 88 % (section 2).
2. `Idle[]` CANNOT be derived reliably. A static scan finds 48 % of the official entries and 60 % of the mod entries,
   with a median of 22 candidates per ROM against 1 to 8 real entries (section 3). No public database holds it.
3. `RAMSize` is not in any hardware database in a usable form. Omit it (section 2.4).
4. A generic config is UNKNOWN. Evidence: the mod author calls the first `Idle` entry "Required for the game to boot" (28 times).
5. Recommendation: bundle `n64_known.json` (the 111 known ROMs) + a 15 KB fact table, fill the rest by rule,
   and show a warning for every ROM that is not in the known table (section 5).

## 1. What was measured (honest counts)

The task text says 125 known games. The real numbers:

| Set | db entries | Entries with own `.dtz` (config) | Notes |
|---|---|---|---|
| Official N64 app (base + update v1769472) | 71 | 71 | 71 ROMs (`.bnz` inflated), all with lua + AOT blobs |
| Official Mature 17+ (base + update v131072) | 11 | 11 | Turok 1, Turok 2, Shadow Man |
| Mod "54 Custom ROMs" | 54 | 34 | 17 entries are PNG only, 4 are `.bin` over the official `.dtz` |
| Total | 136 | 116 configs | 111 distinct ROM keys |

5 ROMs are in both the official set and the mod (same header CRC): Jet Force Gemini, Ridge Racer 64 (= official `N-1730`),
Extreme-G, GoldenEye, Donkey Kong 64 (= official `N-4652`). They are a free A/B test (section 3.3).
All 34 mod configs are found by header CRC in mupen64plus.ini and Project64.rdb (`build.log`: found_in_db 34/34). Three are hacks that
mupen lists under their own hack CRC (Earthworm Jim 3D `[T+Rus]`, GoldenEye "G5 Multi Hack" which has the official CRC, Bomberman 64 Arcade
`[T+Eng]`, whose entry has no fields: this is the 1 "unknown" save type). The only 2 of the 38 mod ROMs that mupen lacks belong to the
4 `.bin`-only Russian patches, which have no config of their own. So a header-CRC lookup also works for these translations.

Method: all 116 `.dtz` parsed; ROM header read from the z64 (CRC1 = 0x10, CRC2 = 0x14, cart id = 0x3B..0x3E);
databases parsed from the public files (sizes and licences in section 4).

## 2. Per field: best recipe and match rate

Key = header CRC pair `(crc1, crc2)`; "A" = 82 official configs, "B" = 34 mod configs (the mod author chose some values
for his own reasons, see 2.2). Source: `build.log`, `tables.log`, `modrule.log`.

| NSO field | Public source | Recipe | A official | B mod |
|---|---|---|---|---|
| `BackupType` | mupen64plus.ini `SaveType`; fallback PJ64 `Save Type` | `Eeprom 4KB`->`EPROM4K`, `Eeprom 16KB`->`EPROM16K`, `SRAM`->`SRAM`, `Flash RAM`->`FLASH`, `None`->`NONE` | 78/82 = 95 % exact, 79/82 = 96 % if 4K/16K merged (1 unknown) | 14/34 literal; 32/34 = 94 % with the mod rule (2.2) |
| `SIDevice_PakType` | mupen `Rumble`, `Mempak` | `Rumble=Yes` -> `Rumble Pak`, else `None` | 72/82 = 88 % | 29/34 = 85 % with the mod rule (2.2) |
| `players_count` (db) | mupen `Players` | copy | 81/82 = 99 % | 32/34 = 94 % |
| meta `PAL:1` | header byte 0x3E (region) | region in `D F H I L P S U W X Y` -> PAL | 81/82 = 99 % | 34/34 |
| `TickPerInst` | none | constant 8 | 79/82 = 96 % (11 only in the three Turok 2 entries; Tonic Trouble has none) | 34/34 |
| `RAMSize` | PJ64 `RDRAM Size` | omit | 74/82 = 90 % (8 official games set it) | 29/34 = 85 % |
| `Idle[]` | none | section 3 | see 3 | see 3 |
| `RendererSetting`, `SpecialInst`, lua, AOT | none | not derivable | - | - |
| db `cold_reset`, `GPU384MHz`, `controller_position`, `control_opt_layout_pattern`, `save_count`, `volume` | none | defaults (below) | default hits 67/71, 65/71, 68/71, 41/71, 41/71; volume: no value fits (84 is the mode with 8/71; 80 is not in the official top 12) | - |

Defaults for the db entry: `cold_reset` false, `GPU384MHz` false, `controller_position` "right", `control_opt_layout_pattern` 1,
`save_count` 1, `volume` 80 (the mod uses 80 for 34 of 54; the official values spread from 67 to 110: 84 is the mode with only 8 of 71).
These are UI choices, not emulator needs (LIKELY). `sram_file_size` is 164096 in all 136 entries.

### 2.1 Save type: details (PROVEN)

- mupen `SaveType` is present for 81 of the 82 official ROMs and equals the NSO value in 78; PJ64 `Save Type` exists for only 86 sections and adds nothing.
- The 4 misses: GoldenEye (mupen has no `SaveType` for that CRC; the 1 unknown), Kirby 64 (mupen 16 KB, NSO 4K), and Turok 1 US/PAL (mupen `None`,
  NSO `EEPROM4K`; Mature app, US and German). Turok 1 really saves on the Controller Pak; NSO chose EEPROM. So the official table is not pure hardware truth.
- NSO writes both `EPROM4K` and `EEPROM4K` (same for 16K). The same ROM is `EPROM*` in the official cfg and `EEPROM*` in the mod cfg for GoldenEye (`EPROM4K` / `EEPROM4K`) and DK64 (`EPROM16K` / `EEPROM16K`, `pairs.log`), so they are one class.
  A string search of the app binary found `EPROM4K`, `EPROM16K`, `FLASH` but no `EEPROM*` (the strings are partly packed, so this is weak).
  LIKELY `EEPROM*` is also accepted: the official set has it in 8 games and the mod in 6, and they are reported working. Safe choice for new files: write `EPROM4K` / `EPROM16K`.
- `BackupType` is absent for 7 official games (no save). Absent and `NONE` both mean "no save".
- Absent field in mupen is NOT the same as `SaveType=None`: mupen then tries to auto-detect. Treat absent as unknown.

### 2.2 Pak type: official policy vs mod policy (PROVEN)

- Official: `Rumble Pak` for every game that supports rumble (57 of 82); `None`/absent for the rest. mupen `Rumble=Yes` has
  47 of the 57 (82 % recall, 100 % precision). The 10 misses are rumble games that mupen does not list. Never `Controller Pak`.
  Controller Pak saving is done by the NSO-only keys `CustomSaveData` / `CustomSaveDataNum` (26 games, set by hand).
- Mod: for games that save on the Controller Pak (mupen `SaveType=None` or absent, `Mempak=Yes`) the author wrote
  `BackupType: SRAM` + `SIDevice_PakType: Controller Pak` (18 of 34). Rule that reproduces the mod:
  `if save==NONE and mempak: (SRAM, Controller Pak); elif save==NONE: (SRAM, Rumble Pak if rumble else None); else (save, Rumble Pak if rumble else Controller Pak if mempak else None)`
  -> save 32/34, pak 29/34, both 27/34 (`modrule.py`, `modrule.log`).
- Whether a ROM that saves on the Controller Pak works with this rule on the console is UNKNOWN for new games, but 18 of
  the mod's games use it and the author lists them as playable (README "Список игр и пояснений.txt").
- Fallback for a ROM with no database entry: `SRAM` + `Controller Pak` (the mod author's most common choice, 18/34).

### 2.3 Expansion Pak, `RAMSize` (PROVEN facts, LIKELY conclusion)

PJ64 `RDRAM Size=8` is set for 58 official ROMs; NSO sets `RAMSize` for only 8 (`16M`: Banjo-Tooie, Majora, Ocarina; `8M`: Yoshi's Story)
plus 5 mod games (Hybrid Heaven, Quest 64, RR64, Ocarina `16M`; Rampage `8M`). The mod's Donkey Kong 64 needs the Expansion Pak and has
no `RAMSize`, so the default is already 8 MB or more. Rule: omit `RAMSize`. Cost: the 13 known games with it are in the known table.
No public database lists NSO's `16M` (more than real hardware).

### 2.4 Other public sources checked

- PJ64 `Counter Factor` (1 to 3, 550 entries) is not `TickPerInst` (8 or 11): different scale. No correlation claimed.
- libretro `serial` dat gives `NUS-NB7E-USA` per name (887 games, crc = whole file, not header). It links a cart id to a name.
  Joined by game title to mupen it gives a cart-id table of 677 ids (`cart_table_derived.json`, 46 KB json, ~10 KB packed).
  Used alone (no CRC), it covers 60 of 82 official games (22 cart ids absent); of those it has a save type for 59 and is right in 57.
  It covers 30 of 34 mod games, with save type equal to the mod value in 13 (the mod choices, 2.2). It is the fallback for ROM hacks whose CRC is in no database (none of the 116 known ROMs is such a case).
- No-Intro dat (581 KB) has whole-file CRC32 only; it adds names, not fields. Not used.
- OpenEmu and ParaLLEl-N64 lists were not downloaded: they copy mupen/PJ64 data (UNKNOWN, not checked).

## 3. `Idle[]` (the hard field)

### 3.1 What it is (PROVEN from data, LIKELY for meaning)

230 entries in 116 configs: `{JmpAddr, JmpInst, IsIdle, Comment}`. The app binary has `n64InsertIdle`, `CpuInsertIdleCt`, "in idle".
It is a hook at a guest address: the JIT stops running when the CPU reaches the loop and waits for an interrupt (LIKELY).
`JmpInst` is the 32-bit MIPS word at that address; it is stored so the app can check it (LIKELY).
Forms of `JmpInst` (230 entries, `idle.log`): `0x1000FFFF` (`b .`) 90 (39 %), other self-branches (`bne/beq ... -1`) 22,
`j <self>` 70, `j <other>` 4, short polling loops (`bne v0,zero,-3` and similar, 2 to 10 words back) 36, forward branches 8.
So `b .` is only 39 % of the entries.
Empty `Idle` exists once in 116: Iggy's Reckin' Balls (official `N-2225_e`, which has `SpecialInst` hooks and lua instead).
All 34 mod configs have at least 1 entry; 29 of 34 have exactly 1.
The mod author wrote "Required for the game to boot" on 28 of the 40 mod entries (official configs have no such comments).

### 3.2 Static scan results (PROVEN, `idle.py`, `idle.log`, `idle_rank.log`)

Address map: guest address -> ROM offset = `0x1000 + (JmpAddr - entry)`, `entry` = header 0x08 plus a CIC adjustment from the
CRC32 of the boot code (0x40..0xFFF): `0x90BB6CB5` (6102) and `0x6170A4A1`: 0; `0x0B050EE0` (6103): -0x100000; `0x98BC2C86` (6106): -0x200000.
Only the first 1 MB after the boot code is copied to RAM at start. Everything the game loads later (overlays, compressed code)
is not in the ROM as plain code.

| Question | Official (166 entries, 70 games) | Mod (40 entries, 34 games) | Mature (24 entries, 11 games) |
|---|---|---|---|
| Entry address lies in the boot MB | 130 (78 %) | 32 (80 %) | 2 (8 %) |
| ROM word there equals `JmpInst` | 102 (61 %) | 25 (63 %) | 0 |
| Found by "scan for `b .` / `j self` / self-branch" | 80 (48 %) | 24 (60 %) | 0 |
| Games where at least one known entry is found | 42/70 | 22/34 | 0/11 |
| Games where ALL known entries are found | 34/70 | 21/34 | - |
| Real entry is the lowest candidate address | 23/70 | 15/34 | - |
| Candidates per ROM (min / median / max) | 0 / 22 / 83 | 2 / 17 / 107 | 3 / 13 / 16 |

A scan therefore gives about 20 candidates for 1 to 5 real entries: precision about 5 to 10 %. It misses half of the entries because
the loop is not in the boot MB. The Mature app (Shadow Man) uses another address form for most entries (UNKNOWN meaning). Raw example, `N-2153_e`:
`JmpAddr 0x001032c4`, `JmpInst 0x08040cB1` = `j 0x001032c4` (a j-self at a low, kuseg-style address: game code loaded
after boot, not in the ROM as plain code). With `|0x80000000` the mapped ROM offset holds no such word (0/24).

### 3.3 A/B test: same ROM, two independent authors (PROVEN, `pairs.log`)

For the 5 shared ROMs, the mod author's `Idle[0]` equals the official `Idle[0]` in 5 of 5 (address and instruction). The mod keeps 1 or 2 entries,
the official 1 to 5. So the first entry is the boot-critical one and the rest are for speed or accuracy (LIKELY).
Mod `Idle[0]` for those: Jet Force Gemini `0x8003F5D8`, Extreme-G `0x8004DDE0`, GoldenEye `0x7000071C` and DK64 `0x8000094C` are not
found by the scan (the loop is outside the boot MB or not a self-branch); only RR64 `0x80000510` is found, as the lowest candidate.
So the scan reproduces 1 of 5.

### 3.4 Conclusion for `Idle[]`

- Not derivable from a public database (none exists), not reliable from a scan.
- For a ROM that is in `n64_known.json`: copy. For any other ROM: UNKNOWN whether it boots without or with a guessed entry.
- A heuristic (self-branch or `j self` in the boot MB, lowest address first) gives the right first entry for about 44 % (mod) / 33 % (official)
  of games. A wrong `Idle` entry is a hook at a wrong address: the effect is UNKNOWN (could crash or just do nothing).
  Do not guess it; prefer an empty `Idle` over a guessed one only if (c) shows an empty list boots (UNKNOWN).

## 4. Licences and sizes (read from the sources on 2026-11-08)

| Source | Size | Licence | Fields |
|---|---|---|---|
| mupen64plus-core `data/mupen64plus.ini` (3048 ROM catalog, 1912 distinct CRC keys) | 449 KB | `LICENSES`: "licensed under the GNU General Public License version 2" | `SaveType`, `Players`, `Rumble`, `Mempak`, `CountPerOp` |
| Project64 `Config/Project64.rdb` v4.0 (1347 CRC sections) | 228 KB | GitHub API for project64/project64: GPL-2.0 | `Save Type` (86), `RDRAM Size` (1181), `Counter Factor` (550), `Internal Name` |
| libretro-database `metadat/serial/Nintendo - Nintendo 64.dat` | 90 KB | repo licence CC-BY-SA-4.0 (GitHub API); content is No-Intro style data | serial per name |

The Hub is a GPL project, but GPL-2.0-only data and CC-BY-SA data have their own conditions. Facts such as "this game saves to
EEPROM 4K" are short and factual, but I give no legal opinion. Safe path: ship a derived fact table (`n64_facts.csv`, 1632 rows,
packed 9 bytes per row = 14.7 KB; `make_facts.py` builds it), keep the credit text in the Hub, and do not ship the ini or rdb files.
Raw files are only needed on the PC that builds the table (`scratch/nso/n64-db/src/fetch.sh` downloads them).

## 5. Recommendation

| Field | Auto-fill | Source | Confidence |
|---|---|---|---|
| ROM in `n64_known.json` (key `CRC1-CRC2-cart`; for the 5 keys with two variants write the mod variant, the only one tested without AOT) | everything: `meta`, `cfg`, `Idle`, `RendererSetting`, lua, db fields | the table (111 ROMs, 488 KB json, 87 KB gz) | highest. For the 71 official games the AOT blobs are NOT in the table: the JIT config of the mod style is used. Only the 34 mod configs are known to run without AOT (UNKNOWN for the 82 official configs without their `.nro`). |
| Unknown ROM, CRC in fact table | `BackupType`, `SIDevice_PakType`, `players_count`, `PAL`, `TickPerInst: 8` | `n64_facts` (14.7 KB packed) | 95 / 88 (official policy) or 94 / 85 (mod policy) / 94-99 / 99 / 96 % |
| Unknown ROM, CRC not in table, cart id known | same, by cart id | cart-id table (~10 KB packed) | save ~96 % (official), pak lower |
| Unknown ROM and unknown cart id | `SRAM` + `Controller Pak`, `TickPerInst: 8`, `PAL` by region byte | the mod author's most common choice | low. Show a warning. |
| `Idle[]` | NOT auto-filled | - | Leave empty and warn, or try the scan candidate only if (c) shows that is harmless. |
| `RendererSetting`, `SpecialInst`, lua | `{}` / omit | - | the 19 mod games with `{}` and no lua are the evidence that it can work |
| db: `cold_reset`, `GPU384MHz`, `controller_position`, layout, `save_count`, `volume` | defaults in section 2 | - | UI only |

Which games are unknown: any ROM whose header CRC pair is not in the 111-ROM table (71 official + 11 Mature + 34 mod, minus 5
shared). That is most ROMs a user owns (the mupen catalog alone has 1912 CRC keys). For those the Hub shows: "This game has no tested config. It may not start."
Unknown also means: no cover, no title. Title/cover/players come from the same header (name at 0x20) and `n64_facts`.

## 6. (c) Does the emulator boot with a generic config? UNKNOWN. Evidence

- 19 of the 34 mod configs are minimal: `RomOption{TickPerInst, BackupType, SIDevice_PakType, ...}` + 1 `Idle` entry + `RendererSetting: {}`
  + no `SpecialInst`, no lua, no AOT. The author reports them as playable. So the per-game part needed to start these games is
  small: one `Idle` address plus the save/pak choice. Every one of the 34 has `Idle[0]` and 28 of 40 entries carry the comment "Required for the game to boot".
- The one official config with an empty `Idle` (Iggy's Reckin' Balls) has `SpecialInst` hooks and a lua. Not generic.
- No evidence exists for a config without `Idle`, for a missing `.dtz`, or for `"Idle":[]` on a game that needs one.
  The app binary contains the key names but the parser defaults are not visible (strings are packed). Test on console:
  (1) 0 `Idle` + mod-style `RomOption`; (2) the scan's lowest candidate; (3) the known value. Compare boot.

## 7. Files

`scratch/nso/n64-db/`: `n64_known.json` (the known table; `games[key]` = list of variants; lua base64; AOT blobs not stored),
`n64_facts.csv` (CRC -> save/rumble/mempak/players), `cart_table_derived.json`, `dbs.py` (parsers), `derive.py` (recipe),
`build.py` (+`build.log`), `idle.py` (+`idle.log`, `idle_rows.tsv`), `idle_rank.py` (+log), `pairs.py` (+log), `modrule.py` (+log),
`tables.py` (+log), `dbfields.py` (+log), `make_facts.py`, `load.py`, `src/fetch.sh`. Run with `python -I -X utf8 x.py < /dev/null`
(`-I` hides the script folder: the scripts add it to `sys.path` themselves). The ROM and cfg unpack uses `scratch/nso/n64-vb`
(`nca_romfs.exe` + `ticket_keys.py`; keys stay outside the repo).
Not stored in the repo: the unpacked official romfs (needs the user's keys), the ROMs.
