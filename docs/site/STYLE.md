# Writing the Kefir Hub docs

Pages live in `docs/site/en/` (source) and `docs/site/uk/` (Ukrainian). Same file names in every language.
Other languages are machine-translated from `en/` later; a missing page falls back to English.

## Reader
A Switch owner with Kefir installed. Not a developer. They want to get one thing done.
Write for that: what it is (one sentence), where to find it, numbered steps, what can go wrong.

## UI labels: `[[Label]]`
- Every button, menu item, option and dialog title the console shows is written as `[[Label]]`,
  where `Label` is the exact key from `assets/romfs/i18n/en.json` (keys are the English strings).
- The build replaces it with the string in the page's language, in bold — the same text the user sees on screen.
  So a Ukrainian page also writes `[[Saves]]`, never «Збереження».
- Check every label: `grep -F '"Label":' assets/romfs/i18n/en.json`. Not in en.json → it is not a UI label;
  do not wrap it (and re-check the code, the label was probably renamed).
- Never wrap strings with printf specifiers (`%s`, `%d`) — describe them in prose instead.
- Menu paths: `[[Tools]] → [[Game Tools]] → [[Saves]]`.
- Controller buttons are plain bold, not labels: **A**, **B**, **X**, **Y**, **L**, **R**, **ZL**, **ZR**,
  **+**, **−**, **L3**, **R3**, **D-pad**. Write "press **Y**", the way the footer shows it.

## Facts
- Every statement comes from the code (`sphaira/source/...`), not from the old wiki (`docs/wiki/`), which is
  partly wrong and full of marketing. Use the wiki only as a hint of what exists.
- Not sure how something behaves → write `<!-- TODO(verify): question -->` and move on. Never guess.
- Items that open "Coming soon" are not documented (one line "planned" at most).
- SD paths are real paths: config `/config/kefir/`, log `/config/kefir/log.txt` — verify in code.

## Tone
- Plain, short sentences. Second person, imperative: "Open …", "Press **A**".
- Banned: seamless, powerful, high-performance, blazing, robust, feature-packed, effortless, cutting-edge,
  "simply", "just", exclamation marks, emoji.
- No internals the user cannot act on (IPC names, thread counts, protocol names like SPHQ) unless they explain
  something the user sees. Developer facts belong in `docs/dev/`.
- Warnings that prevent data loss or bans use admonitions: `!!! warning`. Tips: `!!! tip`. Use sparingly.

## Page shape
```
# Page title (plain words or [[Label]])

One or two sentences: what this is for.

**Where:** [[Tools]] → [[Game Tools]] → [[Saves]]

<!-- shot: saves-list | Saves list, two games, one selected -->

## <Task, as a verb phrase: "Back up a save">
1. …
2. …

## Options
| Option | What it does | Default |
|---|---|---|
| [[Label]] | … | Off |

## Problems
**Symptom.** Cause and what to do.
```
- `<!-- shot: id | what the screen must show -->` marks a screenshot to take. `id` is kebab-case, unique across the
  site, prefixed with the page name. These comments feed the shot list for screenshots and videos; place one
  wherever a picture helps more than text (each screen the user must find, each confirmation dialog).
- Headings are tasks or nouns, not marketing. Heading ids are automatic: every language gets the ids of the
  English page, so a translation must keep the same headings in the same order (the build checks it).
- Link other pages relatively: `[Saves](saves.md#restore-a-save)`.
- Length: as long as the tasks need, no longer. A page that only lists options is fine.

## Ukrainian
Natural Ukrainian, not a calque of English: «Відкрийте», «Натисніть **A**», «карта пам'яті» (not «SD-карта» in prose,
except paths), «консоль». The same structure, headings and shot comments as the English page.
Labels stay `[[English key]]`; build the sentence so a bold nominative label reads naturally
(«Відкрийте [[Saves]]», «пункт [[Restore]]»).
