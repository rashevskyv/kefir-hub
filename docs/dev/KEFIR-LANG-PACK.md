# kefir_lang.json — game language for translation packs

A repacked update + translation NSP/NSZ can tell Kefir Hub which language the game must start in (for example
`en-US`, when the translation replaces the English text). Hub reads it while installing and, once the install
succeeded, writes Atmosphère's per-game override:

```ini
; /atmosphere/contents/<base title id>/config.ini — other keys in the file are kept
[override_config]
override_language=en-US
```

## Where

One extra file named `kefir_lang.json` in the **root of the NSP's PFS0** (next to the `.nca`/`.tik`/`.cert`
entries), not inside an NCA. Other installers ignore unknown root entries. The NSZ compressor must copy it as is.

## Format

```json
{"format": 1, "title_id": "0100AC300919A000", "language": "en-US"}
```

| Field | Meaning |
|---|---|
| `format` | `1`. Hub ignores files with a format it does not know. |
| `title_id` | Base application id (16 hex digits, ends in `000`), not the update id. Must match a title in the NSP. |
| `language` | One of `en-US en-GB ja fr de es-419 es it nl fr-CA pt ru ko zh-Hant zh-Hans pt-BR` (Atmosphère codes). |

Any other field (e.g. `"note"`) is ignored. The file must be under 4 KiB.

## Notes

- The game still has to contain that language: the system picks the closest one the game supports.
- The user can change or remove it later: game details → **+** → Force language.
- Source in Hub: `sphaira/include/forced_language.hpp`, `sphaira/source/forced_language.cpp`, hook in
  `sphaira/source/yati/yati.cpp` (`ReadLanguagePack` / `ApplyLanguagePack`).
