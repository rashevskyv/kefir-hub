# [[Software]]

Get homebrew apps: the Homebrew App Store, DBI builds, mod downloaders and direct downloads from a link.

**Where:** [[Tools]] → [[Software]]

<!-- shot: software-list | Software menu: Homebrew App Store, DBI, Ownfoil, UAModDownloader, ModCD, SimpleModDownloader, Custom Link -->

| Item | What it is |
|---|---|
| [[Homebrew App Store]] | Browse, install, update and remove homebrew apps. |
| [[DBI]] | Download DBI builds, its config and fan translations. |
| [[Ownfoil]] | Install titles from your own Ownfoil server. See [Install over the network](install/network.md). |
| [[UAModDownloader]] | Downloads the UAModDownloader app (Ukrainian mods). |
| [[ModCD]] | Downloads the ModCD app (ECLIPS graphic mods). |
| [[SimpleModDownloader]] | Downloads the SimpleModDownloader app (game mods from GameBanana). |
| [[Custom Link]] | Download a `.zip` or `.nro` from any direct link. |

All items need an internet connection.

## [[Homebrew App Store]]

The app list comes from two sources: the Homebrew App Store (fortheusers.org) and the Kefir store with recompiled
PC ports for Switch (category [[Recompiles]]). Both lists are shown together. An app from one source that is offline
stays visible from the last downloaded list.

**Where:** [[Tools]] → [[Software]] → [[Homebrew App Store]]

<!-- shot: software-appstore-grid | App Store grid with app tiles and status icons -->

A small icon on each tile shows the app's status: not installed, installed, update available, or found on the
memory card but not installed through the store.

### Install, update or remove an app

1. Select an app and press **A** ([[Info]]). The app page opens with the description, version, size and downloads.
2. The available actions depend on the app's status:
    - [[Install]] — the app is not installed.
    - [[Update]] — a newer version is available, or the app was found on the memory card without store data.
    - [[Launch]] — start the installed app.
    - [[Remove]] — delete the app's files ("Completely remove …?").
3. Use **D-pad** up/down to choose an action, then press **A**. [[Launch]] and [[Remove]] ask for confirmation.

<!-- shot: software-appstore-entry | App page: icon, description, version/installed/updated lines, Install button -->

On the app page:

| Button | Action |
|---|---|
| **L** | [[Changelog]] / [[Details]] — switch between the changelog and the description |
| **ZL** | [[Files]] — list the files the app installs |
| **+** | [[Options]]: [[More by Author]], [[Leave Feedback]], [[Visit Website]] |
| **B** | [[Back]] |

[[Visit Website]] is shown only when the app has a website and Kefir Hub runs as a full application (not from the Album).

### Find an app

Press **+** ([[AppStore Options]]) in the list.

| Option | What it does | Default |
|---|---|---|
| [[Filter]] | Show one category: [[All]], [[Games]], [[Emulators]], [[Tools]], [[Advanced]], [[Themes]], [[Legacy]], [[Misc]], [[Recompiles]]. | [[All]] |
| [[Sort]] | [[Updated]], [[Downloads]], [[Size]] or [[Alphabetical]]. | [[Updated]] |
| [[Order]] | [[Descending]] or [[Ascending]]. | [[Descending]] |
| [[Layout]] | [[Icon]], [[Grid]] or [[HB Menu]]. | [[Grid]] |
| [[Search]] | Search by name or keyword. Press **B** to leave the search results. | — |

[[More by Author]] on an app page shows all apps by the same author. Press **B** to return to the full list.

### Add your own store

Kefir Hub reads extra stores from the file `/config/kefir/appstore_sources.txt` on the memory card. Write one store
address per line. A line that starts with `#` is a comment.

```
# my stores
https://example.org/switch-store
```

A store is a folder on a web server with the Homebrew App Store layout: `repo.json`, `packages/<name>/icon.png` and
`zips/<name>.zip`. An entry in `repo.json` may also name its own `download` (direct link to the zip) and `icon`
(direct link to the icon). The tool `tools/recompile-store` in the Kefir Hub repository builds such a store from
GitHub releases. Restart Kefir Hub after you change the file.

## [[DBI]]

DBI is a separate installer app. This menu only downloads DBI and its files; installing games with DBI is described in
[Install over USB](install/usb.md).

**Where:** [[Tools]] → [[Software]] → [[DBI]]

| Item | What it does |
|---|---|
| [[Download DBI translations list]] / [[Update DBI translations list]] | Downloads the list of DBI fan translations. The translations then appear below the line. |
| [[Russian latest DBI]] | Downloads the latest Russian DBI build to `/switch/DBI/DBI.nro`. |
| [[Reset DBI config]] | Replaces your DBI config (`/switch/DBI/dbi.config`) with the default Kefir config. Hold **A** to confirm. |
| *a translation* | Downloads DBI with that fan translation to `/switch/DBI/`. |

!!! warning
    [[Russian latest DBI]] and every translation replace `/switch/DBI/DBI.nro`. [[Reset DBI config]] overwrites your DBI settings.

## Mod downloaders

[[UAModDownloader]], [[ModCD]] and [[SimpleModDownloader]] each download the latest version of that app from GitHub.

1. Select the item and press **A**.
2. When the download finishes, "Done" appears. The app is saved to `/switch/<name>/<name>.nro` and shows up in the homebrew list.
3. Start the app from the homebrew list ([Homebrew](homebrew.md)) to download mods.

Selecting the item again updates the app to the latest version.

## Download from a link

[[Custom Link]] downloads a `.zip` archive or an `.nro` app from a direct link.

1. Select [[Custom Link]] and press **A**.
2. Choose how to enter the link: [[Manual (Keyboard)]] or [[From Phone / PC]].
    - With [[From Phone / PC]], scan the QR code with your phone, or open the shown address on a PC on the same network, and send the link from there.
3. Enter the link. It must start with `http` and end with `.zip` or `.nro`.
4. If the file is larger than 20 MB, Kefir Hub warns that large files may cause issues. Choose [[Force]] to download anyway.
5. What happens next depends on the file:
    - **`.nro`:** it is saved to `/switch/<name>/<name>.nro`. Kefir Hub offers to launch it.
    - **`.zip`:** it is saved to `/downloads/`, and the [[Extract Options]] window opens.

<!-- shot: software-direct-link | Direct Download input: choice between Manual (Keyboard) and From Phone / PC -->

In [[Extract Options]]:

- If the archive contains one app, choose [[Install the app to /switch]].
- Otherwise tick the files you want (**X** selects, **Y** inverts) and choose where to put them:
  [[Extract files to /downloads]], a new folder named after the archive, or [[Extract files to...]] to pick a folder.
- After extracting, Kefir Hub asks whether to delete the downloaded `.zip`, and may offer to launch the app or open the folder in the [File Browser](file-browser.md).

<!-- shot: software-extract-options | Extract Options window with a file tree and the extract choices -->

## Problems

**"This isn't a direct link to a .zip or .nro file."** The link points to a web page, not to the file. Choose [[Edit URL]] and fix it.

**"Couldn't download that file."** The address is wrong or the server did not respond. Choose [[Edit URL]] and try again.

**"Failed to download application".** The App Store server could not be reached. Check the internet connection and try again.

**An app shows as installed but its files are gone.** For apps without a single launchable file (for example sysmodules), the App Store trusts its own install record. Choose [[Remove]] (it works even when the files are already gone), then [[Install]] again.
