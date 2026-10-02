"""Check every {{ site.kefir_hub_docs }}/<path>#<anchor> link of the guide site against the built docs.

    python tools/docs/check_site_links.py [site_dir]      (default D:\\git\\site\\switch-hub)

Build the docs first (docs/site/build.sh). Exit code 1 when a page or an anchor is missing.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "build" / "docs-site"
LINK = re.compile(r"\{\{\s*site\.kefir_hub_docs\s*\}\}/([^\s)\"'#]*)(?:#([^\s)\"']+))?")


def main():
    site = Path(sys.argv[1] if len(sys.argv) > 1 else r"D:\git\site\switch-hub")
    files = [*site.glob("_pages/**/*.md"), *site.glob("_includes/**/*"), *site.glob("_data/*.yml"), *site.glob("*.txt")]
    bad = n = 0
    for f in files:
        if not f.is_file():
            continue
        for i, line in enumerate(f.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for path, anchor in LINK.findall(line):
                n += 1
                page = DOCS / path.strip("/") / "index.html"
                if not page.exists():
                    print(f"{f.relative_to(site)}:{i}: no page {path}")
                    bad += 1
                elif anchor and f'id="{anchor}"' not in page.read_text(encoding="utf-8"):
                    print(f"{f.relative_to(site)}:{i}: no anchor {path}#{anchor}")
                    bad += 1
    print(f"{n} links, {bad} broken")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
