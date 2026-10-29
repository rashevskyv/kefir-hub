"""Generate the demo homebrew folders for the Homebrew folder shots (fictional apps, no third-party files).

    python tools/docs/make_demo_homebrew.py

Writes docs/site/fixtures/sdmc/switch/<group>/...: each app is a tiny .nro (header + asset section with an
icon and a NACP, no code: it only has to be listed, never started). "Demo Tools" also gets a folder icon
(.hubicon.jpg, see FolderIconPath in sphaira/include/ui/menus/homebrew_internal.hpp).
"""
import io
import pathlib
import struct

from make_demo_images import FONT, centred, colour
from PIL import Image, ImageDraw

REPO = pathlib.Path(__file__).resolve().parents[2]
SWITCH = REPO / "docs/site/fixtures/sdmc/switch"

APPS = [
    ("Demo Tools/Pixel Painter", "Pixel Painter", "Demo Studio", "1.2.0"),
    ("Demo Tools/Save Juggler", "Save Juggler", "Demo Studio", "0.9.1"),
    ("Demo Tools/Emulators/Retro Toaster", "Retro Toaster", "Crumb Labs", "3.0"),
    ("Demo Games/Goose Run", "Goose Run", "Honk Games", "1.0.4"),
]
FOLDER_ICONS = {"Demo Tools": "TOOLS"}

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
    for folder, name, author, version in APPS:
        d = SWITCH / folder
        d.mkdir(parents=True, exist_ok=True)
        (d / (pathlib.Path(folder).name + ".nro")).write_bytes(nro(name, author, version))
    for folder, text in FOLDER_ICONS.items():
        (SWITCH / folder / ".hubicon.jpg").write_bytes(jpeg(text, folder))
    print("demo homebrew written to", SWITCH)


if __name__ == "__main__":
    main()
