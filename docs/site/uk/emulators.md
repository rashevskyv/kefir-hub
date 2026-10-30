# [[Emulators]]

Файли BIOS для емуляторів на карті пам'яті: Kefir Hub знаходить встановлені емулятори й завантажує системні файли, які потрібні кожному з них, у його власну теку.

**Де:** [[Tools]] → [[Game Tools]] → [[Emulators]]

<!-- shot: emulators-page | Emulators page with a Download BIOS for RetroArch row, its file count and folder -->

## Download BIOS
<!-- shot: emulators-ask-retroarch | The question for RetroArch: only for the installed cores, or all, each with the file count and size -->
<!-- draft
- the page shows one row [[Download BIOS for ]]<emulator> per emulator found on the card; the row's description is the number of files, their size and the folder the files go to
- emulators and their folders: RetroArch `/retroarch/cores/system/`; Tico `/tico/system/<system>/`; Flycast `/flycast/data/`; melonDS `/switch/melonds/`; DraStic `/switch/drastic/system/` (DS files renamed to `nds_bios_arm7.bin`, `nds_bios_arm9.bin`, `nds_firmware.bin`); NetherSX2 `/switch/nethersx2/bios/`; DuckStation `/switch/duckstation/bios/`
- an emulator counts as installed when its folder exists (RetroArch: `/retroarch/cores`; Tico: `/tico`); with none of them the page shows [[No emulator found on the SD card]] and the list of known emulators
- press **A** on a row: for RetroArch the question offers [[Only for the installed cores]] (the systems the cores in `/retroarch/cores` emulate) or [[All]] (every system of the list); for Tico [[Only for the systems it has]] (the folders under `/tico/system` and `/tico/roms`) or [[All]]; the other emulators ask once with [[Download]]; each choice shows the file count and size; [[Back]] cancels
- the files are downloaded one by one; the progress window shows the file number and name; a file that is already in place with the right size is skipped; each downloaded file is checked by its sha1 and only then moved into the folder
- at the end a message gives the counts: downloaded, [[already there]], [[failed]]; with a failure it also names the last file that failed, and a second run downloads only what is missing
- RetroArch needs its cores in `/retroarch/cores`; the arcade BIOS zips (`neogeo.zip`, `naomi.zip`, `awbios.zip`) stay zipped
- Tico folder names other than `psx` follow the HaulNX table and were not checked on a console yet
- the list is the libretro System.dat set as retrobios ships it: 63 systems, 510 files, about 650 MB in all; the biggest single system is PlayStation 2 (about 310 MB), which only [[All]] downloads
- source: github.com/Abdess/retrobios; the Hub ships no BIOS file and downloads only on your request; the files are console firmware owned by their makers (retrobios NOTICE: personal backup and interoperability)
- the list is built into the Hub and updates with a Hub release
-->

## ROMs from archive.org
<!-- shot: emulators-rom-item | A collection's file list: names, sizes, "on the card" on a downloaded one -->
<!-- draft
- below BIOS the page has the section [[ROMS]]: one row per collection you added (its title, the console, the archive.org item, the folder) and [[Add a collection]]
- a collection is an archive.org item you choose yourself; the Hub lists no sources; the files land in `/roms/<console>/`, the folder names Tico, HaulNX and RetroArch's scanner share (for example `/roms/snes`, `/roms/psx`)
- [[Add a collection]]: pick the console from the list (name and folder), then one of three ways: [[Popular on archive.org]] (the items archive.org lists for that system, most downloaded first, 50 rows), [[Search archive.org]] (the same list narrowed by the words you type, on the console or sent from a phone or a computer), or [[Enter an item id]] (the id or the `archive.org/details/...` address); a result row shows the title, the size, the number of downloads and the id; **A** on it reads the item and shows its title, the number of files and the size; [[Keep the collection]] saves it, [[Back]] drops it
- the catalogue is archive.org's own search: the Hub ships no list of items and no sources of its own
- an item whose files are all archive.org metadata (no content) is refused with [[The item has no files to download]]
- press **A** on a collection: the list of its files with sizes; a file already in the folder is marked [[on the card]]; **A** on a file downloads it; **X** marks a file, **Y** inverts the marks; **START**: [[Download the marked files]], [[Download all]], [[Remove the collection]] (the downloaded files stay)
- every download goes through the same window as BIOS: file number and name, md5 check, a file already there with the right size is skipped, the counts at the end
- archives are not extracted: RetroArch and Tico read zips; CD games in zips (psx, sega-cd, saturn, dc) must be extracted by hand for now
- the collections are kept in `/config/kefir/roms.txt`, one a line: console, item id, title, separated by tabs
- restricted items (an archive.org account needed) are not supported yet
-->

## Див. також
- [Ігри](games.md)
- [Homebrew](homebrew.md): встановлення RetroArch, Tico та інших емуляторів
