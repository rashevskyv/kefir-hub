"""Print the screenshot list: every <!-- shot: id | what --> in docs/site/en, and which are taken.

    python docs/site/shotlist.py > shotlist.md

A shot is taken when docs/site/<lang>/img/<id>.png exists; the page then shows it instead of
the comment (hooks/ui_labels.py). Screens show the console's UI language, so <lang>/img holds
screenshots made with Kefir Hub set to that language; en/img is the fallback for every language.
A language folder may hold only img/ (no pages yet): its build uses the English text with that
language's labels and screenshots.

Recipe column (docs/site/shots.json, replayed by tools/docs/shoot.ps1 for any language):
recipe = recorded button presses, user = console-only shot (the emulator cannot show it), blank = not recorded.
"""
import json
import re
from pathlib import Path

SITE = Path(__file__).resolve().parent
SHOT = re.compile(r"<!-- shot: ([\w-]+) \|(.*?)-->")

langs = ["en"] + sorted(p.name for p in SITE.iterdir()
                        if p.name != "en" and ((p / "index.md").exists() or (p / "img").is_dir()))
shots_file = SITE / "shots.json"
recipes = json.loads(shots_file.read_text(encoding="utf-8-sig"))["shots"] if shots_file.exists() else {}


def recipe(sid):
    entry = recipes.get(sid) or {}
    return "user" if entry.get("user") else "recipe" if entry.get("steps") else ""


print(f"| Page | Shot id | What the screen must show | Recipe | {' | '.join(langs)} |")
print("|---|---|---|---|" + "---|" * len(langs))
for page in sorted((SITE / "en").rglob("*.md")):
    for m in SHOT.finditer(page.read_text(encoding="utf-8")):
        taken = ["x" if (SITE / lang / "img" / f"{m[1]}.png").exists() else "" for lang in langs]
        print(f"| {page.relative_to(SITE / 'en').as_posix()} | `{m[1]}` | {m[2].strip()} | {recipe(m[1])} | {' | '.join(taken)} |")
