# recompile-store

Builds a second App Store source for Kefir Hub: recompiled PC ports (category `recompile`) taken from GitHub releases.
The output has the Homebrew App Store layout, so Kefir Hub shows these entries next to the fortheusers list and
offers Install / Update / Remove for them.

```
python build_store.py sources.json out/
```

| Output | Meaning |
|---|---|
| `out/repo.json` | package list; `version` = release tag |
| `out/zips/<name>.zip` | memory-card layout + `manifest.install` + `info.json` |
| `out/packages/<name>/icon.png` | icon, when `sources.json` names one |

Packages whose tag did not change are not downloaded again. Set `GITHUB_TOKEN` for more API calls.

## Publish

Push `out/` to a GitHub repository (for example `kefir-store`, branch `main`). The store address is then
`https://raw.githubusercontent.com/<owner>/kefir-store/main`. Kefir Hub has this address built in as
`SOURCE_KEFIR_RECOMPILES` (`sphaira/include/ui/menus/appstore_util.hpp`). Any other address goes into
`/config/kefir/appstore_sources.txt` on the console, one per line.

## sources.json

| Field | Required | Meaning |
|---|---|---|
| `name` | yes | package id, unique across all stores; also the cache and install folder name |
| `repo` | yes | GitHub `owner/name` |
| `title`, `author`, `description`, `details`, `license`, `url` | no | shown on the app page; defaults come from the repository |
| `asset` | no | regex over release asset names; default first `.nro` or `.zip` |
| `install_dir` | no | where a bare `.nro` or a zip without `switch/` at its root is put; default `/switch/<name>` |
| `binary` | no | path of the `.nro` on the card; default the first `.nro` in the zip |
| `icon` | no | direct link to a PNG |

Game assets (ROMs, data files) are never packaged. The description should tell the user where to put them.

Test: `python3 tests/test_recompile_store_contract.py`.
