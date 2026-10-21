# Console checklist (what is built but not yet checked on the Switch)

Tick a line when it works on the console. Versions are the ones that introduced the change.

## Tools
- [ ] **Web browser** (0.14.181, favourites in 0.14.182): Tools → Web browser opens a page with "Enter an address" and "Favourites".
  - [ ] A on "Enter an address" opens the keyboard or the QR page; send an address from a phone; the browser opens it.
  - [ ] Y adds a favourite (address typed or sent from a phone). The same address twice shows "Already in the favourites".
  - [ ] X on a favourite asks, then removes it. A on a favourite opens it.
  - [ ] Start Hub from a normal Homebrew launch (applet mode): the error box about full memory appears, no crash.
- [ ] **Storage manager** (0.14.177–0.14.180): ring chart, emuMMC as one container, Y "What is an emuMMC?", slice "Emulators and their games". Not yet done: system memory ring, delete/open actions.

## Battery
- [ ] **Drain screensaver** (0.14.179): start a drain test, wait for the saver, check the numbers; any button wakes it.
- [ ] **Hop between MMCs** (0.14.163): the wizard restarts into the other system and comes back.
- [ ] **Full pipeline** with the user: read → calibrate → write. PRODINFO write only with an external dump and the user's go-ahead.

## Homebrew and updates
- [ ] **Kefir changelog scrolling** (0.14.178): D-pad moves by pages, the stick glides.
- [ ] **Delete with folder** (homebrew delete): the question, the folder removal, nothing else lost.
- [ ] **Update flow**: title "old -> new", restart into the new version.
- [ ] **SteamGridDB**: batches, cache, crop, Y search, overall progress.

## Not started on the console
- [ ] **F.20 repack** (SteamWorld Dig 2), multi-select repack, IPS, DLC.
- [ ] Doc screenshots D.5 (the user's own shots or video).

## Before the next GitHub release (after 0.14.159)
- [ ] Translate the new i18n keys into all languages (`tools/i18n-translate/translate.py` via the proxy).
- [ ] `tests/test_i18n_deployment_contract.py` passes; release notes in `build/release_notes_*.md`.
