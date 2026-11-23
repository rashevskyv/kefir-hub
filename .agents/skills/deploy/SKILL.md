---
name: deploy
description: >
  Ship Kefir Hub to GitHub as a new release: English changelog, git push,
  tag without a leading v, gh release with kefir-hub.nro. Not nxlink, not
  a console send. Use when the user says деплой, deploy, GitHub release,
  залий на гітхаб, зроби реліз, or runs /deploy.
---

# Deploy (GitHub only)

Console nxlink is a different request. This skill never sends the NRO to the Switch.

Repo: `rashevskyv/kefir-hub`. Always `gh --repo rashevskyv/kefir-hub`.
Checkout: only `D:\git\dev\kefir-hub` (WSL `/mnt/d/git/dev/kefir-hub`, see `AGENTS.md`). No worktrees.
Branches: work is on `dev`; `master` holds only released code. The Prelude robot commits and releases on `master`
by itself and merges `master` back into `dev`. A deploy is: bring the robot's commits into `dev`, then move `master` to `dev`.
Version: `sphaira/CMakeLists.txt` `set(sphaira_VERSION …)` is the only source.
Tag and title: `0.13.X` and `Kefir Hub 0.13.X`. Never `v0.13.X` — old 563–565 clients cannot parse a leading `v`.

## Before shipping

1. Confirm `git rev-parse --show-toplevel` is `D:\git\dev\kefir-hub` and the branch is `dev`.
1b. `python tools/dev/merge_master.py`: takes the robot's Prelude releases from `origin/master` (version line and
    CHANGELOG top are resolved by the script; any other conflict stops it, merge by hand).
2. Working tree clean. If product work is unfinished, bump + docs + commit per `AGENTS.md` first.
3. `kForceUpdateForTest` in `sphaira/source/ui/menus/main_menu.cpp` must be `false`. If it is `true`, set it false, bump, docs, commit — do not ship a test hook.
4. Run `python3 tests/test_i18n_deployment_contract.py`; stop if any shipped language is missing or incomplete.
5. Last GitHub release: `gh release view --repo rashevskyv/kefir-hub` (or `gh release list`). Changelog covers **that tag → HEAD**, not only the last commit.

## Build the NRO

Need `build/ReleaseWithInstall/switch/kefir-hub/kefir-hub.nro` matching this version.

WSL, from `/mnt/d/git/dev/kefir-hub`. Quote the bash script so PowerShell does not expand `$(nproc)`:

```
wsl bash -lc 'cd /mnt/d/git/dev/kefir-hub; cmake --preset ReleaseWithInstall; cmake --build --preset ReleaseWithInstall --parallel $(nproc)'
```

Do not `make nxlink`. Do not copy to `_kefir`.

## Changelog

English. Write `build/release_notes_0.13.X.md` (build/ is local; do not commit it unless asked).

Shape (match `0.13.593` / `0.13.601`):

```
# Kefir Hub 0.13.X

Update from 0.13.Y.

### Section
- User-facing bullets. No commit hashes, no ticket ids.
```

Sections only for what actually changed (Tools, Games, Text editor, USB and MTP, Fixes, …). Collect from `git log <last-release-tag>..HEAD` and `walkthrough.md`.

## Publish

```
git push origin dev
git push origin dev:master
gh release create 0.13.X --repo rashevskyv/kefir-hub --title "Kefir Hub 0.13.X" --notes-file build/release_notes_0.13.X.md build/ReleaseWithInstall/switch/kefir-hub/kefir-hub.nro
```

`dev:master` must be a fast-forward (step 1b makes it one); if git refuses it, run step 1b again, never force. Attach the NRO so the asset name is `kefir-hub.nro`.

Verify: `gh release view 0.13.X --repo rashevskyv/kefir-hub` — tag, title, notes, asset.

## Tell the user

Paste the release URL and the full English changelog. Say the NRO is on GitHub, not on the Switch, unless they also asked for nxlink.
