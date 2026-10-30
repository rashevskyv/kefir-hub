#!/usr/bin/env python3
"""assets/romfs/bios/bios.json is what the Hub downloads BIOS files from: check its shape.

Every file has a destination, a repo path, a 40-hex sha1 and a size; destinations are unique
per system; every core maps to systems that exist; the Tico folders are the known slugs.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "romfs" / "bios" / "bios.json"
SHA1 = re.compile(r"^[0-9a-f]{40}$")


def main() -> int:
    m = json.loads(MANIFEST.read_text(encoding="utf-8"))
    errors = []
    if not m.get("source", "").startswith("https://raw.githubusercontent.com/Abdess/retrobios/"):
        errors.append("source is not the retrobios raw url")
    ids = set()
    for s in m["systems"]:
        if not s.get("id") or not s.get("name") or not s.get("files"):
            errors.append(f"system without id/name/files: {s.get('id')}")
            continue
        if s["id"] in ids:
            errors.append(f"duplicate system {s['id']}")
        ids.add(s["id"])
        if "tico" in s and not re.fullmatch(r"[a-z0-9-]+", s["tico"]):
            errors.append(f"{s['id']}: bad tico folder {s['tico']!r}")
        dests = set()
        for f in s["files"]:
            if not f.get("dest") or f["dest"].startswith("/") or ".." in f["dest"]:
                errors.append(f"{s['id']}: bad dest {f.get('dest')!r}")
            if f["dest"] in dests:
                errors.append(f"{s['id']}: duplicate dest {f['dest']}")
            dests.add(f["dest"])
            if not f.get("path", "").startswith("bios/"):
                errors.append(f"{s['id']}/{f.get('dest')}: path not under bios/")
            if not SHA1.match(f.get("sha1", "")):
                errors.append(f"{s['id']}/{f.get('dest')}: bad sha1")
            if not isinstance(f.get("size"), int) or f["size"] <= 0:
                errors.append(f"{s['id']}/{f.get('dest')}: bad size")
    for core, systems in m["cores"].items():
        if core != core.lower():
            errors.append(f"core {core} is not lower case")
        for sid in systems:
            if sid not in ids:
                errors.append(f"core {core} maps to unknown system {sid}")
    for e in errors[:20]:
        print("FAIL:", e)
    if errors:
        print(f"{len(errors)} errors")
        return 1
    files = sum(len(s["files"]) for s in m["systems"])
    print(f"bios manifest: {len(ids)} systems, {files} files, {len(m['cores'])} cores: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
