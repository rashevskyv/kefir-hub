"""Print the screenshot list: every <!-- shot: id | what --> in docs/site/en, and which are taken.

    python docs/site/shotlist.py > shotlist.md

A shot is taken when docs/site/<lang>/img/<id>.png exists; the page then shows it instead of
the comment (hooks/ui_labels.py). Screens show the console's UI language, so uk/img holds
screenshots made with Kefir Hub set to Ukrainian; en/img is the fallback for every language.
"""
import re
from pathlib import Path

SITE = Path(__file__).resolve().parent
SHOT = re.compile(r"<!-- shot: ([\w-]+) \|(.*?)-->")

langs = sorted(p.name for p in SITE.iterdir() if (p / "index.md").exists())
print(f"| Page | Shot id | What the screen must show | {' | '.join(langs)} |")
print("|---|---|---|" + "---|" * len(langs))
for page in sorted((SITE / "en").rglob("*.md")):
    for m in SHOT.finditer(page.read_text(encoding="utf-8")):
        taken = ["x" if (SITE / lang / "img" / f"{m[1]}.png").exists() else "" for lang in langs]
        print(f"| {page.relative_to(SITE / 'en').as_posix()} | `{m[1]}` | {m[2].strip()} | {' | '.join(taken)} |")
