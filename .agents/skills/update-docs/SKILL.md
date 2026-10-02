---
name: update-docs
description: >
  Keep the Kefir Hub user docs (docs/site, EN+UK), their screenshots, the video scripts and the
  switch.customfw.xyz guide (branch kefir-hub) in step with the code. Use after any change a user can
  see: a menu, label, option, default, dialog, behaviour, or a feature added or removed. Also use for
  "update docs", "онови документацію", "актуалізуй інструкцію", "зроби скріншоти", /update-docs.
---

# Update docs («Онови документацію»)

The docs describe what the console shows. A commit that changes something the user sees and leaves the
docs saying otherwise is unfinished. This skill is the same for Codex, Gemini and Claude.

## Map
| What | Where |
|---|---|
| Docs source (English, the reference) | `docs/site/en/**/*.md` |
| Ukrainian docs (same files, same headings) | `docs/site/uk/**/*.md` |
| Writing rules: read before writing | `docs/site/STYLE.md` |
| Nav / page list | `docs/site/mkdocs.yml` |
| Feature → page map, features under review | `docs/dev/DOCS-COVERAGE.md` |
| Open questions, code findings | `docs/dev/AUDIT-2026-10-02-docs.md` |
| Screenshots (English UI) | `docs/site/en/img/<shot-id>.png` |
| Shot list (which ids exist, which are taken) | `python docs/site/shotlist.py` |
| Video scripts + draft subtitles | `docs/video/NN-topic/script.md`, `python docs/video/srt.py` |
| Guide site (Jekyll, Ukrainian), separate clone | `D:\git\site\switch-hub`, branch `kefir-hub`, notes `_rework/REVIEW.md` |
| Emulator + screenshot tools | `tools/docs/eden.ps1`, `tools/docs/web-shot.mjs` |
| Checks | `tests/test_doc_labels_contract.py`, `docs/site/build.sh`, `tools/docs/check_site_links.py` |
| Local review for the owner | `docs/site/review.ps1` |

UI names in pages are `[[en.json key]]`. The build replaces them with the app's own string in the page
language (`docs/site/hooks/ui_labels.py`), so every language uses the production button names. Never write
a translated UI name by hand in docs/site.

## Procedure
1. **Find what changed for the user.** Run `git log` / `git diff` since the last `docs:` commit. Look at
   `"..."_i18n` literals added, removed or renamed, `assets/romfs/i18n/en.json`, settings defaults, menus
   (`sphaira/source/ui/menus/**`) and dialogs. Write the list down: added / changed / removed.
2. **Find where it is documented.** Start with `docs/dev/DOCS-COVERAGE.md`. Then grep the old UI string as
   `[[Old label]]` in `docs/site`, in `docs/video/*/script.md` (labels quoted in UK/EN) and in the site clone
   (`grep -rn` on `_pages`, `_includes`).
3. **Read the code** for the new behaviour (graphify first, line ranges, see AGENTS.md). Facts come from the
   code or from the owner, never from old docs or guesses.
4. **Edit the English page**, following STYLE.md: plain words, the task as numbered steps, an options table,
   a Problems section.
5. **Edit the Ukrainian page the same way**: same headings in the same order (heading ids come from the
   English page, and the build fails on a mismatch). Write natural Ukrainian, «папка», «карта пам'яті».
6. **Per kind of change:**
   - *Renamed label*: replace `[[Old]]` with `[[New]]` everywhere, including nav titles in `mkdocs.yml`.
   - *New feature or option*: add a section, a coverage row and `<!-- shot: page-thing | what the screen shows -->` markers.
   - *Changed default or behaviour*: fix the tables and steps, and any warning that no longer holds.
   - *Removed feature*: delete its sections or page, its nav entry, its shots (`img/<id>.png` and markers),
     its video scenes and its site mentions. Mark the coverage row `removed in v0.13.X`. Nothing may tell
     the user to press a button that no longer exists.
   - *Feature under review* (listed in DOCS-COVERAGE.md): keep it documented as it is. Do not expand it
     and do not build other pages around it. When the owner decides, follow "removed" or drop it from the list.
7. **Screenshots** for every shot whose screen changed (see below). If a shot cannot be taken in the
   emulator, keep the marker and list it as a `[USER]` console shot in the report.
8. **Video scripts**: fix the affected scenes (both voiceover columns) and run `python docs/video/srt.py`.
   If a scene no longer matches recorded footage, add it under `## Re-record` at the end of the script.
9. **Guide site** (only if it describes the changed thing). Edit in `D:\git\site\switch-hub` on branch
   `kefir-hub`, in the site's own style (Ukrainian, `{% include /inc/btn.txt btn="A" %}`, notices). Link to
   the docs with `{{ site.kefir_hub_docs }}/uk/<page>/#<anchor>`. Labels there are the literal uk.json values.
10. **Verify** (all must pass):
    - `python tests/test_doc_labels_contract.py` → `0 unknown`. Read its warnings. A label "not found as a
      literal in sphaira/" means either its feature is gone (go to step 6, removed) or the label is built at
      run time (fine).
    - `wsl bash -lc '. ~/.venvs/docs/bin/activate && cd /mnt/d/git/dev/sphaira && sh docs/site/build.sh'` →
      `built en`, `built uk`, no WARNING or Aborted. First setup: `python3 -m venv ~/.venvs/docs &&
      ~/.venvs/docs/bin/pip install -r docs/site/requirements.txt`.
    - Site changed: `python tools/docs/check_site_links.py` → `0 broken`.
11. **Commit.** Docs-only: prefix `docs:`, no version bump, one line under `## unreleased` in
    `docs/dev/CHANGELOG.md`. Docs written together with the code change go in the same commit as the code
    (that commit has the version bump). Stage by path, never `git add -A`. The site clone is a separate
    repo: commit there on `kefir-hub`. Never push anything.

## Screenshots (Eden emulator)
Kefir Hub runs in Eden from v0.13.955. All screenshots are taken with the **English** UI and saved as
`docs/site/en/img/<shot-id>.png`. They are the fallback for every language.
1. Build as usual (`test-build` skill), then copy the `.nro` from `build/ReleaseWithInstall/kefir-hub.nro`
   to `E:\Switch\Eden\user\sdmc\switch\kefir-hub.nro`. Eden is portable: `E:\Switch\Eden\user` holds the
   keys, the firmware and the emulated microSD (`sdmc`). Hub config is `sdmc\config\kefir\`.
2. PowerShell: `. tools\docs\eden.ps1; Start-Hub`. On first start, pick English.
3. Navigate with `B <A|B|X|Y|L|R|ZL|ZR|Plus|Minus|L3|R3|Up|Down|Left|Right> [count]`. Take a shot with
   `Shot docs\site\en\img\<id>.png`, which saves the console frame only at 1280x720.
4. **Look at every PNG** (open the image) and check it shows what the marker says: right screen, right item
   focused, no stray popup. Retake if not.
5. Web pages of the Hub web server (Install & Share → Web Server, http://127.0.0.1:80 in Eden):
   `node tools/docs/web-shot.mjs <url> docs\site\en\img\<id>.png`.
6. Test data may be created **only** inside `E:\Switch\Eden\user\sdmc` (dummy files, copies of .nro as
   other apps, folders).

The emulator cannot show: MTP/USB/FTP from a PC, game cards, installed games and saves, real install
queues, Ownfoil, online lists (App Store, Themezer, updater, translations), console-to-console transfer,
TegraExplorer. These are `[USER]` console shots: the owner presses Capture in title mode, and the album
can be pulled from the Hub web page `/album`.

## Never
- Never invent a fact. Unsure → `<!-- TODO(verify): question -->` and list it in the report.
- Never write marketing words (STYLE.md has the banned list), and never use the old `docs/wiki` as a source.
- Never document an item that opens "Coming soon" or code no menu reaches (DOCS-COVERAGE.md marks dead code).
- Never change `en.json`/`uk.json` or code just to make the docs read better. A wrong UI string is a
  product fix: its own commit with a version bump, `tools/i18n-translate/context.json` for ambiguous keys.
- Never wrap a string with `%s`/`%d` or a line break in `[[...]]`; describe it instead.
- Emulator: never send keys with keybd_event/SendKeys/SendInput (they type into the user's foreground app).
  Use only `B`, `Shot`, `Grab` from `tools/docs/eden.ps1`. Never install, delete or move real content, and
  never link accounts, reboot, downgrade, change Kefir Settings, or send anything to the internet.
- Never touch `D:\git\site\switch` (the live site's checkout, release commits land there), never switch
  branches in it, never push. Never commit `build/`.
- Never stop at "docs updated" without running the checks in step 10.

## Report (to the owner, in Ukrainian)
What changed in docs / site / video, shots retaken, `[USER]` shots still needed, new `TODO(verify)`
questions, and any UI text that looks wrong (string + file:line).
