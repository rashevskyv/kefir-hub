# Screenshot fixtures

`sdmc/` here is copied over the Eden emulated microSD (`E:\Switch\Eden\user\sdmc`) by
`tools/docs/shoot.ps1` before every run, so recipes always see the same files.

Put here only what a screen needs to look real: dummy folders and files for the file browser,
copies of `.nro` as other homebrew, sample cheats, a theme, a small text or image file.
No keys, firmware, games, saves of real titles or anything personal — this folder is in git.
Keep `config/kefir/config.ini` out of it: `shoot.ps1` sets the language and the demo scene itself.

`sdmc/config/kefir/demo/` is the content of the DOCS_DEMO build (plan Phase S): `titles.json` (six fictional
meme games: names per language, sizes, updates, add-ons, saves per Eden profile) and `icons/` (original 256x256
covers, generated). Release builds ignore this folder.
