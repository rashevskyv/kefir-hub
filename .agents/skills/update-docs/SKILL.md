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
| Screenshots (per UI language; en = fallback) | `docs/site/<lang>/img/<shot-id>.png` |
| Screenshot recipes (button presses per shot) | `docs/site/shots.json`, fixtures `docs/site/fixtures/sdmc/` |
| Shot list (which ids exist, which are taken) | `python docs/site/shotlist.py` |
| Video scripts + draft subtitles | `docs/video/NN-topic/script.md`, `python docs/video/srt.py` |
| Guide site (Jekyll, Ukrainian), separate clone | `D:\git\site\switch-hub`, branch `kefir-hub`, notes `_rework/REVIEW.md` |
| Emulator + screenshot tools | `tools/docs/eden.ps1` (record), `tools/docs/shoot.ps1` (replay per language), `tools/docs/web-shot.mjs`, `tools/docs/sync_site_shots.py` (to the site) |
| Checks | `tests/test_doc_labels_contract.py`, `docs/site/build.sh`, `tools/docs/check_site_links.py` |
| Local review for the owner | `docs/site/review.ps1` |
| Online preview (proofreading, edit links) | https://customfw.xyz/kefir-hub/ (docs, branch `docs`) and `/guide/` (switch, branch `kefir-hub`); `.github/workflows/docs-preview.yml` |

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

## Screenshots (Eden emulator, any language)
Kefir Hub runs in Eden from v0.13.955. Screenshots are per language: `docs/site/<lang>/img/<shot-id>.png`
(`en/img` is the fallback for every language). A screenshot is made from a **recipe**: the button presses
from a fresh Hub launch to the screen, stored in `docs/site/shots.json`. A recipe holds no labels, so one
recipe gives the shot in every language. Record once, replay for any language.
1. Build the **DocsDemo** preset (`cmake --preset DocsDemo && cmake --build --preset DocsDemo`, same WSL setup as the
   `test-build` skill; plan Phase S). It is the normal app plus fictional content from
   `docs/site/fixtures/sdmc/config/kefir/demo/` (meme games with covers, saves, network replies) and frozen demo
   scenes; release builds never contain it. `shoot.ps1` copies `build/DocsDemo/kefir-hub.nro` into Eden by itself.
   Eden is portable: `E:\Switch\Eden\user` holds the keys, the firmware and the emulated microSD (`sdmc`).
2. **Record a recipe** (new or changed screen). PowerShell: `. tools\docs\eden.ps1; Set-HubLang en; Start-Hub`,
   then `Lang en` (fresh main screen, the state every recipe starts from), `Rec <id>`, navigate with `B <A|B|X|Y|L|R|ZL|ZR|Plus|Minus|L3|R3|Up|Down|Left|Right> [count]` and
   `W <seconds>` for loading, and finish with `Shot docs\site\en\img\<id>.png`. Shot saves the frame
   (1280x720) and the recipe. Recipes start from the main screen right after `Lang <code>`: if you navigated
   before `Rec`, run `Lang en` again and record from the first press. Input goes through a file the DocsDemo Hub
   reads (`sdmc/config/kefir/demo/input.txt`), so Eden works in the background and needs no clicks; a non-demo
   .nro does not read it (`B`/`Shot` time out). Steps run once at launch for every shot (a first-run
   dialog) go into `"startup"` in shots.json. A screen that only a demo scene shows (install queue, MTP/FTP
   progress, ...) gets `"scene": "<name>"` in its entry; `shoot.ps1` sets `[demo] scene=<name>` before launch.
3. **Replay for languages**: `powershell -ExecutionPolicy Bypass -File tools\docs\shoot.ps1 -Lang uk,en`
   (add `-Only id1,id2` or `-Missing`; `-List` shows recipe status). It launches Eden once (again only for
   another demo scene), runs `"startup"`, then for each shot loops the languages: `Lang <code>`, the recipe,
   `docs/site/<lang>/img/<id>.png`. Codes = file names in `assets/romfs/i18n/` (en, uk, de, es419, ...).
   A new docs language needs no pages to get its shots: a folder `docs/site/<lang>/` with only `img/` builds
   with English text, that language's labels and its screenshots.
4. **Look at every PNG** (open the image) and check it shows what the marker says in that language: right
   screen, right item focused, no stray popup, no clipped text. Retake (re-record if the menu changed).
5. Test data lives in `docs/site/fixtures/sdmc/` (copied into the Eden sdmc by shoot.ps1; see its README).
   Add dummy files there instead of editing the sdmc by hand, so every language shows the same content.
6. Web pages of the Hub web server (Install & Share → Web Server, http://127.0.0.1:80 in Eden):
   `node tools/docs/web-shot.mjs <url> docs\site\<lang>\img\<id>.png` (Hub language = page language).
7. Status of every marker: `python docs/site/shotlist.py` (columns: recipe, and taken per language).
8. **Guide site**: `python tools/docs/sync_site_shots.py` copies `docs/site/uk/img` shots into
   `D:\git\site\switch-hub\assets\images\switch\hub\` and turns each `{% comment %}shot: id{% endcomment %}`
   that has a PNG into `{% include inc/hub-shot.html id="id" %}`. Commit both in the site clone.

Without the DocsDemo build the emulator cannot show: MTP/USB/FTP from a PC, game cards, installed games and saves, real install
queues, Ownfoil, online lists (App Store, Themezer, updater, translations), console-to-console transfer,
TegraExplorer. Phase S covers them with demo data and scenes. What stays impossible (screens of the PC
side: Windows Explorer, an FTP client) is marked `"<id>": {"user": true}` in shots.json: the owner takes them
on the PC. A console screen the demo cannot fake is also `user`: the owner sets the Hub language, presses
Capture in title mode, and pulls the album from the Hub web page `/album`. Name them `docs/site/<lang>/img/<id>.png`.

## Never
- Never invent a fact. Unsure → `<!-- TODO(verify): question -->` and list it in the report.
- Never write marketing words (STYLE.md has the banned list), and never use the old `docs/wiki` as a source.
- Never document an item that opens "Coming soon" or code no menu reaches (DOCS-COVERAGE.md marks dead code).
- Never change `en.json`/`uk.json` or code just to make the docs read better. A wrong UI string is a
  product fix: its own commit with a version bump, `tools/i18n-translate/context.json` for ambiguous keys.
- Never wrap a string with `%s`/`%d` or a line break in `[[...]]`; describe it instead.
- Emulator: never send keys with keybd_event/SendKeys/SendInput (they type into the user's foreground app).
  Use only `B`, `W`, `Lang`, `Rec`, `Shot`, `Grab`, `Set-HubLang` from `tools/docs/eden.ps1` and `tools/docs/shoot.ps1`. Never install, delete or move real content, and
  never link accounts, reboot, downgrade, change Kefir Settings, or send anything to the internet.
- Never touch `D:\git\site\switch` (the live site's checkout, release commits land there), never switch
  branches in it, never push. Never commit `build/`.
- Never stop at "docs updated" without running the checks in step 10.

## Report (to the owner, in Ukrainian)
What changed in docs / site / video, shots retaken, `[USER]` shots still needed, new `TODO(verify)`
questions, and any UI text that looks wrong (string + file:line).
