# Homebrew

The [[Homebrew]] tab lists the homebrew apps (`.nro` files) on your memory card. Launch them, sort them,
mark favorites, delete them, or add one to the HOME Menu as its own icon.

**Where:** the first tab on the main screen. From [[Tools]], press **L** or **−**.

<!-- shot: homebrew-grid | Homebrew tab in the default Grid layout, one app selected, path shown in the header -->

## Which apps are listed
- every `.nro` in `/switch/` and in its subfolders one level down (for example `/switch/ftpd/ftpd.nro`);
- every `.nro` in your own search folders, up to two levels down (see [Add a search folder](#add-a-search-folder)).

Files and folders whose name starts with a dot are skipped. The list updates by itself when you add or remove
an app in the [File Browser](file-browser.md).

- A subfolder of `/switch/` with several `.nro` files shows all of them, also when one of them is named after the folder (before, only `<folder>/<folder>.nro` was listed and the rest were hidden).
- Each card shows the app's file name (for example `vktest.nro`) on the second line, in place of the author, so builds of one app with the same title can be told apart; the hbmenu-style layout already shows file names.

## Launch an app
1. Select the app. The header shows its path and its position in the list.
2. Press **A** ([[Launch]]). Kefir Hub closes and the app starts.

When you exit the app, the Homebrew Menu (`/hbmenu.nro`) opens again.

- A second Kefir Hub file among the apps (when Kefir Hub is your Homebrew Menu): A asks [[Keep]] / [[Launch]] / [[Delete]]; [[Launch]] is the default, so a debug copy still starts.

## Sort and change the layout
1. Press **+** ([[Options]]).
2. Change [[Sort]], [[Order]], [[Layout]] or [[Show Hidden]]. The list updates at once.

<!-- shot: homebrew-options | Homebrew options panel: Sort, Order, Layout, Show Hidden, then THIS HOMEBREW section -->

| Option | What it does | Default |
|---|---|---|
| [[Sort]] | [[Updated]]: last launched or last changed first. [[Alphabetical]]: by name. [[Size]]: by file size. The `(Star)` variants put starred apps first | [[Alphabetical (Star)]] |
| [[Order]] | [[Descending]] or [[Ascending]]. Applies to [[Updated]] and [[Size]]; [[Alphabetical]] is always A to Z | [[Descending]] |
| [[Layout]] | [[List]]: one row per app with version and file size. [[Icon]]: six icons per row, the name shows for the selected one. [[Grid]]: three tiles per row with icon, name, author and version. [[HB Menu]]: one row of icons that you move through with **Left** / **Right**, with a large info panel for the selected app, like the classic Homebrew Menu | [[Grid]] |
| [[Show Hidden]] | Also list apps that are marked as hidden | Off |

## Mark favorites
Starred apps show a ★ before the name and stay at the top of the list.
Stars work only while [[Sort]] is one of the `(Star)` variants.

- Press **L3** on an app to [[Star]] or [[Unstar]] it.
- Or press **+** and choose [[Star]] or [[Unstar]]. This also works for several selected apps.

## Select several apps
1. Press **X** ([[Select]]) on each app. A mark appears on it and the cursor moves to the next app.
2. Press **Y** ([[Invert]]) to select everything that is not selected, and the other way round.
3. Press **+**. The panel shows [[SELECTED HOMEBREW]] with the count, and the actions apply to all of them.

Press **X** again on an app to unselect it.

Pressing **B** with apps selected clears the selection; the marks go and Kefir Hub stays open. Pressing **B** with nothing selected exits as before.

- When some apps are selected, **+** opens the options with [[SELECTED HOMEBREW]] at the top, above the sort and view options; with nothing selected the view options stay first and [[THIS HOMEBREW]] comes after them.

## Delete an app

- [[Delete]] on one app also looks at its own folder: when the app lives in `/switch/<folder>/` and no other `.nro` is inside, the question shows the folder and its size, with [[Only the app]] and [[App and folder]]; the folder of the running Kefir Hub is never offered.
- A HOME icon (forwarder) made by Kefir Hub that starts this app is deleted together with it, and the question says so ([[The HOME icon that starts it is deleted too.]])
- With several apps selected the old question stays: only the `.nro` files go.
1. Select the app, or several apps.
2. Press **+** and choose [[Delete]].
3. Confirm with [[Delete]].

Kefir Hub deletes the `.nro` file. The app's folder and its other files (settings, saves of the app) stay on the card;
remove them in the [File Browser](file-browser.md) if you want.

!!! warning
    Deleting cannot be undone.

## Change an app's name and icon
This rewrites the name and icon stored inside the `.nro` file, so every launcher shows the new ones.

1. Select one app (no other apps selected).
2. Press **+** and choose [[Edit name and icon]].
3. Change [[App Title]]. Press **A** on the icon, or tap it, to choose a new one:
   [[Local File]] (an image on the memory card) or [[SteamGridDB]] (search online).
4. Choose [[Save Changes]].

- [[SteamGridDB]] offers every static PNG/JPEG picture of the game: icons first, then covers of any size, heroes and logos; a non-square picture is cropped to its centre square (not stretched).
- In the list of found games **Y** ([[Search]]) opens the keyboard with the current text ([[Search SteamGridDB]]) and searches again.
- When no game matches, Kefir Hub says [[No game by this name on SteamGridDB]] and opens the keyboard at once; a game without pictures says [[No pictures for this game on SteamGridDB]].
- A picked picture opens in the crop editor (zoom, move; **A** [[Apply]]), so only a part of it can become the icon.
- Up to 10 pictures load at once; with more, 5 load first and [[Load more]] adds 5 each time (when 1-3 would be left they load with the batch before).
- Found games and downloaded pictures stay until Kefir Hub restarts: going back in does not download again.

<!-- shot: homebrew-edit-name-icon | Edit name and icon screen with title field and icon preview -->

## Folders
<!-- draft
- folders in Homebrew are virtual: they only group apps in this list; no file on the SD card moves (many apps look for their files at a fixed place under /switch)
- folders show first on the top level, with the number of apps in them; there are no folders inside folders
- one app can be in several folders; the top level ([[Homebrew]]) counts as a folder too, so an app can stay on the top level and be in a folder at the same time
- **A** ([[Open]]) goes into a folder; **B** goes back to the top level
- make a folder: **+** on the top level → [[New folder]], type the name
- on an app (or the apps selected with **X**/**Y**), **+** gives [[Copy to folder]] (the app stays where it is and shows in the chosen folder too) and [[Move to folder]] (the app leaves the folder on screen); the list of folders also has [[Homebrew]] (the top level) and [[New folder]]
- inside a folder, **+** on an app also gives [[Remove from this folder]]; an app that is left in no folder goes back to the top level
- **+** on a folder row gives the THIS FOLDER section: [[Open]], [[Rename]], [[Edit name and icon]], [[Remove icon]] (only when the folder has an icon), [[Delete]]
- [[Edit name and icon]] opens the same editor as for an app, without the forwarder options; without a picture the row shows small icons of the apps in the folder: one app nearly fills the picture, 2 to 4 apps in 2x2, 5 or more in 3x3 (the first 9); an empty folder shows a folder shape
- [[Delete]] on a folder asks first and removes only the folder: its apps stay in Homebrew
- the folders are kept in /config/kefir/homebrew_folders.txt, folder pictures in /config/kefir/homebrew_folders/
-->
<!-- shot: homebrew-folders | Homebrew tab, top level: folder rows "Demo Tools" (icons of its two apps) and "Emulators" (its own icon) before the apps -->
<!-- shot: homebrew-folder-options | Homebrew options panel on a folder row: THIS FOLDER section with Open, Rename, Edit name and icon, Remove icon, Delete -->
<!-- shot: homebrew-copy-to-folder | Copy to folder list for an app: Demo Tools, Emulators, New folder -->

## Send an app to another console
Press **+** and choose [[Send to another console]]. The Hub packs the app's whole folder (for example `/switch/<app>`) into one `.zip` and offers it on Wi-Fi. A `.nro` that lies directly in `/switch` is sent alone. On the other console, open [[Console Transfer]] → [[Receive files]]. See [Move files and folders](console-transfer.md#move-files-and-folders). The sending console shows a bar for each file while it goes.

## Add an app to the HOME Menu (forwarder)
A forwarder is a HOME Menu icon that starts the homebrew directly, without the Homebrew Menu.
It starts the app in title mode, with full memory.

!!! warning
    A forwarder is installed like a game. Installing anything other than official content can get the console
    banned when it goes online.

1. Select one app (no other apps selected).
2. Press **+** and choose [[Install Forwarder]] under [[FORWARDER]].
3. On sysMMC, Kefir Hub shows the ban warning first. Choose [[Install]] to go on. See [Ban warning on sysMMC](install/index.md#ban-warning).
4. If [[Ask every time]] is off, the forwarder is created at once with the default options.
   If it is on, the forwarder editor opens: change the title, author, version or icon, then choose [[Create Forwarder]].
5. Go back to the HOME Menu. The new icon is there.

| Option | What it does | Default |
|---|---|---|
| [[Ask every time]] | Open the forwarder editor for each forwarder instead of using the defaults | Off |
| [[Forwarder options]] | The defaults for new forwarders. Also in [[Settings]] → [[Homebrew]] → [[Forwarders]] | see below |
| [[Profile Selection]] | Ask which user profile to use when the forwarder starts | Off |
| [[Address Space]] | Memory layout given to the app: 39-bit, 36-bit, 32-bit or 32-bit (no alias). Change it only if the app needs it: 36-bit for some old apps, the 32-bit spaces for apps like Wine-NX or Box64 that need a fixed low image base | 39-bit |
| [[CPU Cores]] | 3 or 4 CPU cores for the app. The fourth core is shared with the system and can cause lag | 3 cores |
| [[Screenshots]] | Allow screenshots while the app runs | On |
| [[Video Capture]] | Allow video capture while the app runs. Needs [[Screenshots]] | On |
| [[svcDebug]] | Debug permission for the app | [[Automatic]] |

You can also make a forwarder for any `.nro` or for a game ROM from the [File Browser](file-browser.md#make-a-forwarder).

## Add a search folder
Kefir Hub can list apps from folders other than `/switch/`.

1. Go to [[Tools]] → [[Settings]] → [[Homebrew]] → [[Homebrew Search Paths]].
2. Choose [[Add folder]] and pick the folder on the memory card.

Or, in the [File Browser](file-browser.md), put the cursor on the folder (without selecting anything), press **+**,
choose [[Advanced]] and then [[Add to Homebrew Search Paths]].

To remove a search folder, select it in [[Homebrew Search Paths]], or use [[Delete Homebrew Search Paths]] in the
File Browser.

## More in the options panel
The [[Homebrew]] options panel also has the [[Install & Share]] entries ([[Web Server]], [[Mount MTP]],
[[PC Install (USB)]], [[Ownfoil]]) and the [[Settings]] entries ([[Install Title Mode forwarder]], [[Settings]]).
See [Installing games](install/index.md) and [Sharing](sharing.md).

## Problems
**An app is not in the list.** Check that the file ends in `.nro` and sits in `/switch/` or one folder below it.
Deeper folders need a [search folder](#add-a-search-folder). A name that starts with a dot is skipped.

**An app has no icon.** Kefir Hub shows only icons of 256 × 256 pixels. Other sizes show the default picture.

**"Kefir Updater" with the version "Removed" is in the list.** Kefir Updater was replaced by the [[Updater]] in
[[Tools]]. Press **A** on the entry, read the message and hold **A** on OK for five seconds. The entry disappears.

**An app is hidden and I cannot unhide it.** Turn on [[Show Hidden]]. Kefir Hub has no command to hide or unhide an
app. Apps are marked hidden in `/config/kefir/playlog.ini`: a `hidden` line under the app's path.
Delete that line to unhide the app.
