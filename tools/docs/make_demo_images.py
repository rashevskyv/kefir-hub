"""Generate the simple pictures the DOCS_DEMO network fixtures serve (no third-party art).

    python tools/docs/make_demo_images.py

App Store: http/switch.cdn.fortheusers.org/packages/<name>/icon.png (256x256) and screen.png (1280x720)
for every package in repo.json. Themezer: http/img.themezer.net/demo/<id>@jpg (16:9 previews; the Hub asks for "@jpg" URLs) for THEMES,
the same list http/api.themezer.net/graphql.* returns.
"""
import colorsys
import json
import pathlib
import zlib

from PIL import Image, ImageDraw, ImageFont

REPO = pathlib.Path(__file__).resolve().parents[2]
HTTP = REPO / "docs/site/fixtures/sdmc/config/kefir/demo/http"
FONT = "C:/Windows/Fonts/arialbd.ttf"


def colour(seed, s=0.55, v=0.85):
    r, g, b = colorsys.hsv_to_rgb((zlib.crc32(seed.encode()) % 360) / 360, s, v)
    return int(r * 255), int(g * 255), int(b * 255)


def centred(d, text, y, size, w, fill=(255, 255, 255)):
    font = ImageFont.truetype(FONT, size)
    while d.textlength(text, font=font) > w - 24 and size > 12:
        size -= 2
        font = ImageFont.truetype(FONT, size)
    d.text(((w - d.textlength(text, font=font)) / 2, y), text, font=font, fill=fill, stroke_width=3, stroke_fill=(20, 20, 30))


def gradient(size, seed):
    w, h = size
    top, bottom = colour(seed), colour(seed + "!", 0.7, 0.45)
    im = Image.new("RGB", size)
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / h
        d.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return im, d


def tile(size, seed, title, sub=""):
    w, h = size
    im, d = gradient(size, seed)
    r = min(w, h) // 4
    d.ellipse([w / 2 - r, h / 2 - r, w / 2 + r, h / 2 + r], fill=colour(seed + "o", 0.35, 0.95), outline=(20, 20, 30), width=4)
    letter = ImageFont.truetype(FONT, int(r * 1.2))
    d.text((w / 2, h / 2), title[0], font=letter, anchor="mm", fill=colour(seed, 0.8, 0.5), stroke_width=2, stroke_fill=(20, 20, 30))
    centred(d, title, h * 0.08, max(18, h // 9), w)
    if sub:
        centred(d, sub, h * 0.80, max(14, h // 14), w)
    return im


def screen(seed, title, sub):
    """1280x720 "screenshot": the App Store page shows a centre band, so the text sits in the middle."""
    w, h = 1280, 720
    im, d = gradient((w, h), seed)
    for i in range(14):
        x = (zlib.crc32(f"{seed}{i}x".encode()) % w)
        y = (zlib.crc32(f"{seed}{i}y".encode()) % h)
        r = 20 + zlib.crc32(f"{seed}{i}r".encode()) % 50
        d.rounded_rectangle([x - r, y - r, x + r, y + r], radius=8, fill=colour(f"{seed}{i}", 0.5, 0.9), outline=(20, 20, 30), width=3)
    centred(d, title, h * 0.36, 96, w)
    centred(d, sub, h * 0.54, 40, w)
    return im


def appstore():
    repo = json.loads((HTTP / "switch.cdn.fortheusers.org/repo.json").read_text(encoding="utf-8"))
    for p in repo["packages"]:
        out = HTTP / "switch.cdn.fortheusers.org/packages" / p["name"]
        out.mkdir(parents=True, exist_ok=True)
        tile((256, 256), p["name"], p["title"]).save(out / "icon.png")
        screen(p["name"], p["title"], p["description"]).save(out / "screen.png")
        print(out.relative_to(REPO))


# (hexId, name, creator) - keep in step with the themezer graphql fixture.
THEMES = [
    ("a1", "Midnight Borshch", "Smetana"), ("a2", "Stonks Green", "LineGoesUp"), ("a3", "Cozy Kotyk", "Meow"),
    ("a4", "Pixel Night", "8bitFox"), ("a5", "Sunny Mood", "Calm"), ("a6", "Retro Wave", "Synth"),
    ("a7", "Minimal Mint", "Leaf"), ("a8", "Lava Lamp", "Groovy"), ("a9", "Deep Ocean", "Wave"),
    ("aa", "Paper Craft", "Origami"), ("ab", "Neon Arcade", "Coin"), ("ac", "Autumn Leaves", "Maple"),
]


# POST body hash of the first Themezer page (default sort), as the Hub log printed it ("[demo] http ... graphql.<hash>").
THEMEZER_PAGE1 = "graphql.fc2832eb"


def themezer():
    nodes = [{"hexId": h, "name": n, "creator": {"username": c},
              "collagePreview": {"thumbUrl": f"https://img.themezer.net/demo/{h}@jpg", "hdUrl": f"https://img.themezer.net/demo/{h}@jpg"}}
             for h, n, c in THEMES]
    reply = {"data": {"switch": {"packs": {"nodes": nodes, "pageInfo": {"page": 1, "pageCount": 3, "itemCount": 36, "limit": len(nodes)}}}}}
    api = HTTP / "api.themezer.net"
    api.mkdir(parents=True, exist_ok=True)
    (api / THEMEZER_PAGE1).write_text(json.dumps(reply, indent=1), encoding="utf-8")
    out = HTTP / "img.themezer.net/demo"
    out.mkdir(parents=True, exist_ok=True)
    for hex_id, name, creator in THEMES:
        tile((640, 360), hex_id + name, name, "by " + creator).convert("RGB").save(out / f"{hex_id}@jpg", format="JPEG", quality=85)
    print(out.relative_to(REPO))


# Ownfoil demo shop: the saved server "Home Shop" (192.168.0.20:8465, fixtures config/sphaira/ownfoil.ini) sells
# games the console does not have, so "New games" lists them. "Living Room Shop" (192.168.0.42) is the one the
# DOCS_DEMO discovery finds; it only answers the handshake.
SHOP = [
    ("0100DE0000110000", "Goose Delivery Service", "Honk Works", "1.2.0"),
    ("0100DE0000120000", "Bread Knight", "Crust Games", "2.0.1"),
    ("0100DE0000130000", "Space Hamster", "Wheel Studio", "1.0.0"),
    ("0100DE0000140000", "Sock Hunter", "Laundry Day", "1.4.3"),
    ("0100DE0000150000", "Moon Bakery", "Crater Cakes", "3.1.0"),
    ("0100DE0000160000", "Rubber Duck Debugger", "Quack Labs", "0.9.2"),
    ("0100DE0000170000", "Cat Cafe Tycoon", "Meow", "1.1.0"),
    ("0100DE0000180000", "Tiny Tractor 2026", "Field Day", "1.0.5"),
]
SHOP_HOST = HTTP / "192.168.0.20_8465"
# POST body hash of the first catalog page (New games, NAME asc, 24 per page) from the Hub log.
SHOP_PAGE1 = "graphql.ae8ef18a"
# POST body hash of the title page of SHOP[0] (the hash covers the title id).
SHOP_TITLE0 = "graphql.ab6f757f"


def ownfoil():
    out = SHOP_HOST / "demo/icons"
    out.mkdir(parents=True, exist_ok=True)
    for tid, name, publisher, _ in SHOP:
        tile((256, 256), tid, name).save(out / f"{tid}.jpg", quality=88)
    reply = {"data": {"apps": {"total": len(SHOP), "items": shop_items()}}}
    (SHOP_HOST / SHOP_PAGE1).write_text(json.dumps(reply, indent=1), encoding="utf-8")
    (SHOP_HOST / SHOP_TITLE0).write_text(json.dumps(shop_title(SHOP[0]), indent=1), encoding="utf-8")
    shots = SHOP_HOST / "demo/screens"
    shots.mkdir(parents=True, exist_ok=True)
    screen(SHOP[0][0], SHOP[0][1], "Every parcel arrives. Eventually.").save(shots / f"{SHOP[0][0]}.jpg", quality=85)
    print(out.relative_to(REPO))


def shop_title(game):
    """the title page query (ownfoil_api_details.cpp ParseTitle): base, two updates, two add-ons."""
    tid, name, pub, ver = game
    dl = f"/api/shop/{tid}"
    return {"data": {"title": {
        "name": name, "publisher": pub, "intro": "Every parcel arrives. Eventually.",
        "description": "Deliver parcels across a sleepy town as a goose with places to be. Fictional demo game for the Kefir Hub docs.",
        "releaseDate": "2026-05-14", "category": ["Simulation", "Adventure"], "numberOfPlayers": "1-2",
        "size": "2147483648", "rating": "E", "region": "WORLD",
        "banner": {"url": f"/demo/screens/{tid}.jpg", "local": True},
        "screenshots": [{"url": f"/demo/screens/{tid}.jpg", "local": True}],
        "availableVersions": [{"version": 131072, "releaseDate": "2026-09-01"}],
        "base": [{"displayVersion": "1.0.0", "downloadSize": "2147483648", "downloadUrl": dl + "/base", "downloadExtension": "nsp"}],
        "updates": [{"appVersion": 65536, "displayVersion": "1.1.0", "downloadUrl": dl + "/v65536", "downloadExtension": "nsp"},
                    {"appVersion": 131072, "displayVersion": ver, "downloadUrl": dl + "/v131072", "downloadExtension": "nsp"}],
        "dlc": [{"appId": tid[:-4] + "1001", "appVersion": 0, "downloadUrl": dl + "/dlc1", "downloadExtension": "nsp",
                 "titledb": {"name": "Golden Bell Pack", "intro": "A bell that honks."}},
                {"appId": tid[:-4] + "1002", "appVersion": 0, "downloadUrl": dl + "/dlc2", "downloadExtension": "nsp",
                 "titledb": {"name": "Winter Scarf Pack", "intro": "Stay warm on the rounds."}}],
    }}}


def shop_items():
    """apps.items as the Ownfoil GraphQL answers (ownfoil_api_details.cpp ParseApps)."""
    return [{"appId": tid, "titleId": tid, "displayVersion": ver,
             "titledb": {"name": name, "publisher": pub, "icon": {"url": f"/demo/icons/{tid}.jpg", "local": True}}}
            for tid, name, pub, ver in SHOP]


def pictures():
    """screenshots for the file browser's image viewer (fixtures sdmc/pictures)."""
    out = REPO / "docs/site/fixtures/sdmc/pictures"
    out.mkdir(parents=True, exist_ok=True)
    screen("stonks-shot", "Stonks Tycoon", "Quarterly report: line goes up").save(out / "2026092818301200-stonks-tycoon.jpg", quality=88)
    screen("borshch-shot", "Borshch Royale", "Victory bowl!").save(out / "2026093020470900-borshch-royale.jpg", quality=88)


if __name__ == "__main__":
    pictures()
    appstore()
    themezer()
    ownfoil()
