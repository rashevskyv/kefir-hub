#!/usr/bin/env python3
"""Copy the file-browser path notes into en.json.

The notes live as plain strings in sphaira/include/ui/menus/filebrowser_path_notes.hpp
(a constexpr table, so they cannot be "..."_i18n literals and translate.py's sync
does not see them). Run this, then translate.py fills the other languages.

    python tools/i18n-translate/add_path_notes.py
"""
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HEADER = os.path.join(ROOT, "sphaira", "include", "ui", "menus", "filebrowser_path_notes.hpp")
EN = os.path.join(ROOT, "assets", "romfs", "i18n", "en.json")
ROW = re.compile(r'^\s*\{"([^"]*)",\s*"([^"]*)",\s*"((?:[^"\\]|\\.)*)"\},', re.M)


def notes():
    with open(HEADER, encoding="utf-8") as f:
        return [m.group(3) for m in ROW.finditer(f.read())]


def main():
    raw = open(EN, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in raw else "\n"
    en = json.loads(raw)
    missing = [n for n in dict.fromkeys(notes()) if n not in en]
    if not missing:
        print("en.json already has every path note")
        return
    lines = raw.rstrip().split(nl)
    assert lines[-1].strip() == "}"
    body = lines[:-1]
    body[-1] = body[-1].rstrip()
    if not body[-1].endswith(","):
        body[-1] += ","
    indent = re.match(r"\s*", body[-1]).group(0) or "  "
    for n in missing:
        body.append(f"{indent}{json.dumps(n)}: {json.dumps(n)},")
    body[-1] = body[-1].rstrip(",")
    out = nl.join(body + ["}"]) + nl
    json.loads(out)
    open(EN, "w", encoding="utf-8", newline="").write(out)
    print(f"en.json: added {len(missing)} path note(s)")


if __name__ == "__main__":
    main()
