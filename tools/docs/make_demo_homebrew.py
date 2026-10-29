"""Generate the demo homebrew and its virtual folders for the Homebrew folder shots (fictional apps).

    python tools/docs/make_demo_homebrew.py

Writes docs/site/fixtures/sdmc/switch/<app>/<app>.nro: a tiny .nro (header + asset section with an icon and a
NACP, no code: it only has to be listed, never started), and config/kefir/homebrew_folders.txt with the folders
(format: sphaira/include/homebrew_vfolders.hpp) plus a folder icon in config/kefir/homebrew_folders/<id>.jpg.
The .nro files are force-added to git (*.nro is ignored).
"""
import io
import pathlib
import struct

from make_demo_images import FONT, centred, colour
from PIL import Image, ImageDraw

REPO = pathlib.Path(__file__).resolve().parents[2]
SDMC = REPO / "docs/site/fixtures/sdmc"
SWITCH = SDMC / "switch"
KEFIR = SDMC / "config/kefir"

APPS = [
    ("Pixel Painter", "Demo Studio", "1.2.0"),
    ("Save Juggler", "Demo Studio", "0.9.1"),
    ("Retro Toaster", "Crumb Labs", "3.0"),
    ("Goose Run", "Honk Games", "1.0.4"),
]
# id, name, icon text (None: the Hub draws the icons of the folder's apps)
FOLDERS = [(1, "Demo Tools", None), (2, "Emulators", "EMU")]
# app, folder ids (0 = the top level). Pixel Painter and Save Juggler moved to Demo Tools; Retro Toaster
# copied to Emulators (still on the top level); Goose Run has no record: top level only.
MEMBERS = [("Pixel Painter", [1]), ("Save Juggler", [1]), ("Retro Toaster", [0, 2])]

NACP_SIZE = 0x4000
NACP_VERSION_OFFSET = 0x3060
HEADER_SIZE = 0x80  # NroStart (0x10) + NroHeader (0x70)
ASSET_HEADER_SIZE = 0x38


def jpeg(text, seed, size=40):
    img = Image.new("RGB", (256, 256), colour(seed))
    d = ImageDraw.Draw(img)
    centred(d, text, 128 - size // 2, size, 256)
    out = io.BytesIO()
    img.save(out, "JPEG", quality=90)
    return out.getvalue()


def nacp(name, author, version):
    data = bytearray(NACP_SIZE)
    for i in range(16):  # every language entry: name 0x200, author 0x100
        data[i * 0x300:i * 0x300 + len(name.encode())] = name.encode()
        data[i * 0x300 + 0x200:i * 0x300 + 0x200 + len(author.encode())] = author.encode()
    data[NACP_VERSION_OFFSET:NACP_VERSION_OFFSET + len(version)] = version.encode()
    return bytes(data)


def nro(name, author, version):
    icon = jpeg(name, name, 30)
    header = bytearray(HEADER_SIZE)
    header[0x10:0x14] = b"NRO0"
    struct.pack_into("<I", header, 0x18, HEADER_SIZE)  # NroHeader.size: where the asset section starts
    icon_off = ASSET_HEADER_SIZE
    nacp_off = icon_off + len(icon)
    asset = b"ASET" + struct.pack("<I", 0) + struct.pack("<QQQQQQ", icon_off, len(icon), nacp_off, NACP_SIZE, 0, 0)
    return bytes(header) + asset + icon + nacp(name, author, version)


def main():
    for name, author, version in APPS:
        d = SWITCH / name
        d.mkdir(parents=True, exist_ok=True)
        (d / (name + ".nro")).write_bytes(nro(name, author, version))
    lines = [f"F\t{i}\t{n}" for i, n, _ in FOLDERS]
    lines += [f"A\t{i}\t/switch/{app}/{app}.nro" for app, ids in MEMBERS for i in ids]
    (KEFIR / "homebrew_folders").mkdir(parents=True, exist_ok=True)
    (KEFIR / "homebrew_folders.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    for i, n, text in FOLDERS:
        if text:
            (KEFIR / "homebrew_folders" / f"{i}.jpg").write_bytes(jpeg(text, n))
    print("demo homebrew written to", SDMC)


if __name__ == "__main__":
    main()
