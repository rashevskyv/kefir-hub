"""Put Kefir Hub screenshots into the guide site (Jekyll clone, branch kefir-hub).

    python tools/docs/sync_site_shots.py [--site D:/git/site/switch-hub] [--lang uk] [--fallback-en] [--dry-run]

The site marks a picture as {% comment %}shot: <id>{% endcomment %} (same ids as docs/site).
For every marker whose docs/site/<lang>/img/<id>.png exists, this copies the PNG to
<site>/assets/images/switch/hub/<id>.png and replaces the marker with
{% include inc/hub-shot.html id="<id>" %}. Already placed shots get their PNG refreshed.
The site is Ukrainian, so English screenshots are used only with --fallback-en.
"""
import argparse
import re
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MARKER = re.compile(r"\{% comment %\}shot: ([\w-]+)\{% endcomment %\}")
PLACED = re.compile(r'\{% include inc/hub-shot\.html id="([\w-]+)" %\}')

ap = argparse.ArgumentParser()
ap.add_argument("--site", default="D:/git/site/switch-hub")
ap.add_argument("--lang", default="uk")
ap.add_argument("--fallback-en", action="store_true")
ap.add_argument("--dry-run", action="store_true")
args = ap.parse_args()

site = Path(args.site)
dest = site / "assets" / "images" / "switch" / "hub"
if not (site / "_config.yml").exists():
    raise SystemExit(f"not a Jekyll site: {site}")


def source(sid):
    for lang in [args.lang] + (["en"] if args.fallback_en and args.lang != "en" else []):
        png = REPO / "docs" / "site" / lang / "img" / f"{sid}.png"
        if png.exists():
            return png
    return None


placed, refreshed, missing = [], [], []
for page in sorted([*site.glob("_pages/**/*.md"), *site.glob("_includes/**/*")]):
    if not page.is_file():
        continue
    with open(page, encoding="utf-8", newline="") as f:  # keep CRLF/LF as is
        text = f.read()

    def put(m):
        png = source(m[1])
        if not png:
            missing.append(f"{page.relative_to(site).as_posix()}: {m[1]}")
            return m[0]
        if not args.dry_run:
            dest.mkdir(parents=True, exist_ok=True)
            shutil.copy2(png, dest / f"{m[1]}.png")
        placed.append(m[1])
        return f'{{% include inc/hub-shot.html id="{m[1]}" %}}'

    new = MARKER.sub(put, text)
    for sid in PLACED.findall(text):
        png = source(sid)
        if png and not args.dry_run:
            shutil.copy2(png, dest / f"{sid}.png")
        (refreshed if png else missing).append(sid if png else f"{page.relative_to(site).as_posix()}: {sid} (placed, no PNG)")
    if new != text and not args.dry_run:
        with open(page, "w", encoding="utf-8", newline="") as f:
            f.write(new)

print(f"placed {len(placed)}, refreshed {len(refreshed)}, missing {len(missing)}{' (dry run)' if args.dry_run else ''}")
for m in missing:
    print("  missing", m)
