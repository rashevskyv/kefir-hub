"""DOCS_DEMO fixtures (docs/site/fixtures/sdmc) are real data the demo build reads; check they stay readable.

titles.json parses and its ids, icons and save owners are what sphaira/source/demo/ expects; every http fixture that
is not an image or a plain text page parses as JSON; the save backups are zip archives with a 128-byte
.nx_save_meta.bin of the right magic and owner; shots.json parses and every shot is a recipe or `user`.
Standard library only (runs in tests/run.sh).
"""
import json
import pathlib
import struct
import sys
import zipfile

REPO = pathlib.Path(__file__).resolve().parents[1]
SDMC = REPO / "docs/site/fixtures/sdmc"
DEMO = SDMC / "config/kefir/demo"
EDEN_PROFILES = 3          # Pixel, Kotyk, Guest (tools/docs/make_demo_backups.py)
NX_SAVE_META_MAGIC = 0x4A4B5356
TEXT_FIXTURES = {"changelog_full"}  # served as text, not JSON
IMAGE_SUFFIXES = (".png", ".jpg", "@jpg")

failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)


def jpeg_size(data):
    """(width, height) from the first SOF marker, or None."""
    if data[:2] != b"\xff\xd8":
        return None
    i = 2
    while i + 4 <= len(data):
        if data[i] != 0xFF:
            return None
        marker = data[i + 1]
        length = struct.unpack(">H", data[i + 2:i + 4])[0]
        if marker in (0xC0, 0xC1, 0xC2):
            h, w = struct.unpack(">HH", data[i + 5:i + 9])
            return w, h
        i += 2 + length
    return None


def check_titles():
    titles = json.loads((DEMO / "titles.json").read_text(encoding="utf-8"))["titles"]
    ids = [t["id"] for t in titles]
    check(len(ids) == len(set(ids)), "titles.json: duplicate ids")
    for t in titles:
        tid = t["id"]
        check(len(tid) == 16 and all(c in "0123456789ABCDEF" for c in tid), f"{tid}: not 16 upper hex digits")
        check(tid.endswith("000"), f"{tid}: not a base application id")
        check("en" in t["name"], f"{tid}: no English name (the fallback for every UI language)")
        check(t["storage"] in ("sd", "nand", "gamecard"), f"{tid}: unknown storage {t['storage']}")
        icon = DEMO / t["icon"]
        check(icon.is_file(), f"{tid}: missing icon {t['icon']}")
        if icon.is_file():
            check(jpeg_size(icon.read_bytes()) == (256, 256), f"{tid}: {t['icon']} is not a 256x256 JPEG")
        for s in t["saves"]:
            check(0 <= s["user"] < EDEN_PROFILES, f"{tid}: save owner index {s['user']} has no Eden profile")
        if "build_id" in t:
            check(len(t["build_id"]) == 16, f"{tid}: build_id is not 16 hex digits")
    return {t["id"] for t in titles}


def check_http():
    count = 0
    for f in (DEMO / "http").rglob("*"):
        if not f.is_file() or f.name.endswith(IMAGE_SUFFIXES) or f.name in TEXT_FIXTURES:
            continue
        count += 1
        try:
            json.loads(f.read_text(encoding="utf-8"))
        except ValueError as e:
            failures.append(f"http fixture {f.relative_to(DEMO)}: {e}")
    check(count > 0, "no http fixtures found")


def check_backups(ids):
    zips = list((SDMC / "dumps").rglob("*.zip"))
    check(zips, "no demo save backups (run tools/docs/make_demo_backups.py)")
    for z in zips:
        with zipfile.ZipFile(z) as a:
            check(a.comment.startswith(b"sphaira v"), f"{z.name}: not marked as a Kefir Hub backup")
            names = a.namelist()
            check(".nx_save_meta.bin" in names, f"{z.name}: no .nx_save_meta.bin")
            check(len(names) > 1, f"{z.name}: no save payload")
            if ".nx_save_meta.bin" in names:
                meta = a.read(".nx_save_meta.bin")
                check(len(meta) == 128, f"{z.name}: meta is {len(meta)} bytes, not 128")
                magic, _version, app_id = struct.unpack("<IIQ", meta[:16])
                check(magic == NX_SAVE_META_MAGIC, f"{z.name}: wrong meta magic")
                check(f"{app_id:016X}" in ids, f"{z.name}: backup of {app_id:016X}, not a demo game")
                check(meta[16:32] != bytes(16), f"{z.name}: account save without an owner uid")


def check_shots():
    shots = json.loads((REPO / "docs/site/shots.json").read_text(encoding="utf-8-sig"))["shots"]
    for sid, e in shots.items():
        check(e.get("user") or e.get("scene") or e.get("steps"), f"shots.json {sid}: neither recipe nor user")


ids = check_titles()
check_http()
check_backups(ids)
check_shots()

if failures:
    for f in failures:
        print("FAIL", f)
    sys.exit(1)
print("test_demo_fixtures_contract: ok")
