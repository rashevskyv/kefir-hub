# [[Emulators]]

BIOS files for the emulators on your microSD card: Kefir Hub finds the installed emulators and downloads the system files each of them needs into its own folder.

**Where:** [[Tools]] → [[Game Tools]] → [[Emulators]]

<!-- shot: emulators-page | Emulators page with a Download BIOS for RetroArch row, its file count and folder -->

## Download BIOS
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

## See also
- [Games](games.md)
- [Homebrew](homebrew.md): installing RetroArch, Tico and the other emulators
