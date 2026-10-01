# CHANGELOG

Newest first. One heading per version; add 1-3 lines per delivery (see AGENTS.md).
Entries without a detail line are commit titles only; their verification state was not recorded.

## unreleased
- chore: context diet — AGENTS.md/CLAUDE.md/plan.md, CHANGELOG from git history, junk untracked, one test-build skill, graphify hook.
- docs: README 435 -> 109 lines; its feature detail moved into docs/wiki (new Interface-and-Navigation.md).
- test: drop C++ source-text assertions from Python contracts (9 files deleted, static functions removed from 21); i18n JSON checks kept.
- docs: plan 2.2 table — every remaining Python test mapped to its C++ mirror (1 PURE part, 1 DROP, rest SEAM/KEEP).
- test: tests/run.sh honours `// LINK: <libnx-free .cpp>` lines in host C++ tests.
- test: DBI root-marker and save-entry matrix moved from Python model to tests/test_dbi_root_marker.cpp (real path_util.hpp).
- test: drop test_shutdown_lifecycle_contract.py (Python MockSystem of exit interleavings; mirrors no callable C++, hardware-only).
- docs: docs/dev/ARCHITECTURE.md — directory map, 19 Menu structs, thread map, god-node rules, build/test, i18n.
- docs: wiki Developer-Guide.md (architecture, changelog, build, host tests); README feature headings verified in wiki.
- docs: tools/i18n-translate/README.md (translate, add a language, parity check); module_catalog README checked (its test_catalog has 1 stale source-text test).
- test: `tests/run.sh --quick` runs host C++ tests + dead-symbol guard only (~20 s).
- chore: tools/dev/check.ps1 runs `tests/run.sh --quick` (or `-Full`) in WSL from Windows (not executed here: no PowerShell/WSL in this environment).

## v0.13.924 — atomic account-daemon flag
account_link: `g_daemons_terminated` -> std::atomic<bool> (written on ProgressBox workers, consumed on the main thread); `g_launch_link_prompted` annotated main-thread only. host tests: pass · nro: not built · switch: pending
## v0.13.923 — atomic network cache flags
net.cpp: `g_cache_value`/`g_cache_valid` -> std::atomic<bool> (TryConnect workers clear them while the UI reads); `g_request_open`, `g_cache_ts` annotated. host tests: pass · nro: not built · switch: pending
## v0.13.922 — keep MTP active during USB recovery
Recovery flag set before local cancel; no haze::Exit during controlled USB detach; MTP restarts if enabled but stopped. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.921 — fix libhaze cancel patch compilation and verify build
Fixed duplicate R_TRY_CATCH from patch_libhaze_cancel.cmake in ptp_responder.cpp; patch idempotent. host tests: py contracts pass · nro: built · switch: pending
## v0.13.920 — abort cancelled MTP transfer promptly
Local MTP install cancel stops reading the file without draining to EOT; libhaze re-inits broken transport. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.919 — select language from bundled translations
Language list from bundled JSON `__language_name`; language stored as text code in config.ini, numeric values migrated. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.918 — drain MTP transfers after install cancellation
After cancel libhaze drains the transfer, then sends TransactionCanceled; libhaze patches split cancel/cleanup/usb. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.917 — avoid MTP restart after install cancellation
User-confirmed cancel ends the PTP transaction; MTP stays active for the next transfer. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.916 — fix compilation errors and verify build
Dropped `<haze/results.hpp>` from haze proxies (R_* macro clash); added `haze::ResultCancelled()`. host tests: py contracts pass · nro: built · switch: pending
## v0.13.915 — add first-run language choice and 26 localizations
First-run language picker, 26 locales, ru.json removed; i18n completeness contract. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.914 — fix MTP install cancellation and shutdown
Cancel-install dialog takes input over active session; exit joins worker before stopping MTP. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.913 — route MTP device root uploads
MTP writes to device root: packages go to Install proxy, other files to microSD. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.912 — fix compilation errors and verify build
Fixed libhaze `operator==` for ams::Result and `transfer_success` scope; patch dedup on reconfigure. host tests: py contracts pass · nro: built · switch: pending
## v0.13.911 — fix MTP transfer cancellation
B -> Yes cancels the active MTP file to microSD; partial file deleted; URB 0x748C treated as abort. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.910 — default forwarders to 39-bit with optional fourth core
Forwarders default to 39-bit address space and 3 CPU cores; 4 cores optional with warning. host tests: i18n JSON checked · nro: not built · switch: pending
## v0.13.909 — expose current app forwarder in plus menu
"+" menu gets Install Title Mode forwarder above Settings. host tests: not run · nro: not built · switch: pending
## v0.13.908 — fix MTP SD copy routing and minimized progress
MTP copies packages to SD (no root interception); collapsed install bar shows overall progress. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.907 — fix CollectBackups namespace collision and verify build
Fixed `path` shadowing in CollectBackups (save_backup_pub.cpp). host tests: py contracts pass · nro: built · switch: pending
## v0.13.906 — target hbmenu for Kefir Hub forwarders
Forwarder target is /hbmenu.nro when HB Menu replacement is on, else current NRO path. host tests: not run · nro: not built · switch: pending
## v0.13.905 — write save backups to selected folder
Save dumps go to configurable backup_root (default /dumps). host tests: py contracts pass · nro: not built · switch: pending
## v0.13.904 — finish MTP progress banner lifecycle
MTP progress box closes after the last file; timer reset per file. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.903 — fix MTP transfer UI lifecycle and build warnings
MTP banner lifecycle via g_mtp_transfer_active/seq under g_mtp_ui_mutex. host tests: py contracts pass · nro: not recorded · switch: pending
## v0.13.902 — update README and wiki documentation for release
README and docs/wiki synced to release. host tests: n/a (docs) · nro: not recorded · switch: pending
## v0.13.901 — update USB queue during package transfer
Live SPHQ queue polled after a FileRange read. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.900 — fix compilation errors and verify build
Ownfoil compile fixes. host tests: py contracts pass · nro: built · switch: pending
## v0.13.899 — synchronize live USB queue
DBI Backend Qt sends ordered SPHQ list with revision; ReviewQueue applies it. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.898 — recognize empty SPHQ queue
Empty SPHQ reply handled. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.897 — restore initial DBI USB list request
Initial DBI List/SPHQ request after Awoo listen. host tests: not recorded · nro: not built · switch: pending
## v0.13.896 — add Ownfoil client
## v0.13.895 — install packages recursively from folders
## v0.13.894 — group game backups and restore all
## v0.13.893 — return from nested file browser with minus
## v0.13.892 — fix batch restore target call
## v0.13.891 — simplify backup restore menus
## v0.13.890 — fix HB Menu title scroll access
## v0.13.889 — improve backup titles and icon fallback
## v0.13.888 — remove duplicate icons save layout
## v0.13.887 — wrap save backup navigation past section header
## v0.13.886 — restore backup without save slot prompt
## v0.13.885 — remove empty rows between backup sources
## v0.13.884 — align backup dividers and title labels
## v0.13.883 — remove first backup grid gap
## v0.13.882 — group backup sources and restore folder backups
Backups grouped by origin (Kefir Hub, DBI, JKSV, Checkpoint, Other). host tests: not recorded · nro: built · switch: pending
## v0.13.881 — restore saves for uninstalled games
Restore a save without the game installed, using ZIP metadata. host tests: py contracts pass · nro: not built · switch: pending
## v0.13.880 — show DBI save backups
DBI ZIP backups listed (payload_count carried with metadata). host tests: not run · nro: not built · switch: pending
## v0.13.879 — close remaining file-size audit
Remaining 25 files over 600 lines split by responsibility. host tests: pass · nro: built · switch: pending
## v0.13.878 — split installer, menus and contract suites
yati.cpp, menus and tests split. host tests: pass · nro: built · switch: pending
## v0.13.877 — split transfer pipeline
threaded_file_transfer.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.876 — split App implementation by responsibility
app.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.875 — split web responsibilities
web.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.874 — split save operations
save_menu_ops.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.873 — split save menu responsibilities
save_menu.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.872 — split save path responsibilities
save_paths.cpp split. host tests: py contracts pass · nro: built · switch: pending
## v0.13.871 — remove dead code and restore clean build
Dead code removed; accumulated compile blockers fixed. host tests: py contracts pass · nro: built · switch: pending
## v0.13.870 — add Game Tools save-slot manager
## v0.13.869 — create and grow save slots safely
## v0.13.868 — admit DBI backups through exact restore route
## v0.13.867 — fix compilation errors and verify build
## v0.13.866 — fail closed on save deletion errors
## v0.13.865 — expose all save types over read-only MTP
## v0.13.864 — make MTP Saves read-only
## v0.13.863 — make backup freshness conservative
## v0.13.862 — distinguish backup rank provenance
## v0.13.861 — retain exact backup library members
## v0.13.860 — check ordinary SD backup publication
## v0.13.859 — reject mismatched save identity before backup export
## v0.13.858 — refuse unsafe RAW save container restore
## v0.13.857 — import selected SD save folders through checked ZIP staging
## v0.13.856 — decode JKSV ZIP metadata through shared admission
## v0.13.855 — commit ZIP restore chunks through closed handles
## v0.13.854 — verify ZIP restores through fresh read-only mount
## v0.13.853 — require verified SD recovery before ZIP save restore
## v0.13.852 — reject obvious save undersize and remove growth guesses
## v0.13.851 — fix shutdown session admission and transport teardown
## v0.13.850 — validate ZIP payload accounting before restore
## v0.13.849 — require explicit existing restore targets
## v0.13.848 — discover exact save slots and use actual space
## v0.13.847 — show MTP saves by game and user
## v0.13.846 — fix host test blockers
## v0.13.845 — share ZIP save restore backend
## v0.13.844 — make ZIP save restore fail closed
## v0.13.843 — fix backup account restore flow
## v0.13.842 — align backup metadata columns
## v0.13.841 — polish save metadata and restore
## v0.13.840 — fix compilation errors and verify build
## v0.13.839 — finish save actions compilation fixes
## v0.13.838 — fix save actions GCC build
## v0.13.837 — enrich save backups and actions
## v0.13.836 — keep web sharing awake and fix cancel dialog
## v0.13.835 — fix transliterated russian strings and uninstalled save titles
## v0.13.834 — clean saves backup actions
## v0.13.833 — improve selection mark contrast
## v0.13.832 — separate save popup from tabs
## v0.13.831 — fix compilation errors and verify build
## v0.13.830 — move selection checks to tile corners
## v0.13.829 — add save category tabs
## v0.13.828 — harden SysNAND forwarder safety
## v0.13.827 — fix Wi-Fi connection reliability
## v0.13.826 — add wi-fi management menu under tools
## v0.13.825 — fix firmware cleanup false failure
## v0.13.824 — informative error dialogs and network gate in updater
## v0.13.823 — localize firmware update and reboot notifications
## v0.13.822 — silence error on user creation cancellation
## v0.13.821 — improve downgrade warning readability
## v0.13.820 — fix downgrade staging and translation layout
## v0.13.819 — simplify translation install UI
## v0.13.818 — separate interface translation languages
## v0.13.817 — restore translation removal contract
## v0.13.816 — remove locked translations offline
## v0.13.815 — fix post-806 regressions
## v0.13.814 — preserve DBI translation files during firmware cleanup
## v0.13.813 — background translation replacement and deferred reboot on removal
## v0.13.812 — remove themes and translations on all firmware updates
## v0.13.811 — remove themes and translations on downgrade and warn about Nintendo folder
## v0.13.810 — automated post-downgrade fix via TegraExplorer and Maintenance Mode warning
## v0.13.809 — fix duplicate HR and separator navigation in DBI menu
## v0.13.808 — pin top DBI items, add separator and alphabetical sorting
## v0.13.807 — move fan curve from Kefir settings to Tools
## v0.13.806 — sort interface translation languages
## v0.13.805 — prefer standard HTTP port
## v0.13.804 — automated firmware-to-translation mapping and FW 22.5.0+ support
## v0.13.803 — fix global list focus draw order
## v0.13.802 — add kefir.local mDNS discovery
## v0.13.801 — avoid TegraScript compound conditions
## v0.13.800 — integrate profiles and playtime restore
## v0.13.799 — add test-build skill and fix compilation errors
## v0.13.798 — restore immediate NTP display with timezone
## v0.13.797 — harden remote NAND backup transfer
## v0.13.796 — receive and restore NAND backups from another console
## v0.13.795 — legend actions parity, direct restore, and console transfer share in Manage Backups context menu
## v0.13.794 — Manage Backups context menu with Select All, Delete and Rename
## v0.13.793 — diagnostic dashboard for dump script and 5s auto-reboot to Hekate
## v0.13.792 — payload swap fallback for legacy Hekate and bidirectional Hekate restore
## v0.13.791 — restore payload.bin in goHekate, early disarm in TE scripts and payload integrity audit
## v0.13.790 — embed TegraExplorer in RomFS, payload version auto-sync, dump script pause and hekate reboot
## v0.13.789 — auto launch TegraExplorer via hekate payload fallback on backup
## v0.13.788 — archive NAND transfer backups
## v0.13.787 — restore exact selected NAND transfer pack
## v0.13.786 — harden remote update check, stream-free version parsing and auto-update install destination
## v0.13.785 — fix svcCallSecureMonitor build error, SetRegion_HTK compatibility and DBI USB status/target sync
## v0.13.784 — accurate EmuNAND detection, header NAND label and SysNAND forwarder safeguard
## v0.13.783 — strict only-if-newer remote update detection via version_compare
## v0.13.782 — fix account link detection via Baas administrator IPC
## v0.13.781 — consistently center folder and file labels in icon layout
## v0.13.780 — diagnostic firmware, target translation tag and release URLs preview in interface translation menu
## v0.13.779 — on-the-fly image viewer rotation via shoulder buttons L and R
## v0.13.778 — move file browser layout setting into view options submenu
## v0.13.777 — dynamic minus button navigation to homebrew screen or app exit
## v0.13.776 — single-prompt concise notification on incomplete TegraExplorer restore
## v0.13.775 — eliminate interior tab overlap from vector folder icon
## v0.13.774 — prompt reboot after setting user profile avatar
## v0.13.773 — fix minimized install touch badge compile error and verify build
## v0.13.772 — direct firmware-matched interface translations and metadata fallback
## v0.13.771 — USB install Minimize / Expand and dedicated origin identification
## v0.13.770 — disable B and map X to Cancel installation for FTP/MTP/HTTP
## v0.13.769 — unify vector folder outline and remove interior tab overlap
## v0.13.768 — fix menu list selection outline draw priority and z-order
## v0.13.767 — transport-specific install success notifications and i18n decoupling
## v0.13.765 — fix MTP install actions and stat row label spacing
## v0.13.764 — fix USB unplug UI teardown
## v0.13.763 — refresh automatic install plan
## v0.13.762 — clarify install storage targets
## v0.13.761 — correct install queue delivery version
## v0.13.760 — add install queue sorting and free space
## v0.13.759 — repair transport install window
## v0.13.758 — serialize background MTP batch installs
## v0.13.757 — unify transport install queue UI
## v0.13.756 — clean TegraExplorer dump progress
## v0.13.755 — stabilize Kefir Hub forwarder title id
## v0.13.754 — allow zero identity BAAS placeholders
## v0.13.753 — persist account link diagnostics
## v0.13.752 — simplify user backup management
## v0.13.751 — link profiles with unique donor pool
## v0.13.750 — show cumulative install storage progress
## v0.13.749 — preserve BAAS payload during account linking
## v0.13.748 — delete linked users; backup all accounts before delete
## v0.13.747 — nand pack date, nicknames, delete only from list
## v0.13.746 — drop TE busy-wait that RESET-crashes dump
## v0.13.745 — nand pack list multi-select delete
## v0.13.744 — dump progress, pack library, no sticky toast
## v0.13.743 — dump_auto deletes one-shot temp files
## v0.13.742 — TE dump RESULT stats green/red
## v0.13.741 — TE dump_auto known-tree clear and reboot
## v0.13.740 — fix TE dump_auto combinepath on nested save files
## v0.13.739 — fix MTP haze split (SUPPORTED_EXT bound, FsSaveProxy members)
## v0.13.738 — fix Games split includes for title_nsp/ncm/save_paths
## v0.13.737 — include account_user.hpp in slim users_menu.cpp
## v0.13.736 — split USB queue, MTP haze, and File Viewer TUs
## v0.13.735 — split Games menu into game/ TUs
## v0.13.734 — split Settings menu into settings/ TUs
## v0.13.733 — USB auto-install balances usable space; live yellow storage bar
## v0.13.732 — split File Browser into filebrowser/ TUs
## v0.13.731 — split Users menu and group account domain
## v0.13.730 — auto TE dump for profiles & play hours; Ultrahand reopen hint
## v0.13.729 — restore profiles & play hours via TE auto
## v0.13.728 — manage user backups and sidebar navigation
## v0.13.727 — add portable account backup archives
## v0.13.726 — fix config folder deletion and descriptions
## v0.13.725 — defer network source connection
## v0.13.724 — browse Console Transfer HTTP sources
## v0.13.723 — simplify Console Transfer addresses
## v0.13.722 — Restore Backup TE apply link (no Horizon 0010 write)
## v0.13.721 — SnapshotOk accepts TE dump; auto-continue restore
## v0.13.720 — Restore Backup one user+NA, proven Replace only, no playtime
## v0.13.719 — Restore Backup Replace when NA unproven; keep ns/friends
## v0.13.718 — raw 0010 Undo snapshot, no unpack
## v0.13.717 — one Nintendo Account per baas, no other-console bootloop
## v0.13.716 — receive user backups over HTTP
## v0.13.715 — simplify user backup sharing
## v0.13.714 — add restore backup sources
## v0.13.713 — share Console Transfer sources over HTTP
## v0.13.712 — add Console Transfer hub placeholder
## v0.13.711 — match same-console restore by pack UID, do not clone
## v0.13.710 — same-console restore asks to replace existing profile
## v0.13.709 — restore onto existing NA; auto-write play hours via TE
## v0.13.708 — rollback path via TE; restore dialog before link
## v0.13.707 — load hekate after dump, do not stay in TegraExplorer
## v0.13.706 — name rollback script Undo_restore_if_wont_boot.te
## v0.13.705 — dump locked 0010 only via /startup.te
## v0.13.704 — mount romfs before reading dump.te
## v0.13.703 — launch TegraExplorer via hekate autoboot fallback
## v0.13.702 — hekate > payloads > tegraexplorer path in UI
## v0.13.701 — say Hekate Payloads TegraExplorer, not RCM
## v0.13.700 — dump locked 0010 via startup.te and TegraExplorer
## v0.13.699 — snapshot 0010 before restore with TE rollback
## v0.13.698 — warn then reboot; skip duplicate Nintendo Account on restore
## v0.13.697 — do not write 00F0 under Horizon; reboot after ACCOUNT stop
## v0.13.696 — honest play-hour backup restore and empty PlayEvent parse
## v0.13.695 — restore user backup with remapped play hours
## v0.13.694 — export account link, overwrite backups, dates and delete
## v0.13.693 — remap baas UID on restore and Icon link dots
## v0.13.692 — put Backup user under its own Options heading
## v0.13.691 — put Backup user and Restore Backup back in Options
## v0.13.690 — fix user backup stack overflow and L/R Backup Restore
## v0.13.689 — fix user backup restore build
## v0.13.688 — complete user backup and restore backup
## v0.13.687 — move Users Icon labels above tiles
## v0.13.686 — clarify account link safety
## v0.13.685 — separate Users Icon captions
## v0.13.684 — clarify account link save safety
## v0.13.683 — improve Users Icon labels
## v0.13.682 — fix false suspended-app gate in applet
## v0.13.681 — fix launch account-link prompt stacking
## v0.13.680 — remove account diagnostics and add Unlink
## v0.13.679 — MainMenu B back and launch account-link warning
## v0.13.678 — link only unlinked account profiles
## v0.13.677 — add manual account diagnostics
## v0.13.676 — probe administrator account linkage
## v0.13.675 — classify official accounts from save tokens
## v0.13.674 — Official vs Fake status and coloured labels
## v0.13.673 — streamline paste replacement confirmation
## v0.13.672 — label DBI translation file
## v0.13.671 — confirm file replacements
## v0.13.670 — romfs official link launch/Users UX
## v0.13.669 — LinkAllFromRomfsDonor live apply with safety filters
## v0.13.668 — LinkKind Fake vs Official status in Users
## v0.13.667 — romfs Kefir donor + LoadRomfsDonorPackage
## v0.13.666 — dump 0010 via copied file lists
## v0.13.665 — dump full account save 0010 /su
## v0.13.664 — add read-only official-link save layout probe
## v0.13.663 — match BaaS export by content
## v0.13.662 — fix NAS export condition precedence
## v0.13.661 — avoid nested NAS prefix loop in export
## v0.13.660 — draw System Tools focus text above border
## v0.13.659 — fix live DBI queue compatibility and metrics
## v0.13.658 — sync live DBI queue additions and metrics
## v0.13.657 — fix TimeStamp update method in dbi_menu & verify WSL build
## v0.13.656 — sync DBI backend queue selection
## v0.13.655 — continue DBI queue after skipped package
## v0.13.654 — improve queue scrolling and screen-off options
## v0.13.653 — keep System Tools focus border on top
## v0.13.652 — extend payload label scan
## v0.13.651 — unified manual firmware file and folder picker
## v0.13.649 — add downgrade warning QR dialog
## v0.13.648 — add manual firmware ZIP install
## v0.13.647 — add safe payload handoff and touch hold
## v0.13.646 — fix NAS filename matching in export script
## v0.13.645 — pair account save iterator fix with TegraExplorer
## v0.13.644 — read account link data from su directory
## v0.13.643 — fix account transfer script diagnostics
## v0.13.642 — auto-select NAND for account transfer
## v0.13.641 — restore account link status semantics
## v0.13.640 — fix compilation and verify NRO build and tests
## v0.13.639 — prepare offline official link transfer
## v0.13.638 — respect system timezone after clock sync
## v0.13.637 — export selected official account link
## v0.13.636 — query Horizon account link status
## v0.13.635 — clarify Nintendo Account link status
## v0.13.634 — use Horizon user creator and select SGDB games
## v0.13.633 — remove embedded user avatar presets
## v0.13.632 — make user backup paths UID-unique
## v0.13.631 — harden readable user backup export
## v0.13.630 — readable user backup export
## v0.13.629 — NAND dump never kills services; TegraExplorer dump.te fallback
## v0.13.628 — vector folder tiles, centered names, SGDB search keyboard
## v0.13.627 — avatar picker on create; delete user without killing account
## v0.13.626 — folder silhouette around Icon-layout previews
## v0.13.625 — image picker A selects; Fit Image only after zoom
## v0.13.624 — fix IsIconLayout compile, ReleaseWithInstall built
## v0.13.623 — never terminate ns when dumping play hours
## v0.13.622 — user UID in all layouts, folder mosaics, avatar crop
## v0.13.621 — backup user does not delete; delete requires hold A
## v0.13.620 — Users list layout, create-before-complete, avatar image picker
## v0.13.619 — fix avatar JPEG compile, ReleaseWithInstall built
## v0.13.618 — encrypt profile and play-hour restore in Hub via FS commit
## v0.13.617 — decrypt profiles and play hours on console, restore via TegraExplorer
## v0.13.616 — Users manager with grid, avatars, backup and delete
## v0.13.615 — mount account save via system_save_data_id
## v0.13.614 — import official Nintendo Account link instead of Linkalho-only stub
## v0.13.613 — Users menu with Linkalho-style offline account link
## v0.13.612 — explain sys-patch and FunControl instead of ErrorBox
## v0.13.611 — draw menu under the web-server overlay again
## v0.13.610 — Module Manager running counter and filter
## v0.13.609 — After reboot Enabled/Disabled uses the same green/grey
## v0.13.608 — Module Manager status dot green on, grey off
## v0.13.607 — drop Overlay memory, keep Sysmodule RAM
## v0.13.606 — Overlay memory bar and retry per-module RAM via debug
## v0.13.605 — Module Manager shows System RAM used and free
## v0.13.604 — drop Task Manager, show per-module RAM
## v0.13.603 — Task Manager lists every process using RAM
## v0.13.602 — Module Manager shows where RAM actually goes
## v0.13.601 — drop Software Network Downloads, match Tools icon set
## v0.13.600 — menu action icons, Games options, Tools tiles
## v0.13.599 — fix crash when nxlink exits with a file viewer open
## v0.13.598 — pass INVALID_HANDLE to svcGetSystemInfo
## v0.13.597 — Module Manager RAM via svcGetSystemInfo
## v0.13.596 — remote editor shows a closed-session overlay
## v0.13.595 — remote editor Save no longer closes the session
## v0.13.594 — Game Tools folder, system-tool stubs, Module Manager in Tools
## v0.13.593 — L3 launches the focused game from the Games list
## v0.13.592 — MTP Games drops Unmerged, localizes folder names
## v0.13.591 — MTP Games puts forwarders in their own folder
## v0.13.590 — replace dynamic_cast with virtuals (-fno-rtti)
## v0.13.589 — Games list uses the same badge pills as icon layouts
## v0.13.588 — keep NAND/SD move UI alive so Cancel works
## v0.13.587 — NAND/SD move shows the NCA being copied
## v0.13.586 — detect USB flash after MTP, open and close that drive
## v0.13.585 — do not hold USB as gadget when plugging a flash drive
## v0.13.584 — blue NAND/SD bars only on storages that hold the game
## v0.13.583 — MTP Games dumps — compatible, separate, or both
## v0.13.582 — USB plug identifies flash vs PC (open browser / auto MTP)
## v0.13.581 — restore only-if-newer auto-update for GitHub release
## v0.13.580 — Ask skip is a full-width row; Minus skips, Plus updates
## v0.13.579 — tap Ready — restart to relaunch Kefir Hub
## v0.13.578 — drop On demand; Ask dialog is Later / Skip this update / Update
## v0.13.577 — Auto-update is a folder in General, not a sidebar category
## v0.13.576 — stop the header N/M counter from jittering
## v0.13.575 — parse ### changelog headings; About update vs refresh notes
## v0.13.574 — Saves settings for filters, default location, WebDAV
## v0.13.573 — Network shows FTP and MTP toggles, then their folders
## v0.13.572 — Auto-update category at the top of Settings
## v0.13.571 — Y toggles boolean lines in the text editor
## v0.13.570 — toggle 0/1 with A; fix swkbd overflow after applet
## v0.13.569 — pass StopToken to silent auto-update ToFileAsync
## v0.13.568 — non-silent update uses the download transfer icon
## v0.13.567 — force auto-update from GitHub latest for mode testing
## v0.13.566 — silent auto-update with header progress and update modes
## v0.13.565 — text-file open menu before viewing; L/R+Up/Down in range legend
## v0.13.564 — stop auto-forwarder from stalling launch and duplicating HOME icons
## v0.13.563 — ask to save unsaved remote editor changes on B
## v0.13.562 — make the remote CodeMirror editor fill the window
## v0.13.561 — Edit on PC / phone from the file browser Options
## v0.13.560 — full-page CodeMirror editor for remote file edit
## v0.13.559 — expand an existing text selection from either edge
## v0.13.558 — stretch the line outline across the whole text selection
## v0.13.557 — file edit and paste reuse the existing remote input page
## v0.13.556 — paste multiline text from PC/phone at the editor cursor
## v0.13.555 — edit text files in the browser on PC or phone
## v0.13.554 — Create Folder in extract picker defaults to the archive name
## v0.13.553 — Enter in Direct Download sends the URL
## v0.13.552 — add Close picker to folder-picker Options
## v0.13.551 — picker Create Folder only, minus returns to extract options
## v0.13.550 — keep zip-extract row lines still, pad selection off the stripes
## v0.13.549 — smaller zip-extract checks, X/Y select, create folder in picker
## v0.13.548 — remove previous Kefir Hub HOME icon when installing a new one
## v0.13.547 — after zip app install, offer launch instead of the folder
## v0.13.546 — full-screen zip extract with tree, checkboxes, named folder
## v0.13.545 — use Manual video capture on forwarders, Auto crashed am
## v0.13.544 — treat OptionBox glyph as part of the caption again
## v0.13.543 — center OptionBox buttons, honest auto-forwarder notices
## v0.13.542 — enable forwarder capture, collapse stacked http(s), friendly URL errors
## v0.13.541 — label single-NRO zip action as install to /switch
## v0.13.540 — wrap OptionBox button labels, keep plus glyph fixed
## v0.13.539 — install a single zip NRO to /switch/stem, drop extract-to-root
## v0.13.538 — hide remote-input Paste on desktop, keep it on phone
## v0.13.537 — GameCard row, drop dead install screens, zip extract defaults
## v0.13.536 — fix nacp_util::GetName in forwarder_auto_install.cpp
## v0.13.535 — optimize auto-forwarder with fast-path check and avoid touching /Games folder
## v0.13.534 — clean legacy HBL forwarders and generate native KefirHub forwarder on the fly
## v0.13.533 — fix auto-forwarder thread lifecycle to guarantee threadClose and bypass in Application mode
## v0.13.532 — ensure 64-bit integer-safe NRO bounds and pre-body read validation
## v0.13.531 — validate contiguous OverrideHeap and check NRO code/BSS bounds
## v0.13.530 — clean NRO launch handoff and eliminate duplicate FS commit
## v0.13.529 — Fix List null controller dereference in Forwarder Editor and improve dual-pane D-Pad navigation
## v0.13.528 — Fix HBL loader NRO segment bounds, BSS zeroing, AppletType detection & full heap restoration
## v0.13.527 — Separate Network Downloads and Custom Link into dedicated bottom section in Software Menu
## v0.13.526 — Move Network Downloads and Custom Link to Software Menu, focusing Updater on Kefir and Firmware
## v0.13.525 — USB 3.0 indicator, graceful download cancellation, universal remote input (QR/Web) & direct NRO d...
## v0.13.519 — AppStore EntryMenu layout anti-overlap & instant launch state transition
## v0.13.518 — AppStore installed version display, RetroArch LibRetro Nightly 7z extractor & clean network teardown
## v0.13.516 — AppStore EntryMenu launch confirmation guard
## v0.13.515 — UPA-13 confirmed ROM database compatibility aliases
## v0.13.514 — UPA-11 GameCard theme roles and safe storage ratio
## v0.13.513 — UPA-10B localized UTF-8 MTP display names
## v0.13.512 — UPA-10A usable-title core and ASCII-safe NSP export naming
## v0.13.511 — UPA-09 forwarder editor touch/controller focus matrix
## v0.13.510 — UPA-08A discovery gate and UPA-08B raw FTP mutation adapter
## v0.13.509 — UPA-07B MTP delete/rename/directory operations mutation coverage
## v0.13.508 — UPA-07A MTP upload/final-close shared mutation policy integration
## v0.13.507 — UPA-06 shared homebrew mutation policy and complete Web success coverage
## v0.13.506 — UPA-05 playtime worker UI-thread isolation and race elimination
## v0.13.505 — UPA-04A MTP zero-byte upload support and patch shape verification
## v0.13.504 — UPA-03 centralized GitHub and direct URL validation
## v0.13.503 — UPA-02B GHDL ZIP type detection and safe non-ZIP destination
## v0.13.502 — UPA-02A GHDL operation identity, cancel and temp isolation
## v0.13.501 — UPA-01 GitHub downloader callback ownership and selection safety
## v0.13.500 — guard NRO heap after return
## v0.13.499 — auto-update and tools UI
## v0.13.496 — auto-update and tools UI
## v0.13.495 — pixel-balanced split & full-width justified 2-row footer layout
## v0.13.491 — fix flush thread stack overflow
## v0.13.490 — fix cstring include in static logger
## v0.13.489 — zero-heap static logging buffer and image load ordering
## v0.13.488 — increase sysmodule boot timeouts for slow SD cards
## v0.13.487 — stabilize microSD FS sync, background logger and NanoVG image decoding
## v0.13.469 — unify pending UI and updater work
## v0.13.468 — stabilize cURL shutdown
## v0.13.467 — version HTTP user agent
## v0.13.466 — stabilize menu header subheadings
## v0.13.465 — add multi-line text editing
## v0.13.464 — fix Homebrew search path build
## v0.13.463 — add local forwarder icon crop editor
## v0.13.460 — refine text editor controls
## v0.13.458 — add Homebrew settings
## v0.13.457 — decouple text viewer read-only viewport scrolling
## v0.13.456 — add streamed text viewer pager
## v0.13.454 — add localized NSP install diagnostic messages
## v0.13.453 — harden PFS0 NSP parser
## v0.13.452 — restore NRO loader affinity
## v0.13.451 — add custom NRO search paths
## v0.13.450 — harden NRO icon normalization
## v0.13.449 — add read-only NFS source
## v0.13.448 — limit NTP notifications
## v0.13.447 — harden ZIP extraction paths
## v0.13.446 — enable HOS clock auto correction
## v0.13.445 — persist NTP time via set:sys
## v0.13.444 — trace NTP synchronization on screen
## v0.13.443 — write NTP time via system-user service
## v0.13.442 — prevent file browser association crash
## v0.13.440 — add install queue package skip and queue cancel controls
## v0.13.439 — fix immediate NTP time synchronization
## v0.13.438 — add USB 3.0 Kefir toggle
## v0.13.437 — finalize text editor delivery
## v0.13.433 — Add TICO core launchers and forwarders
## v0.13.432 — Improve ROM forwarder titles and validation
## v0.13.431 — Fix missing includes in file_viewer and format specifier
## v0.13.430 — Interactive screensaver controls via analog sticks
## v0.13.429 — Document delivery and audit
## v0.13.413 — Rebind the web server after a sleep, close it if the address moved
## v0.13.412 — Plan where every queued title lands before installing any of it
## v0.13.404 — End the header gap before NAND, not before SD
## v0.13.403 — The header gap is a slot, sized by what the title leaves
## v0.13.402 — Time the shutdown too, and account for frames
## v0.13.401 — Sub heading moves to the header, marked rows get a background, boot timings
## v0.13.400 — Measure the footer hint row when it changes, not every frame
## v0.13.399 — Fling scrolling, wrap past the Updater's captions, select-and-advance
## v0.13.398 — Wrap every list, unmerge the footer, mark list rows like the file browser
## v0.13.397 — Game details page, DBI-style list rows, nxlink that stops on a dead socket
## v0.13.386 — Drop the unused half of the curl Api, fold i18n's three lookups
## v0.13.385 — One case-insensitive path compare instead of five
## v0.13.384 — One copy of the firmware version logic, with tests
## v0.13.383 — Delete dead code the compiler could never warn about
## v0.13.382 — Mount every selected folder, not just one
## v0.13.381 — A ".." row, and Mount acts on what the cursor is pointing at
## v0.13.380 — Mount the highlighted folder, not the folder you are standing in
## v0.13.379 — Root means the same thing in the browser and over HTTP
## v0.13.378 — Survive a failed reconnect; reset the recovery guard per session
## v0.13.377 — Retry a failed post even when the endpoint reports healthy
## v0.13.376 — Retry a stalled post in place; open endpoints like libusbhsfs does
## v0.13.375 — Align USB posts to the real max packet size, not a hardcoded 512
## v0.13.374 — Never hold an MTP data phase open across a reader stall
## v0.13.373 — Never cancel an MTP transaction; size the request to the reader
## v0.13.372 — Stop RemoveDevice from nulling the sd card's devoptab slot
## v0.13.371 — Fix yati shutdown deadlock and settle MTP cancels properly
## v0.13.370 — Clear USB endpoint halts instead of tearing the MTP link down
## v0.13.369 — Stream MTP file reads in one long transaction
## v0.13.368 — Survive MTP session drops mid-install, halve read transactions
## v0.13.367 — MTP host self-heal, devoptab NULL-hole crash, honest listing errors
## v0.13.366 — One mount, shared by FTP, HTTP and MTP
## v0.13.365 — Close remaining MTP host races and read-path gaps
## v0.13.364 — Rewrite MTP host transport, protocol and session handling
## v0.13.363 — Fix folder mounting over FTP and HTTP
## v0.13.362 — Fix USB transfer handling - eventWait check, retry with delay, memcpy UB fix, re-enable pre-fetch
## v0.13.361 — Strict MTP interface filter (only ifClass 0x06), revert pre-fetch to prevent USB system crash
## v0.13.360 — Fix MTP mount path (keep trailing slash) + pre-fetch root entries during scan
## v0.13.359 — Store dir path in Dir struct and use full device-qualified paths for stat() fallback on DT_UNKNOWN
## v0.13.358 — Add stat fallback for DT_UNKNOWN devoptab entries in fs.cpp
## v0.13.357 — Fix USB DMA read buffer post size to prevent endpoint packet babble
## v0.13.356 — Strip device prefix in ResolvePathToHandle to fix empty MTP directory listing
## v0.13.355 — Fix MTP Host empty directory listing and active session disconnect probe
## v0.13.291 — Show confirmation prompt when cancelling MTP installations (via B or Stop).
## v0.13.290 — Optimize Stream buffering for MTP installs to prevent speed drop and USB timeout.
## v0.13.288 — feat: close sidebar with START button
## v0.13.287 — fix: resolve MTP stall by returning short stream reads early
## v0.13.286 — fix: resolve Yati member compilation error in InstallNcaInternal
## v0.13.285 — fix: replace scary error dialog with friendly notification on install cancel
## v0.13.284 — fix: resolve deadlock on cancel, add verbose condvar logging to installer
## v0.13.283 — fix: replace if with while for condvarWait to fix 3% install hang
## v0.13.282 — fix: MTP install hangs on EOF, block input while expanded, Stop button, dynamic badge
## v0.13.281 — fix: robust handling of EOF and short reads during MTP install
## v0.13.253 — feat: R/W speed graph, install-hang fixes, ReleaseWithInstall builds
## v0.13.252 — fix: send auth across redirects, wrap-around browsing, cursor-first metadata
## v0.13.251 — fix: WebDAV Digest auth, friendly network errors, persistent source badges
## v0.13.250 — fix: robust WebDAV/HTTP listing and root-level source management
## v0.13.249 — fix: validate remote sources and WebDAV sync
## v0.13.248 — fix: verify install no-sleep guard
## v0.13.247 — fix: clarify game badges and storage totals
## v0.13.246 — fix: scroll long HB menu card titles
## v0.13.245 — fix: align game badges and detail controls
## v0.13.244 — fix: mark and filter unavailable game records
## v0.13.243 — fix: crashes when browsing HTTP/WebDAV sources
## v0.13.242 — feat: stack game content badges vertically
## v0.13.241 — feat: allow changing network source protocol
## v0.13.240 — feat: refine games selection and details UX
## v0.13.239 — feat: rebuild games UI around DBI details
## v0.13.238 — feat: harden game details and applet mode
## v0.13.237 — feat: implement WebDAV, FTP, HTTP browsing, System Root and status badges
## v0.13.236 — feat: handle location name collisions and show protocol type in source lists
## v0.13.235 — feat: accept any HTTP response code from 200 to 599 as successful connection test
## v0.13.234 — feat: simplify HTTP connection testing using HEAD request without PROPFIND
## v0.13.233 — feat: preserve focus index and scroll offset on SettingsMenu and SourceEditMenu focus restore
## v0.13.232 — feat: show all configured network locations in file browser mount picker and restrict non-smb bro...
## v0.13.231 — feat: implement sidebar context menu, connection testing, and auto url copying for network sources
## v0.13.230 — feat: fix translation removal target lock, filter empty translations, and add auto language switc...
## v0.13.225 — feat: wrapping submenus, translation package updates, language/region filtering
## v0.13.223 — fix: resolve Use-After-Free crash on add network source by replacing PopToMenu with Pop
## v0.13.222 — feat: implement single source of truth for saves sync and support FTP and local locations
## v0.13.221 — feat: integrate WebDAV saves config as a shared source and fix swkbd crash
## v0.13.220 — feat: WebDAV, FTP, HTTP storage sources, settings management, and battery layout fix
## v0.13.219 — Harden SMB network sources
## v0.13.217 — Integrate localized sysmodule catalog
## v0.13.216 — Correct and validate sysmodule catalog
## v0.13.215 — Complete sysmodule catalog with newly verified modules and overrides
## v0.13.213 — feat(modules/settings): UX improvements to Module Manager and Settings touch navigation
## v0.13.212 — Fix install-queue UI bugs (id 57-59) in PC Install (USB)
## v0.13.210 — DBI Backend protocol extension - file sizes in LIST response
## v0.13.209 — MTP copy progress popup, WebDAV settings folder, File Browser Sources picker
## v0.13.208 — MTP Saves drive (S6 / id 37) - read-only decrypted game saves
## v0.13.207 — Review fix for S5 - revert MTP toggle when haze::Init() fails
## v0.13.206 — MTP storage options
## v0.13.205 — Cheats i18n, ProgressBox overflow, selection outline clipping, zip name sanitize
## v0.13.204 — Review follow-ups for S1/S4 - stdio auto-sync upload and cancel result code
## v0.13.203 — Shared /dumps constant and honest Location/Sync tooltips
## v0.13.202 — Sync resilience - continue past failed transfers, summarise at end
## v0.13.201 — Fix auto-sync after Backup ignoring the selected backup location
## v0.13.200 — Fix DeletePath/MovePath directory regression from Step 14.4
## v0.13.193 — Fix web.cpp duplication and restore LF line endings
## v0.13.192 — Decompose web.cpp and extract web_http and web_screenshots
## v0.13.191 — Mark Phase 11 complete, document comment-restoration fixes for Phases 10-11
## v0.13.190 — Decompose cheat game select menu and isolate CheatDownloadMenu
## v0.13.181 — docs(walkthrough): add missing entries for (Step 6.2) and move to top
## v0.13.180 — docs(walkthrough): add missing entries for (Step 6.2) and move to top
## v0.13.146 — Complete MTP, module manager, and WebDAV updates
## v0.13.133 — Allow concurrent web server clients and show install progress from any device
## v0.13.131 — Fix case-sensitive NCA/tik/cert lookup causing install failures (YatiNcaNotFound)
## v0.13.130 — Use NanoVG vector arrow for submenu indicator, remove arrow from PC Install (USB)
## v0.13.129 — Fix compilation error due to extra closing brace in filebrowser.cpp
## v0.13.128 — Reorder context menu options and add submenu arrows
## v0.13.127 — Integrate WebDAV auto-sync, update battery charging indicators, and cleanups
## v0.13.126 — Fix Themezer exit crash, hide ZL/ZR from legend, and add Down/Right auto-navigation
## v0.13.125 — Fix early namespace closure compile error in filebrowser.cpp
## v0.13.124 — Reorganize file options menu to Windows-like layout and translate descriptions
## v0.13.123 — Update Themezer page navigation, layout, and Ukrainian translations
## v0.13.122 — Add Connection Options sidebar to Tools menu for Plus button
## v0.13.121 — Support deleting Favorite themes via R3 in Themes menu
## v0.13.120 — Merge ZL/ZR legend for theme pages and fix Screenshot translation
## v0.13.119 — Reorganize Software -> DBI menu and move PC Install (USB) to it
## v0.13.118 — Fix sorting tooltips and Show Hidden translations in Homebrew menu
## v0.13.115 — Fix status bar overlapping and vertically align NAND/SD labels
## v0.13.114 — Implement saves synchronization with remote WebDAV and auto-sync after backup
## v0.13.113 — Add SMB network storage mount support and play via NXMP
## v0.13.112 — Implement Phase 8.1 - Play with NXMP from File Browser
## v0.13.111 — Fix thread safety and cleanup in USB and DBI menus
## v0.13.110 — Add support for DBI Backend USB protocol
## v0.13.109 — Fix install_location migration bug for fresh users
## v0.13.108 — Set default install location to Auto and support all 5 location priority types in Settings UI
## v0.13.107 — Implement Target layout and Tags filters in Themezer client
## v0.13.106 — Fix thread safety in BackgroundInstaller, optimize status bar storage polling, and resolve audit...
## v0.13.105 — Support background MTP game installation from any interface screen
## v0.13.104 — Support for custom install location priority and free space reserve threshold
## v0.13.103 — Support for NACP v2 format introduced in FW 20.0+
## v0.13.102 — Reorganize status bar UI with dual NAND/SD storage capacity bars and static charging icon
## v0.13.101 — Add List layout to Saves, move Layout to Save Options top level, fix selection checkmark on icon
## v0.13.100 — Fix sidebar option label scrolling - scroll label text instead of value
## v0.13.99 — Localize all hardcoded strings in settings_menu, add Save/Fan/Kefir translations to i18n
## v0.13.98 — Ignore 0x1002 error when deleting translations, localize Translate Interface title
## v0.13.95 — Auto-crop and upscale icon symbols, use exact background color from example
## v0.13.94 — Sliced Tools icons with transparency, render dark icon background in C++ code
## v0.13.93 — Slice and apply new Tools icons, improve HoldConfirmBox dynamic fonts and line spacing, translate...
## v0.13.91 — Merge App Store and Software menus under Software, update uk.json translations and always render...
## v0.13.89 — Center Install button in changelog, translate all changelog text, refactor fast scroll through Li...
## v0.13.88 — Move Install button to center to prevent overlap with Cancel, translate all changelog labels, fix...
## v0.13.87 — Show changelog preamble only when target version description is not found in the file
## v0.13.86 — Require full scrolling of Kefir changelog to focus and activate Install button, and fix version d...
## v0.13.85 — ZL/ZR fast scrolling (except settings), and Left/Right page-middle navigation in List View
## v0.13.84 — Always show Kefir changelog, skip auto-skip logic, and handle download cancellation without showi...
## v0.13.83 — Fix double keydown events using stopImmediatePropagation, move AppendConfirmModal out of header,...
## v0.13.82 — Fix confirmation double execution, deduplicate confirm modals, and resolve title service lifecycle
## v0.13.80 — Fix infinite recursion on START button press in MenuBase
## v0.13.76 — Fix file item focusing scroll-margin-top, add Backspace parent folder navigation, and implement i...
## v0.13.74 — Fix compilation error in PopupList B1, remove dead SanitizeRelativePath B2
## v0.13.73 — Fix resource leaks A2, remove dead ShareMode A3, optimize title initialization A4
## v0.13.72 — Context menu for Web Server & Screenshots in Tools, change route to /album, fix upload queue esca...
## v0.13.71 — Fix compilation errors: restore namespace structure and use explicit type in progress box lambda
## v0.13.70 — Fix review comments: deduplicate progress box, case-insensitive screenshots, document P2-7, facto...
## v0.13.69 — Clean up code and expand HTTP read limit to 16 KB
## v0.13.68 — Optimize web server memory allocation using constexpr string_view
## v0.13.67 — Fix active download cancellation and cleanup dead code
## v0.13.66 — Implement Screenshot Gallery and integrate into Tools menu
## v0.13.65 — Remove obsolete Share Images and image/gallery page builders dead code
## v0.13.64 — Extract NAND/SD target heuristic into ChooseInstallTarget helper
## v0.13.63 — Add deletion logging in HandleDelete
## v0.13.62 — Convert ScanDirectoryRecursive to DFS and increase server thread stack to 128KB
## v0.13.61 — Make ProgressBox pointer and mute flag atomic to eliminate data races
## v0.13.60 — Add socket stream timeouts and cancellation checks
## v0.13.59 — Fix image share and gallery routing
## v0.13.54 — Web SPA, sequential transfer queue, direct game installation, battery anim, touch stop, dynamic s...
## v0.13.53 — feat(web): drag-prevention and list-container element ID fixes
## v0.13.52 — feat(web): custom checkbox styling, keyboard navigation (ArrowKeys, Space, Esc, Delete, Enter), a...
## v0.13.15 — Improvement: Invert panning controls and fix themezer ProgressBox/LazyImage collisions
## v0.13.14 — Improvement: Add solid overlays and 3s hold confirm actions
## v0.13.12 — Feature: Add Image Theme Creator
## v0.13.11 — Combine L/R and ZL/ZR buttons in the bottom legend bar as single items with slash dividers and co...
## v0.13.10 — Trigger actions on release (UP) instead of press (DOWN) for widget buttons, and filter out chord/...
## v0.13.9 — Reduce spacing in button legend bar, display L/R always, shorten Bezier label, and enable horizon...
## v0.13.8 — Implement Bezier Helper Mode as an auxiliary overlay (green curve) that preserves the original cu...
## v0.13.7 — Implement 3-point Bezier Easy Curve Mode and fix graph sensor marker drawing layers
## v0.13.6 — Query actual hardware fan speed level using fanControllerGetRotationSpeedLevel in sysmodule
## v0.13.5 — Fix fan control IPC conflicts, add SoC temperature sensing, and implement smooth fan inertia visu...
## v0.13.3 — FunControl sysmodule version tag (not a Kefir Hub release)
