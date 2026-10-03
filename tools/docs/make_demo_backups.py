"""Write the save backups the DOCS_DEMO Backups tab shows, in the Kefir Hub archive format.

    python tools/docs/make_demo_backups.py [--profiles <Eden profiles.dat>]

Output: docs/site/fixtures/sdmc/dumps/<title id>/Account/<owner> - YYYY.MM.DD @ hh.mm.ss.zip. Menu::CollectBackups
(save_backup_pub.cpp) looks in the folder named after the game and in the one named after its title id; the demo
game names change with the UI language, so only the title id folder is found in every language. Each zip: `.nx_save_meta.bin` (NXSaveMeta, 128 bytes,
save_paths.hpp), one fictional payload file, zip comment "sphaira v..." (read as a Kefir Hub backup).
The owner uid is the Eden profile at the save's `user` index, read from Eden's profiles.dat: run this again
after the Eden profiles change. Stonks Tycoon gets three dated backups (Pixel), Borshch Royale one per owner.
"""
import argparse
import datetime
import json
import pathlib
import shutil
import struct
import zipfile

REPO = pathlib.Path(__file__).resolve().parents[2]
DEMO = REPO / "docs/site/fixtures/sdmc/config/kefir/demo/titles.json"
OUT = REPO / "docs/site/fixtures/sdmc/dumps"
PROFILES = pathlib.Path(r"E:\Switch\Eden\user\nand\system\save\8000000000000010\su\avators\profiles.dat")

NX_SAVE_META_MAGIC = 0x4A4B5356
FS_SAVE_DATA_TYPE_ACCOUNT = 1

# (title id, user index, backup date); the Saves tab groups them per game and owner.
BACKUPS = [
    ("0100DE0000010000", 0, datetime.datetime(2026, 9, 12, 18, 30, 5)),
    ("0100DE0000010000", 0, datetime.datetime(2026, 9, 20, 21, 4, 41)),
    ("0100DE0000010000", 0, datetime.datetime(2026, 10, 1, 9, 15, 12)),
    ("0100DE0000030000", 0, datetime.datetime(2026, 9, 28, 19, 2, 33)),
    ("0100DE0000030000", 2, datetime.datetime(2026, 9, 30, 20, 47, 9)),
]


def eden_users(path):
    """[(uid bytes as AccountUid, nickname)] in Eden's profile order (yuzu ProfileDataRaw)."""
    data = path.read_bytes()
    users = []
    for i in range(8):
        off = 0x10 + i * 0xC8
        uid = data[off:off + 16]
        if uid == bytes(16):
            continue
        users.append((uid, data[off + 0x28:off + 0x48].split(b"\0")[0].decode("utf-8")))
    return users


def nx_save_meta(app_id, uid, when, size):
    attr = struct.pack("<Q16sQBBHIQQQ", app_id, uid, 0, FS_SAVE_DATA_TYPE_ACCOUNT, 0, 0, 0, 0, 0, 0)
    ts = int(when.replace(tzinfo=datetime.timezone.utc).timestamp())
    meta = struct.pack("<II", NX_SAVE_META_MAGIC, 1) + attr + struct.pack(
        "<QQIIqqQQ", app_id, ts, 0, 0, size, size // 4, 1, size)
    assert len(meta) == 128
    return meta


def safe_name(name):
    return "".join("_" if c in '\\/:*?"<>|' else c for c in name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profiles", type=pathlib.Path, default=PROFILES)
    args = ap.parse_args()

    titles = {t["id"]: t for t in json.loads(DEMO.read_text(encoding="utf-8"))["titles"]}
    users = eden_users(args.profiles)
    shutil.rmtree(OUT, ignore_errors=True)

    for tid, user, when in BACKUPS:
        t = titles[tid]
        uid, owner = users[user]
        size = next(s["size"] for s in t["saves"] if s["user"] == user)
        folder = OUT / tid / "Account"
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / f"{safe_name(owner)} - {when:%Y.%m.%d @ %H.%M.%S}.zip"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
            z.writestr(zipfile.ZipInfo(".nx_save_meta.bin", when.timetuple()[:6]), nx_save_meta(int(tid, 16), uid, when, size))
            z.writestr(zipfile.ZipInfo("progress.sav", when.timetuple()[:6]),
                       f"{t['name']['en']} - fictional demo save of {owner}, {when:%Y-%m-%d}\n".encode())
            z.comment = b"sphaira v0.13 [docs demo]"
        print(path.relative_to(REPO))


if __name__ == "__main__":
    main()
