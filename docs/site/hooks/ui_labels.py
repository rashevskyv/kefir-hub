"""[[Label]] in the docs -> the product's own UI string for the build language.

Labels are keys of assets/romfs/i18n/en.json, so the docs show exactly what the console
shows, in every language the app ships. Body text gets the label in bold; headings and nav
section titles get it plain. A label that is not in en.json fails the build.

Heading ids: every heading gets the id of the matching heading of the English page (same
position), so `page.md#anchor` links work in every language. A translated page must keep the
English heading structure; a mismatch fails the strict build.
"""
import html
import json
import logging
import os
import re
from pathlib import Path

from markdown.extensions.toc import slugify
from mkdocs.exceptions import PluginError

SITE = Path(__file__).resolve().parents[1]
I18N = SITE.parents[1] / "assets" / "romfs" / "i18n"
LABEL = re.compile(r"\[\[([^\[\]\n]+)\]\]")
HEADING = re.compile(r"^(#{1,6}) (.*?)\s*$", re.M)
ATTR_ID = re.compile(r"\s*\{#([^}\s]+)\}$")
SHOT = re.compile(r"<!-- shot: ([\w-]+) \|(.*?)-->")
log = logging.getLogger("mkdocs.hooks.ui_labels")


def _load(code):
    path = I18N / f"{code}.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}


LANG = os.environ.get("DOCS_LANG", "en")
_en = _load("en")
_loc = _load(LANG)


def label(key, where):
    if key not in _en:
        raise PluginError(f"{where}: [[{key}]] is not a UI string in en.json")
    return _loc.get(key) or _en[key]


def _nav(items):
    out = []
    for item in items:
        if isinstance(item, dict):
            item = {LABEL.sub(lambda m: label(m[1], "nav"), k): (_nav(v) if isinstance(v, list) else v)
                    for k, v in item.items()}
        out.append(item)
    return out


def _heading_ids(markdown):
    """Ids of the headings of an English page, as the toc extension would make them."""
    ids, seen = [], {}
    for m in HEADING.finditer(markdown):
        explicit = ATTR_ID.search(m[2])
        if explicit:
            ids.append(explicit[1])
            continue
        text = LABEL.sub(lambda l: _en.get(l[1], l[1]), m[2]).replace("`", "").replace("*", "")
        slug = slugify(text, "-")
        n = seen.get(slug, 0)
        seen[slug] = n + 1
        ids.append(slug if n == 0 else f"{slug}_{n}")
    return ids


def on_config(config):
    config["nav"] = _nav(config["nav"] or [])
    return config


def on_page_markdown(markdown, page, config, files):
    where = page.file.src_uri
    en_page = SITE / "en" / where
    ids = _heading_ids(en_page.read_text(encoding="utf-8") if en_page.exists() else markdown)
    count = len(HEADING.findall(markdown))
    if count != len(ids):
        log.warning(f"{LANG}/{where}: {count} headings, English page has {len(ids)}; anchors will not match")
    it = iter(ids)

    def heading(m):
        text = ATTR_ID.sub("", m[2])
        text = LABEL.sub(lambda l: label(l[1], where), text)
        hid = next(it, None)
        return f"{m[1]} {text} {{#{hid}}}" if hid else f"{m[1]} {text}"

    markdown = HEADING.sub(heading, markdown)
    markdown = SHOT.sub(lambda m: _shot(m, page, config), markdown)
    # <strong> rather than ** so a label inside bold text does not break the emphasis.
    return LABEL.sub(lambda m: f"<strong>{html.escape(label(m[1], where))}</strong>", markdown)


def _shot(m, page, config):
    """<!-- shot: id | what --> becomes the picture once img/<id>.png exists (uk/img overrides en/img)."""
    if not (Path(config["docs_dir"]) / "img" / f"{m[1]}.png").exists():
        return m[0]
    up = "../" * page.file.src_uri.count("/")
    return f"![{m[2].strip()}]({up}img/{m[1]}.png)"
