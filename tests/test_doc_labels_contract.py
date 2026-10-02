"""The user docs (docs/site) name UI elements as [[Label]], resolved from the app's i18n files.

Every label must be a key of en.json: a label that is not there names a button the app does
not have (or one that was renamed), and the docs build would fail on it anyway. Labels the
docs use but uk.json lacks are listed as a warning: they render in English on the uk site.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
I18N = ROOT / "assets" / "romfs" / "i18n"
SITE = ROOT / "docs" / "site"
LABEL = re.compile(r"\[\[([^\[\]\n]+)\]\]")


def main():
    en = json.loads((I18N / "en.json").read_text(encoding="utf-8"))
    uk = json.loads((I18N / "uk.json").read_text(encoding="utf-8"))
    files = [SITE / "mkdocs.yml", *sorted(SITE.glob("*/**/*.md"))]
    bad, used = [], set()
    for path in files:
        text = path.read_text(encoding="utf-8")
        for n, line in enumerate(text.splitlines(), 1):
            for key in LABEL.findall(line):
                used.add(key)
                if key not in en:
                    bad.append(f"{path.relative_to(ROOT)}:{n}: [[{key}]] not in en.json")
    for line in bad:
        print(line)
    missing_uk = sorted(k for k in used if k in en and not uk.get(k))
    if missing_uk:
        print(f"warning: {len(missing_uk)} doc labels missing from uk.json: {missing_uk[:10]}")
    print(f"doc labels: {len(used)} used, {len(bad)} unknown")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
