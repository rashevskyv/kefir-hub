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

`sdmc/dumps/` holds the demo save backups (Kefir Hub archive format), written by
`python tools/docs/make_demo_backups.py`. Owner uids come from the Eden profiles (Pixel, Kotyk, Guest in Eden's
`nand/system/save/8000000000000010/su/avators/profiles.dat`): run it again after the Eden profiles change.
`sdmc/atmosphere/contents/0100DE0000010000/cheats/` holds the demo game's cheat files (Build IDs = `titles.json`
`build_id`).
