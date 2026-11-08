# NSO mods: SEGA Genesis, Game Boy / Color, Game Boy Advance

Research spec for adding a user ROM to a Nintendo Switch Online app with a LayeredFS overlay.
Status words: **PROVEN** = reproduced byte for byte by `scratch/nso/genesis-gb-gba/nsoconv.py verify`
(last run: all PASS, log in `verify.log`). **LIKELY** = strong evidence, no console run. **UNKNOWN** = not shown.
Nothing here was run on a console.

Title IDs: Genesis `0100B3C014BDA000` (update `…A800`), Game Boy `0100C62011050000` (`…0800`), GBA `010012F017576000` (`…6800`).
Overlay root on SD: `/atmosphere/contents/<base TitleID>/romfs/`.

## 1. Short answers

| Question | Answer |
|---|---|
| Is there a ".gen wrapper"? | **No. PROVEN.** `.gen`, `.md`, `.bin` are the same thing: a raw linear big-endian 68000 ROM. The "01 00 00 00 00 00 02 00" at offset 0 is the normal 68k reset vector (SP, PC) of that game. |
| Genesis ROM size rules | None needed. Any size from 128 KiB up works in the mods (131072 … 5242880 seen, plus two 15 MB hacks in Ckau). Odd sizes are fine (535218, 591064, 2384052 shipped). No padding, no power of 2. |
| SMD / byte order | The app wants **linear** ROM. SMD (512-byte header, `AA BB` at 0x08/0x09, 16 KiB interleaved blocks) must be de-interleaved: `smd_to_bin()` (self-test round trip PASS; no real SMD dump on this PC, so LIKELY). |
| GB / GBC / GBA file format | **Raw ROM, no wrapper.** Official GB/GBC: raw. Official GBA: an LZ4 frame of the (power-of-2 padded) raw ROM; **the app also accepts a plain raw GBA ROM** (code proof in 4.2). |
| Need to compress GBA? | **No.** Write the raw `.gba`. LZ4 is optional; we can recreate the official frame exactly (lz4hc level 9) if ever wanted. |
| Cover format | **Plain PNG** (official update, Ckau 280 titles, GB/GBA). `.xtx.z` is only the 2021 launch style of the base v0 app. **No dependency on the cover texture work.** |
| ROM id rule | Free text, unique inside one DB. Not a hash (0 of 308 match crc32/md5/sha1/size/header). See section 6. |

## 2. What must change to add ONE game

The mod is a LayeredFS overlay: every file under the overlay `romfs/` hides the same path in the real RomFS; every path
not present falls through to the real one. **A file in the overlay replaces the whole file.**

Zouloo's complete difference to the official update is exactly two files: `titles/lclassics.titlesdb` and
`bootapp/resources/strings/en/strings.lng` (PROVEN by file-by-file hash compare). Ckau adds Lua/sprites/`resources.json`
only for its own custom options (backgrounds, display mode), not for titles.

### 2.1 Minimal complete set, existing mod already installed

1. `romfs/titles/<id>/<id>.<ext>` raw ROM (`.gen`/`.md`/`.bin`, `.gb`/`.gbc`, `.gba`; the extension is only what the db says).
2. `romfs/titles/<id>/<id>.png` cover and `<id>-details.png` details screen.
3. Edit `romfs/titles/lclassics.titlesdb`: read it, add one entry under `titles`, write it back (section 5 for the exact byte form).

Nothing else. No index, no hash file, no sort/order edit (the app sorts by `sort_title`; entry position is irrelevant), no
count field. `strings.lng` is optional (section 2.3).

### 2.2 Clean install, no mod yet

Same three items, **but the db must be the full current official db plus the new entry**. The overlay db replaces the
official one, so a db that holds only the new entry shows only that game. Zouloo kept 14 official entries and dropped
46 official ones: those 46 titles are gone from its menu (PROVEN by diff: 60 official, 91 in mod, 46 only in official).

Hub requirement: read the current official `titles/lclassics.titlesdb` of the installed base+update (sizes 35 KB Genesis,
39 KB GB, 52 KB GBA). **UNKNOWN** how to read the patched RomFS of another title on console (libnx
`fsOpenDataStorageByProgramId`, `fsOpenFileSystemWithPatch`, or NCA parse with keys). This is the one real feasibility risk.
Fallbacks: (a) if the overlay folder already has a db, use it as the base; (b) ship per-update-version db snapshots with the Hub.
Always keep the db that is on the SD as the base for later additions; never rebuild from a stale copy after an NSO update.

### 2.3 Items checked and judged not needed

| Item | Verdict |
|---|---|
| `strings.lng` (`META_TITLE_COMMENT_<id with - as _>`) | **Optional, LIKELY.** It only holds the description text. Ckau ENG ships no `en` strings at all and 280 titles work. Official variant ids (`_pd`, `_pf`) have none. If written, start from the installed update's file: Zouloo's file is the v0 file plus 77 keys, so it silently **drops 45 strings the v917504 update added** and changes 10 (PROVEN). Do not copy that approach. |
| `bootapp/resources.json`, `init.json`, Lua | Not touched by any title add. Lua refers to the db by the fixed path `/titles/lclassics.titlesdb`. |
| `DBINFO` | Artifact of the "CaVE Database Manager" tool (`cave_version 1.5.1.0`), absent from official and Zouloo. Not needed. |
| Region filter | List mode default is NTSC: `machine_country` `us`/`jp` show, `eu` shows only after the user enables PAL ("North American, European, and Korean versions" in app settings). `hidden_countries:["kr"]` hides for Korean accounts. Use `"us"`. |
| Title count limit | Ckau: 280 entries in the db ("288, limit reached, more is unstable"). Official 60. Cause UNKNOWN (Ckau guesses memory). Keep Genesis under ~280. GB/GBA limit UNKNOWN (REPACK has 52). |
| BCAT enabled-list | The Lua has `enabledTitles/enabledGameCodes` filtering (`filterTitles`). All mods with ids not on any server list work, so it does not block custom ids. LIKELY harmless; UNKNOWN when online. |
| Missing cover | UNKNOWN. Lua prints "Invalid or incomplete title information: Skipping title" for bad entries and has an "Image loading error" path. Always write both PNGs (placeholder if the user has none). |

Cover sizes seen (any works, PNG RGB or RGBA): Genesis official 365x512 + 400x300; Ckau 341x512 + 320x240; GB/GBA 512x512 or 360x324 + 400x300.

## 3. Genesis

### 3.1 ROM bytes (PROVEN)

- `rom` in the db may be any path in the overlay; convention `/titles/<id>/<id>.<ext>`. The app does not care about the extension (Zouloo `.gen` and `.bin`, Ckau `.md`/`.bin`/`.gen`, official `.bin`).
- Same ROM in both mods under different ids and extensions is **SHA1 identical** (51 pairs). Proof run: `M-6048_e.gen` (Zouloo) -> `M-7587_e.md` (Ckau), `M-7017_e.gen` -> `M-6134_e.md`, `M-4314_e.gen` -> `M-8730_e.md`: `detect()` + write gives the other mod's file byte for byte.
- Reset vector (SP, PC) is sane in 348/354 files; the 6 others (e.g. `M-3980_e` Astebros 15 MB) have SP `0xE1000000` (not RAM) but still carry `SEGA` at 0x100 and PC 0x200: hacks/homebrew, not a format difference.
- `SEGA` at 0x100 in all 354 shipped ROMs with a header. 68k header checksum is valid over the whole file for 65/77 Zouloo and 183/231 Ckau files (a prefix or tail would break it; the rest are dumps with bad header words, normal).
- Header `ROM end` (0x1A4) may be smaller than the file (Ckau `M-4333_e` file 2 MiB, header 1 MiB): fine.
- 14 official launch ROMs (`M-5003`..`M-5052`) have no `SEGA` at 0x100: an official obfuscation. Custom mods never use it. Irrelevant, UNKNOWN format.
- Sizes: official max 4 MiB; SSF2 5 MiB works (Zouloo `M-4945_e`); Ckau 15 MB hacks are listed in its db (works: UNKNOWN).
- Detect rule (no extension needed): `SEGA` at 0x100 -> Genesis; or SMD header -> de-interleave then `SEGA` at 0x100.

### 3.2 Entry fields (Genesis)

Entry keys are stored in ASCII order. "Req" = present in 100% of official + mod entries.

| Field | Req | Value / rule |
|---|---|---|
| `code` | yes | equals the key and the folder name; also used by the app for the save path `/saves/<code>/cartridge.sram` (so reusing an id reuses the save) |
| `title`, `title_ko`, `title_zhHans`, `title_zhHant`, `sort_title` | yes | title text; custom mods use `title_ko = title`, `ー` for both zh, `sort_title = lower(title)` unless they want a different order ("batman 1", "phantasy star 4") |
| `publisher`, `sort_publisher`, `copyright` | yes | `sort_publisher = lower(publisher)` (371/371); copyright text e.g. `©SEGA` |
| `release_date`, `lcla6_release_date` | yes | free `YYYY-MM-DD`; official use `1992-12-??` |
| `cover`, `details_screen`, `rom` | yes | paths in the overlay |
| `machine_country` | yes | `us` default, `jp`, `eu` |
| `overscan` | yes | `[8,8]` (a few `[32,32]`, `[24,24]`) |
| `players_count`, `simultaneous` | yes | `1`/`2`.., `false`/`true` |
| `rewind_interval` | yes | `1.5` (some `3.0`) |
| `save_count` | yes | custom mods use `1`; official 0 for no-battery, 2-4 for battery. Meaning UNKNOWN; `1` is accepted for all |
| `volume` | yes | `66` default, tuned per game 46..100 |
| `z80c` | yes | `0.993` default (Z80 clock factor), 0.975..1.001 per game |
| optional | no | `display_version` (tag text), `emu_option` (e.g. `dev.mdpad.disable_6b`, `--68kdiv=5`), `hidden_countries`, `launch_caution` (key of a `strings.lng` text; 2 Ckau entries) |

Proof: `build_entry('genesis', ...)` reproduces **280/280** Ckau entries byte for byte from the user fields plus 8 overridable
defaults (`sort_title title_ko machine_country volume z80c save_count overscan rewind_interval`), and 173/280 with pure defaults.

## 4. Game Boy / Game Boy Color

### 4.1 ROM bytes (PROVEN)

- GB files are **raw dumps** (48 official + 4 REPACK-added): 52 files, header checksum valid for 51/52 and global checksum valid for 51/52; the same single file fails both: the REPACK-added `D-A8A1_e` Bugs Bunny Crazy Castle (measured, `probes/p19.py`), a bad dump rather than a wrapper. Sizes 32 KiB..2 MiB, no padding rule.
- Extension rule (official): CGB flag at 0x143 (`0x80`/`0xC0`) -> `platform:"CGB"`, `.gbc`; else `"DMG"`, `.gb`. 51/52 match; the exception is Pokemon Yellow (CGB-enhanced, REPACK chose DMG), so platform is a choice for `0x80` ROMs.
- Detect: Nintendo logo `CE ED 66 66` at 0x104.
- No independent external dump on this PC, so the "raw" proof is the checksum argument plus the REPACK's 4 plain-file entries.

### 4.2 Entry fields (GB)

Same common fields as 3.2 minus `machine_country overscan z80c title_zh*`, plus:

| Field | Value |
|---|---|
| `platform` | `"DMG"` or `"CGB"` |
| `CGB-Default`, `CGB-Nostalgic` | on every entry: `"None"` (also `Min/Low/Medium/High`) = colour-filter strength |
| `DMG_MGB-Default`, `DMG_MGB-Nostalgic` | only on `platform:"DMG"`: custom uses `"None"` / `"High"` |
| `volume` | `100` custom (official 83..100) |
| optional | `display_version`, `emu_option:"sh3"`, `enable_ir`, `compatible_titles`, `connect_guides`, `anothertitle_guides` (link-cable help images, Pokemon TCG etc.) |

Rebuilt byte for byte from raw ROM + metadata: the 4 REPACK entries `D-A8A1_e`, `D-EADB_e`, `D-FE6F_e`, `D-1D39_e` (PASS).

## 5. Game Boy Advance

### 5.1 ROM bytes (PROVEN, except console run)

- Official file = **LZ4 frame**: magic `04 22 4D 18`, 4 MiB independent blocks, block checksum off, content checksum on, content size stored, **liblz4 HC level 9**. Decompressed size is a power of two (4/8/16/32 MiB). Re-compressing the decompressed ROM with `lz4.frame.compress(compression_level=9, block_size=BLOCKSIZE_MAX4MB, content_checksum=True, block_linked=False, store_size=True)` (liblz4 1.9.4) gives the shipped file **byte for byte** for `A-6845_e`, `A-9444_e`, `A-1302_e`. HC output depends on the lz4 version; the check is only a curiosity because we do not need to compress.
- **Plain ROM is accepted (LIKELY).** Strongest evidence: the public REPACK ships 4 plain entries, all raw 8/8/32/16 MiB files (`A-AZCE_e` Megaman Zero, `A-AMTE_e` Metroid Fusion, `A-AE7E_e` Fire Emblem, `A-BZME_e` Minish Cap). Code evidence: in `main` the loader function around `0x1CD7E0` reads the file, calls `IsLz4Frame` (`0x1CE0A4`: `size>=5 && *(u32*)p == 0x184D2204`); if false (`tbz` at `0x1CD878`) it takes the raw path at `0x1CD90C`, which rounds the size up to the next power of two and copies the bytes. The magic check has a single caller, so it may be a general asset loader. Console launch not verified here.
- Detect: `24 FF AE 51` at 0x04 and `0x96` at 0xB2. Game code at 0xAC (4 chars).

### 5.2 Entry fields (GBA)

Common fields as 3.2 minus `machine_country overscan z80c`, plus:

| Field | Value |
|---|---|
| `fadein` | `[3,0]` on all 52 |
| `sram_file_size` | save file size in bytes. Official: 512 (EEPROM small), 8192 (EEPROM), 32768 (SRAM), 65536 (FLASH), 131072 (FLASH1M). The REPACK used **164096 for all 4 added games** (types SRAM, SRAM_F, EEPROM) and they were shipped, so 164096 works as a universal value (LIKELY). Save library names to scan for: `EEPROM_V`, `SRAM_V`, `SRAM_F_V`, `FLASH_V`/`FLASH512_V`, `FLASH1M_V`. |
| `volume` | `100` custom (official 90..113) |
| `simultaneous` | `false` |
| `rewind_interval` | `1.5` or `3.0` |

Converter default is 164096 (= REPACK). `gba_info()['official_sram']` returns the library-based value if you prefer the official one (effect UNKNOWN).
Rebuilt byte for byte from raw ROM + metadata: `A-AZCE_e`, `A-AMTE_e`, `A-AE7E_e`, `A-BZME_e` (PASS).

## 6. Ids

- Format `<L>-<4 chars>_<suffix>`. `L` = system: `M` Genesis, `C` GBC, `D` DMG, `A` GBA (NES/SNES/N64 are other agents').
- Suffix = region/language variant of the same game: `_e` English/US, `_p` Europe, `_k`, `_pd`, `_pf`, `_ps`, `_x`, `_y`, `_d`, `_f`, `_i`, `_s`. Custom mods use `_e` only. The same game in several variants shares the 4 chars.
- Official ids: 4 decimal digits (`M-5234_e`, `C-8914_p`, `A-6845_e`). Ckau/Zouloo: random 4 digits 3036..9994 (2 ids were picked in both mods independently: cross-mod collisions exist, so a Hub that merges into an existing mod must probe). No hash relation (tested crc32, crc32-high, md5, sha1, size, header checksum mod 10000: 0 hits).
- The REPACK used **letters/hex**: GB = `D-`/`C-` + the checksum **word stored at 0x14E** (hex, upper case), GBA = `A-` + game code (`AZCE`). Both loaded, so any 4 `[0-9A-Z]` chars work (LIKELY).
- Uniqueness inside one db is the only constraint (`code` is the dict key, folder name and save folder).
- Hub rule (our choice): Genesis `M-%04d_e` from `crc32 % 10000` with +1 probing against taken ids (same ROM -> same id, idempotent); GB from 0x14E word; GBA from game code. Collisions between two different GBA ROMs with the same game code: add a probe letter (not implemented, rare).

## 7. `lclassics.titlesdb` byte form (PROVEN on 5 dbs)

`{"titles":{"<id>":{...},...}}`; compact separators `,` and `:`; UTF-8 without BOM; no trailing newline; non-ASCII raw
(`©`, `ー`, `™`); entry keys in ASCII order; entries in any order; floats keep their form (`1.0`, `3.0`, `0.993`, int `3`).
`json.loads` -> `json.dumps(ensure_ascii=False, separators=(',',':'))` returns every db byte for byte (Zouloo, Ckau, official Genesis, GB and GBA REPACK).
In C++ the safest path is a text splice: find the final `}}` and insert `,"<id>":<entry>` before it, which avoids float re-printing.

## 8. Hub algorithm (on console)

1. Ensure the target app **and its update** are installed (base v0 Genesis RomFS has only `.xtx.z` covers and 14 ROMs; PNG covers exist only in the update RomFS, so PNG on a v0-only app is UNKNOWN; Ckau's install text also requires the update). Overlay path `/atmosphere/contents/<tid>/romfs`.
2. Read the file header: `SEGA`@0x100 (or SMD) -> Genesis; `CE ED 66 66`@0x104 -> GB/GBC; `24 FF AE 51`@4 + `96`@0xB2 -> GBA; LZ4 magic -> GBA already compressed (decompress or keep). If nothing matches (for example a `.bin` for N64/NES) -> ask the user which installed system.
3. Genesis: if SMD, de-interleave. Others: keep bytes.
4. Choose the id (section 6), make `titles/<id>/`, write ROM raw (GBA: raw, no LZ4), write the two PNGs.
5. Load the db from the overlay (else from the official RomFS, section 2.2), splice the entry (section 7), write the file atomically (temp + rename) because a broken db makes the app skip or crash.
6. Relaunch the NSO app (LayeredFS is read when the app opens its RomFS; no console reboot expected, UNKNOWN).

## 9. Reproduction

```
python -X utf8 -I scratch/nso/genesis-gb-gba/nsoconv.py verify      # every PASS line above
python -X utf8 -I scratch/nso/genesis-gb-gba/nsoconv.py add ROM OUTDIR --title T --publisher P --year Y --cover c.png
```
`add` is a research tool, not the Hub reference: it writes covers only when `--cover` is given, while the Hub must always write both PNGs.
`verify` needs the official RomFS unpacked in WSL (~1 GB, kept in `/tmp/gg`, safe to delete and rebuild) `/tmp/gg/{gen_upd,gb_base,gba_base}_romfs` (scripts `unpack.sh`, `rf.sh`, `probes/gen_rf.sh`; keys are read from `D:\git\dev\swuk_shop_nx\keys` and never copied) and the two Genesis mods in `D:\Downloads\switch`. GB/GBA official data comes from the REPACK base NSPs in `G:\Games\NSO` (the `…0800`/`…6800` update NSZ for GB is older than the REPACK and its RomFS did not unpack cleanly; use the REPACK).

## 10. Unknown / not done

- Console behaviour of: raw GBA ROM, GBA `sram_file_size` 164096 for EEPROM games, ids with letters, PNG without details image.
- How to read the official patched RomFS db on console (blocker for a clean install without shipped snapshots).
- Meaning of `save_count`, `z80c`, `volume` tuning; cause of the ~288 title limit; BCAT interplay.
- Genesis: ROMs above ~5 MiB, SMD from real dumps, the 14 obfuscated launch ROMs.
- Titles that need `emu_option` hacks (6-button pad, sound fixes) are per-game; the full list is in Ckau's `_Заметки.txt`.
- The `.xtx.z` cover is not needed for current app versions (PNG proven in the official update); the texture format itself belongs to the cover agent.
