# Translation repack metadata and forced language — research and design

Status: research only. No source changed. Labels: **VERIFIED** (read in code or a source this session), **LIKELY** (strong evidence, not proven), **UNKNOWN** (needs a test).

## 0. Key finding: half of this already exists

| Fact | Label |
|---|---|
| Hub reads `kefir_lang.json` from the PFS0 root on install (stream and random-access paths) and writes Atmosphere `override_language` after the title is registered. Shipped in v0.13.984. | VERIFIED (`sphaira/source/yati/yati.cpp` `ReadLanguagePack`, `ApplyLanguagePack`; `sphaira/source/forced_language.cpp`) |
| The CHANGELOG says `switch: pending` for v0.13.984. No hardware test is recorded. | VERIFIED (`docs/dev/CHANGELOG.md`) |
| Hub has a manual "Force language" in game details (`game_details_ops.cpp` `ShowForceLanguage`). | VERIFIED |
| SWUK Shop NX already writes `kefir_lang.json` into the NSP stage dir, with extra keys (`translation.lang/source/post_id/url/author/modified/game_version/ported_to`). | VERIFIED (`swuk_shop_nx/core/repacker_engine.py` ~line 616, `core/swuk_pipeline.py` `_pack_info`) |
| Hub parses only `format`, `title_id`, `language`. It drops every other key. It shows no translation info anywhere. | VERIFIED (`ParsePack` in `forced_language.cpp`) |
| `docs/dev/KEFIR-LANG-PACK.md` documents format 1. | VERIFIED |

So the work is: (A) read and keep the extra keys, (B) show them, (C) hardware-test the language override.

## 1. Where the metadata lives

### Options compared

| Option | Survives NSZ | Ignored safely by DBI/Tinfoil/Awoo | Hub reads without NCA extraction | Verdict |
|---|---|---|---|---|
| Extra file in PFS0 root (`kefir_lang.json`) | Yes, copied byte for byte (nsz copies every non-NCA entry in `SolidCompressor.py`, VERIFIED from source) | Likely yes (they look up entries by extension; ticket/cert/xml siblings already exist) — LIKELY, not tested per installer | Yes: PFS0 header + one small read (`Nsp::GetCollections`, VERIFIED) | **Recommended** |
| Custom field in CNMT | No safe place: the CNMT is inside a signed-by-hash NCA; the extended header has fixed fields | Risky: a changed CNMT NCA changes the content id hash chain | No: needs NCA decrypt | Reject |
| File inside update RomFS | Yes | Yes (a RomFS file is just game data) | No: needs NCA decrypt and RomFS walk; Hub cannot do this cheaply | Reject |
| Sidecar file next to NSP | n/a | Yes | Yes, but lost when the user moves, renames or uploads the NSP via MTP/FTP/USB | Reject as the only copy |

### Facts about Hub's NSP handling

| Fact | Label |
|---|---|
| `Nsp::GetCollections` returns every PFS0 entry as `{name, offset, size}`; it does not care about the name. | VERIFIED (`yati/container/nsp.cpp`) |
| `InstallInternal` (random access) and the stream path only act on `.cnmt.nca/.ncz`, `.nca/.ncz`, `.tik`, `.cert` and `kefir_lang.json`. All other entries are ignored. | VERIFIED (`yati.cpp` ~lines 310 and 400-425) |
| In the stream path, entries are sorted by offset and read in order. The JSON can be read only when its bytes have arrived. | VERIFIED (sorter in `yati.cpp`) |
| `yati_metadata.cpp` handles NCA/CNMT/control data, not JSON/XML sidecars. | VERIFIED (grep) |
| hacpack file order inside the PFS0 is unknown. For stream installs the JSON should come first. | UNKNOWN — check `hactool -t pfs0` listing of a SWUK output; if not first, SWUK should write the PFS0 itself or rename so it sorts first. |

### Recommended format: keep `kefir_lang.json`, format 1, add an optional `translation` object

Do not create a new file. SWUK already writes this shape. Format 1 readers ignore unknown keys, so no bump is needed. Bump `format` to 2 only for a breaking change.

```json
{
  "format": 1,
  "title_id": "0100AC300919A000",
  "language": "en-US",
  "translation": {
    "name": "Ukrainian translation",
    "description": "Full text, UI and fonts.",
    "lang": "uk",
    "authors": ["Name A", "Name B"],
    "source": "swuk.com.ua",
    "url": "https://swuk.com.ua/...",
    "version": 3,
    "modified": "2026-10-30",
    "repacked": "2026-11-08",
    "game_version": "1.2.0",
    "ported_to": "1.3.0"
  }
}
```

| Key | Type | Required | Meaning |
|---|---|---|---|
| `format` | int | yes | Format version. Hub accepts `1`. |
| `title_id` | string, 16 hex | yes | Base application id (ends `000`). Already used. |
| `language` | string | yes | Atmosphere code the game must run in. Already used. |
| `translation.name` | string | no | Name for the card. |
| `translation.description` | string | no | One or two lines. Hub shows max ~200 characters. |
| `translation.lang` | string | no | Target language of the translation (`uk`). Display only. |
| `translation.authors` | array of string | no | Credits. SWUK now writes a single `author` string — Hub should accept both. |
| `translation.source`, `url` | string | no | Origin. |
| `translation.version` | int | no | Monotonic translator version. Best update test. |
| `translation.modified` | string `YYYY-MM-DD` | no | Translation date. Fallback update test when `version` is absent. |
| `translation.repacked` | string `YYYY-MM-DD` | no | Repack creation date. Not used for comparison. |
| `translation.game_version` | string | no | Game version the translation was built for. |

Update rule (design): same `title_id`, newer `version`, or (no version) later `modified` means "newer". Dates sort as text in ISO form. Limit: file under 4 KiB (existing limit in `ReadLanguagePack`).

Persistence (design): at install, Hub writes the accepted JSON to `/atmosphere/contents/<title_id>/kefir_translation.json`. Reason: `system_cleanup.cpp` already deletes `/atmosphere/contents/<tid>` folders of uninstalled games, so the record cleans itself (VERIFIED in code). Atmosphere ignoring an unknown file in that folder: LIKELY (the folder already holds `exefs`, `romfs`, `cheats`, `flags`, `config.ini`); test on console.

## 2. Forcing the game language on a Switch

| Mechanism | What it is | Evidence | Needs console test? |
|---|---|---|---|
| (a) Atmosphere per-title `override_language` | `/atmosphere/contents/<TID>/config.ini`, `[override_config]`, `override_language=<code>`. Served to the title by `ams_mitm` (the `set` service is mitm'd). | Atmosphere 0.8.10 changelog "per-game language redirection" (search results); `setmitm_module.cpp` registers `SetMitmService` on `set` and `set:sys` (VERIFIED by fetch); sphaira PR #405 and its merge describe the same path and key (VERIFIED by fetch) | PR #405 states no hardware test. Forum posts show it works when the section header is correct. **Needs one hardware test with a real game.** |
| (b) Launch hook / sysmodule / SaltyNX / overlay | Not needed. (a) is the Atmosphere way. | I found no source for another per-game mechanism. | UNKNOWN, not pursued |
| (c) Edit NACP supported-language flags / title language in control data | `control_patch.cpp` already rewrites control NCAs for install patches. Changing `supported_language_flag` makes the system pick another language, but changes how HOME Menu shows the game name and icon too. | Hub code (VERIFIED it can patch control data). Effect on game language selection: LIKELY, not tested. | Yes. Worse than (a): alters installed data, needs rewrite on every update. Use only as a fallback. |
| (d) Per-save or per-account setting | The language is system-wide in `set`. No per-account language that games use. | No source found. | UNKNOWN |

Recommendation: (a) only. It is already coded.

Facts and limits:
- The value is Atmosphere's code list. Hub's `LANGS[16]` table matches the NACP order (VERIFIED in `forced_language.hpp`). The 0.8.10 changelog also lists `zh-CN`/`zh-TW`; Hub does not use them (fine).
- It needs Atmosphere. On stock firmware it has no effect (PR #405 text, VERIFIED by fetch).
- No reboot: applied at each launch (PR #405 text; LIKELY).
- The game must contain that language. If it does not, the system picks the closest one (Hub doc says this; the exact behaviour is UNKNOWN). Hub's manual dialog already lists only languages in `supported_language_flag`.
- The `config.ini` is edited with minIni and keeps other keys (VERIFIED).
- Important for translations: the translation replaces an existing language file (for example Italian). The forced code must be that language (or the one the game loads the replaced files for). SWUK currently forces `en-US` because its translations replace English (VERIFIED in `_pack_info`). If a game maps English to British English files, `en-GB` may be needed — UNKNOWN per game.
- Hub does not check that `language` is in the game's `supported_language_flag` at install. Design: warn in the log and still apply.

## 3. Hub code changes

| Step | File / function | Change | Size |
|---|---|---|---|
| Parse | `sphaira/include/forced_language.hpp` `PackInfo`; `source/forced_language.cpp` `ParsePack` | Add `TranslationInfo` (name, description, authors, version, modified, repacked, url). Accept `author` string or `authors` array. Cap string lengths. Pure code, host-testable. | ~80 lines |
| Keep | `yati/yati.cpp` `ApplyLanguagePack` | After `forced_language::Set`, write `kefir_translation.json` to the contents folder. Both install paths already call this (lines ~371, ~492). | ~30 lines |
| Read helper | new free function in `forced_language.cpp`: `GetTranslation(app_id)`, `IsNewer(a, b)` | Read the saved file; compare versions. | ~50 lines |
| Show on card | `source/ui/menus/game/game_details.cpp` (stat list near line 224), `game_details_ops.cpp` `LoadGame` (near line 141) | New "Translation" row: name, authors, date; read in `LoadGame`. Add i18n keys to `en.json` first (source of truth). | ~60 lines |
| Show at install confirm | `App::ConfirmInstall(std::function<void()>)` in `app_settings.cpp` has no file data. The pre-read must happen in the caller, for example `filebrowser_options.cpp:176` (`InstallFiles`). | For random-access sources only: read the PFS0 root JSON before the confirm and add one text line. Streams (MTP/FTP/USB) cannot be read before the install starts. | ~80 lines, riskiest part |
| Update check | game card | Compare the saved record with a new pack seen during install (reinstall of the same title): show "newer/older/same" in the notification. A web or catalog source of "latest version" is a separate feature, not in scope. | ~40 lines |
| Tests | `tests/test_forced_language.cpp` (extend), `tests/test_pfs0_nsp.cpp` | Parse cases: full, minimal, bad types, long strings, `author` vs `authors`, version compare. | ~80 lines |

Total about 350-450 lines plus i18n keys. `yati.cpp` is a hot path; keep the change to the one `ApplyLanguagePack` call.

Risks:
- The stream path reads JSON late; the confirm dialog cannot show it for MTP/FTP. Mitigation: show it on the card after install.
- A wrong `title_id` (update id instead of base id) silently skips the override. Hub logs `[LANG] pack title ... matches no installed meta`. Add a notification.
- `ApplyLanguagePack` failure is silent for the user. Add a notification on failure.
- hacpack file order for stream installs (see section 1).
- A language the game lacks.
- Unknown JSON content shown on screen: cap length, strip control characters, i18n not applied to author text.
- Files over 600 lines rule: `yati.cpp` and `game_details_ops.cpp` — check line count before adding.

## 4. Phased plan

| Phase | Work | Needs console? |
|---|---|---|
| P0 (console, 10 min) | Hardware test of the already shipped feature: install a SWUK NSP with `kefir_lang.json`, check `/atmosphere/contents/<tid>/config.ini`, launch, confirm the game language. Also test an NSZ output. Test `en-US` vs `en-GB` for one game. Check the `.json` ordering with `hactool -t pfs0`. | Yes |
| P1 (host) | Extend `TranslationInfo` parse, version compare, host tests. | No |
| P2 (host + build checkpoint) | Persist `kefir_translation.json`; card row; i18n keys; docs per `update-docs` skill. | Build in WSL; display needs console |
| P3 (host) | Confirm-dialog line for random-access installs. | Display check on console |
| P4 (console) | Test other installers (DBI, Tinfoil, Awoo) with an NSP that has the extra file. | Yes |
| P5 (later) | Update check against a catalog. | Needs a data source |

### What SWUK Shop NX must write (writer side)

File `kefir_lang.json`, UTF-8, under 4 KiB, in the PFS0 root, preferably as the first entry. Fields: exactly the table in section 1. Change from today: write `translation.authors` (array), `translation.name`, `translation.version` (int, increase on each new translation release), `translation.modified` and `translation.repacked` as `YYYY-MM-DD`. Keep `format: 1`. Keep `title_id` as the base id in upper case. Set `language` to the code of the language the translation replaced. The NSZ step needs no change (nsz copies the file; checked in the library source, run P0 to confirm with your nsz version).
