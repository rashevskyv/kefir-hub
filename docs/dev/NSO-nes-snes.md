# NSO add-a-ROM: NES and SNES

Status tags: PROVEN = rebuilt byte for byte by `scratch/nso/nes-snes/rebuild_check.py` (130 checks, 0 bad).
LIKELY = follows from data, not tested on a console. UNKNOWN = not found.

Samples: SNES base v0 + "Phoenix 1.1.0c HIDE" update (544 ROMs), NES base v0 + "HIDE 479 ROMs" update.
Both unpacked with hactool + nca_romfs. Raw ROMs: `D:\Emulators\roms\roms\snes\*.zip` and `...\nes\*.nes`.

## 1. Answer to the owner: what must change to add ONE game

The overlay is a LayeredFS file replacement. It replaces `titles/lclassics.titlesdb` as a WHOLE file.

Minimal complete set (same for a clean install and for an existing mod):

| # | Change | Status |
|---|--------|--------|
| 1 | Write ROM file to `/atmosphere/contents/<base TitleID>/romfs/titles/<folder>/...` | PROVEN |
| 2 | Write cover `<code>.xtx.z` and details screen `<code>-details.xtx.z` (NES: `<code>00.xtx.z`) in the same folder | PROVEN (all 552 + 499 entries have both; 0 files missing) |
| 3 | SNES only: write the `.sfromsig` next to the `.sfrom` (256 bytes, see section 3) | PROVEN present for every entry |
| 4 | Write the NEW `lclassics.titlesdb` = every existing entry + the new entry | PROVEN |

Differences:
- **Clean install (no mod):** there is no overlay file yet. Hub must READ the official `lclassics.titlesdb` from the
  installed game (RomFS of the game NCA, or the update NCA if one is installed; the update wins), copy it,
  add the new entry, write the result to the overlay path. If Hub writes only the new entry, the 20 official
  games disappear. The official file is JSON; it must stay valid UTF-8 JSON (the ReadMe says a broken file gives an error at start).
- **Existing mod:** the overlay file already exists and holds the official entries. Parse it, add the entry, write it back.
- Official entries never need to be edited. The HIDE mod rewrote 5 of the 20 (only the `title` text; 15 are identical).

What does NOT need to be edited (PROVEN by search): no index file, no hash list, no count field and no
other JSON key. `titles` is the only top-level key in both systems. In the SNES RomFS the only other place that
names the official codes is `bootapp/resources/strings/*/strings.lng` (`META_TITLE_COMMENT_S_2002_e`, flavour
text for official games). Not checked whether mod entries have such strings; the ReadMe says the mod games run, so treat it as optional (LIKELY).
No region filter was found: mod entries are plain dict entries; the Lua list loader (`titlelistmanager.lua`, bytecode)
has no region check in its string table. Sort order: UNKNOWN. The mod gives added games a future
`lcla6_release_date` (SNES `2030-11-xx`, NES `2032-xx-xx`); the official ones are `2018-09-01`/`2019-09-01`.
Likely the app sorts by this date + `sort_title`; the Phoenix ReadMe says the list order is "stored in the save",
so an old save may show new games unsorted ("delete the save").

Limits:
- SNES: ReadMe says that above about 290 total ROMs the app shows an error when it exits (not a crash while playing). The 544-ROM
  pack lives with it. Plan on at most ~290 total titles.
- ReadMe: ROMs that need a Super FX/DSP-4 etc. may not start; an NSP with a modified main cannot also take LayeredFS adds.
- Largest ROM in the SNES pack: 7 340 032 bytes (7 MB). Largest NES: 1 048 592 bytes (1 MB + 16). Sizes seen in SNES: 256 KB..7 MB; any multiple of 1 KB works.

## 2. Title ids and file names

| | SNES | NES |
|---|---|---|
| Base / update TitleID | 01008D300C50C000 / ...C800 | 0100D870045B6000 / ...6800 |
| `code` (key in `titles`) | `S-NNNN_e` (NNNN = 2002..2029 official, mod 2101..) | `CLV-P-N<3 chars>E`, optional `-spN` for special/startup_state variants |
| `titles` type | JSON object keyed by `code` (both) | list in v0 base, object keyed by `code` in the v917504 update (PROVEN) |
| Folder | any name (mod uses `BatmanForever`, `ZJ_YsIV`); `rom` path in the db is what counts | any name (mod: `10YARDFIG`); `-spN` entries reuse the folder of the parent code |
| ROM | `NNNN_e.sfrom` (+ `NNNN_e.sfromsig`) | `<code>.nes` (FDS: `<code>.qd`) |
| Cover | `S-NNNN_e.xtx.z` | `<code>.xtx.z` (official PAL covers sit in `/titles/_PAL/`) |
| Details | `S-NNNN_e-details.xtx.z` | `<code>00.xtx.z` |
| Save path | derived from `code`: `/saves/<code>/cartridge.sram` (from the Lua string table, LIKELY) | same |

Rule for a new code: take a number/code that is not in the db. `code` must be unique and match the file names (PROVEN: all 552 and 499 entries are consistent).
The cover format (`.xtx.z`) is covered by another topic.

## 3. SNES `.sfrom` and `.sfromsig`

### `.sfrom` layout (PROVEN, 30 mod entries rebuilt byte-exact from raw ROMs)

```
[ headerless SNES ROM, unchanged, any size multiple of 1 KB ]   (strip the 512-byte copier header if size % 1024 == 512)
[ cfg bytes, n bytes ]
[ u32 little-endian n ]
[ "Can1" ]                                                       (43 61 6e 31)
```

`Can1` is the Nintendo "Canoe" SNES core of the SNES Classic family. The footer is 18..28 bytes in practice.
All 572 files (20 official + 552 mod) end in `Can1`.

### The cfg blob (UNKNOWN meaning; PROVEN usage)

Always starts `47 02 00 00` (u32 0x247, version?), then 2-byte records. 40 distinct blobs exist.
- Default used by the mod for 425 of 552 games: `47020000 0010 7406 6301 6a38 7002`.
- Second most common (82): `47020000 0010 7406 7002`.
- Not derivable from the ROM header (checked: map mode, ROM type, SRAM size, size do not predict it).
- Chip games reuse the cfg of the official sibling: mod Doom = `47020000 4411 7406 7001` = the cfg of official `S-2003_e` (Super FX).
  The mod also reuses the sig of that sibling (same md5 `8680d4...`).
- Practical rule for Hub: LoROM/HiROM without special chip -> default blob. Super FX/SA-1/DSP games -> UNKNOWN; the
  Phoenix ReadMe lists several that "do not start" (Top Gear 3000 DSP-4, Street Fighter Alpha 2, Dirt Trax FX...).

### `.sfromsig` is a 256-byte blob, and it is NOT checked against the ROM in the mod (PROVEN for the HIDE update NSP)

- Size 256 bytes = RSA-2048 signature size. The 20 official files all differ (20 distinct blobs). LIKELY a real RSA-2048 signature over something.
- In the 544-ROM mod: 510 files are byte-identical to the signature of official `S-2016_e` (Super Mario Kart), 20 to `S-2017_e`,
  4 to `S-2003_e`. The same blob sits next to 18 different footers and hundreds of different ROMs.
- Nobody holds the signing key. So the blobs are only valid signatures for the official ROMs. A single signature
  cannot be valid for hundreds of different ROMs. Therefore, in that mod, the app does not verify it against the ROM.
- Therefore: NO signing key and NO PC tool is needed to add a SNES ROM. Copy any official `.sfromsig` (256 bytes) next to it.
  Hub can read it from the installed game RomFS at runtime (do not ship the blob in the Hub source).
- Caveat (UNKNOWN): the proof comes from the update NSP, whose `main` (v1.1.0 plus "HIDE" patch) differs from the base
  main (the sizes differ: the update is a newer build, so a code diff does not isolate the HIDE patch). The ReadMe says a LayeredFS route
  also works with the stock game, and it asks to use LayeredFS for extra ROMs. So the stock exefs most likely does not verify either,
  but this needs one console test: add one ROM with a copied sig and one with 256 zero bytes. If the stock app rejects it, the owner
  needs an exefs patch (IPS/`atmosphere/exefs_patches`) that disables the check; I did NOT find the check in the code and did not find
  any "Full Unlock" patch or note on this PC or in a web search (UNKNOWN).
- Not found by string search: the sig check is native code in `main`/`subsdk`. The Lua scripts (`bootapp/resources/scripts`, Lua 5.1 bytecode) only
  load the db, covers and the ROM path.

## 4. NES ROM wrapper (PROVEN)

The NES ROM is a plain **iNES file** with the normal 16-byte header and the normal extension `.nes`.
- 97 mod/official `.nes` files are byte-identical to files rebuilt by `nes_wrap(raw)`: keep header bytes 0..7, set bytes 8..15 to 0, keep PRG/CHR.
  (112 are identical to raw dumps outright; the others differ only by junk in bytes 9-15, e.g. "DiskDude!".)
- Mapper, mirroring, battery and trainer data live in the standard iNES flags 6/7 (and NES 2.0). There is NO extra metadata file and NO footer.
- Official editions sometimes fix flag 6 (mirroring bit) of a bad dump: `CLV-P-NAAWE` (Mario Bros.) and `CLV-P-NMSPE` differ from the raw dump in that bit only.
  Hub should not touch flag 6.
- Famicom Disk System games use `.qd` files (30 entries) with extra `fds_*` keys (section 5) and the FDS BIOS in `kachikachi/`. Raw `.fds` -> `.qd` conversion is UNKNOWN (not studied).
- One oddity: official Excitebike `CLV-P-NAAHE.nes` has a 96-byte non-iNES prefix (Wii U style); not needed for adds.
- Max NES ROM seen: 1 MB. Unsupported mappers: UNKNOWN (the pack covers ~30 mappers incl. MMC1/3/5, VRC; NSO NES core limits are not documented).

## 5. `lclassics.titlesdb` fields

File: UTF-8 JSON, `{"titles": {<code>: {...}}}`. The mod uses `ensure_ascii` style escapes (`\u30fc`); a normal UTF-8 writer is fine.
All 552 SNES entries have exactly the same 22 keys; 454 of 499 NES entries have 23, the rest add `fds_*` (30) or `startup_state` (15).

SNES entry (rebuilt exactly by `snes_entry`, PROVEN for S-2120_e and S-2170_e):
`code, rom, sort_title (= lower(title)), details_screen, copyright ("added for your fun" in the mod), title, armet_threshold 0,
lcla6_release_date, armet_version "off", sort_publisher, title_zhHant "ー", save_count 1, volume 100, title_zhHans "ー",
publisher, title_ko (= title), rewind_interval 1.5, release_date, players_count, cover, simultaneous (bool), autoplay []`

NES entry (rebuilt exactly by `nes_entry`, PROVEN for CLV-P-NTYFE):
`code, rom, sort_title, details_screen, copyright, title, armet_threshold 80, lcla6_release_date, armet_version "v1", sort_publisher,
title_zhHant, save_count 0, volume 72, fadein [3,2], title_zhHans, publisher, title_ko "-", rewind_interval 1.5, release_date,
players_count, cover, overscan [0,0,9,3], simultaneous`
Optional: `startup_state` (the `-spN` variants), `fds_auto_disk_side_switch`, `fds_auto_disk_side_switch_on_keypress`,
`fds_bios_skip_intro`, `fds_disk_switch_side_delay`, `fds_disk_switch_side_threshold` (`.qd` only).
`armet_version` is `v1` (436) or `v2_blend` (63): per-game audio/video tuning, `v1` is the safe default.
`save_count` 0 for all added NES games in the mod (battery saves in added games: UNKNOWN; official battery games use 1).
Which keys are strictly required: the Lua loader prints "Invalid or incomplete title information : Skipping title" and uses
`cover`, `details_screen`, `rom`, `code`; the rest are LIKELY read by the UI; keep the full set to be safe.

## 6. Hub on-console algorithm (needs only file I/O + JSON)

1. Check the update TitleID or base TitleID is installed. Extension decides the system: `.sfc/.smc/.swc/.fig` -> SNES, `.nes` -> NES, `.fds/.qd` -> NES (FDS, UNKNOWN convert).
2. SNES: strip copier header, append `cfg + len + "Can1"`, pick a free `S-NNNN_e`, write `NNNN_e.sfrom`, copy a donor `.sfromsig`.
   NES: zero header bytes 8..15, write `<code>.nes`.
3. Cover + details textures (other topic).
4. Load the current db (overlay if present, else the game's RomFS), add the entry, write it atomically to the overlay path.

## 7. Files

- `scratch/nso/nes-snes/nsoconv.py` - wrappers + entry builders (stdlib only).
- `scratch/nso/nes-snes/rebuild_check.py` - proof: `python -I rebuild_check.py < /dev/null` (needs `work/`; 156 MB of trimmed unpacked samples).
- `scratch/nso/nes-snes/work/` - titlesdbs of base and mod, 30 recovered `.sfrom`, all NES `.nes`.
- Unpack recipe: `hactool -t pfs0 --outdir=X file.nsp`; title key = 16 bytes at 0x180 of the `.tik`; `nca_romfs prod.keys title.keys out base.nca [patch.nca]`.
  The update NCA of both mods is a full program NCA (no rights id), so it is passed as the "base".
