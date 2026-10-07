#!/usr/bin/env python3
"""Build a Homebrew App Store compatible store from GitHub releases.

    python build_store.py sources.json out/

out/ gets the layout Kefir Hub (and any hbstore client) reads:
    repo.json                     list of packages
    packages/<name>/icon.png      icon (when the source names one)
    zips/<name>.zip               files in memory-card layout + manifest.install + info.json

Publish out/ anywhere that serves plain files (a GitHub repository read through
raw.githubusercontent.com is enough) and put the folder address into
/config/kefir/appstore_sources.txt on the console, or make it a default source.

Only packages whose release tag changed since the last run are rebuilt; the
rest are copied over from the previous repo.json. Set GITHUB_TOKEN to lift the
anonymous API limit. Standard library only.
"""
import hashlib
import io
import json
import os
import re
import sys
import urllib.request
import zipfile
from datetime import datetime

CATEGORY = "recompile"
API = "https://api.github.com/repos/{repo}/releases/latest"
SD_ROOTS = ("switch/", "atmosphere/", "config/", "retroarch/", "roms/")


# ---- pure helpers (tested in tests/test_recompile_store_contract.py) -------------

def pick_asset(assets, pattern):
    """First release asset whose name matches the regex; None when nothing does."""
    rx = re.compile(pattern)
    for asset in assets:
        if rx.search(asset["name"]):
            return asset
    return None


def plan_layout(asset_name, members, install_dir):
    """Map store-zip paths (no leading slash) to the asset's content.

    members is None for a bare file (an .nro) or the list of file names inside a
    release .zip. A release zip that already starts at the memory-card root
    (switch/, atmosphere/, ...) is kept as is; anything else goes under install_dir.
    Returns a list of (zip_path, member_name_or_None).
    """
    install_dir = install_dir.strip("/")
    if members is None:
        return [(f"{install_dir}/{os.path.basename(asset_name)}", None)]

    files = [m for m in members if not m.endswith("/")]
    files = [m for m in files if not m.startswith("__MACOSX/") and os.path.basename(m) != ".DS_Store"]
    rooted = all(m.lower().startswith(SD_ROOTS) for m in files)
    if rooted:
        return [(m, m) for m in files]
    return [(f"{install_dir}/{m}", m) for m in files]


def manifest_text(paths):
    """hbstore manifest: one 'U: path' line per file, LF endings."""
    return "".join(f"U: {p}\n" for p in paths)


def first_nro(paths):
    for p in paths:
        if p.lower().endswith(".nro"):
            return "/" + p
    return ""


def repo_entry(pkg, release, zip_size, extracted, md5, binary):
    """One package object of repo.json, in the field names hbstore uses."""
    published = release.get("published_at", "")[:10]
    updated = ""
    if published:
        updated = datetime.strptime(published, "%Y-%m-%d").strftime("%d/%m/%Y")
    entry = {
        "category": CATEGORY,
        "name": pkg["name"],
        "title": pkg.get("title", pkg["name"]),
        "author": pkg.get("author", pkg["repo"].split("/")[0]),
        "version": release.get("tag_name", ""),
        "updated": updated,
        "description": pkg.get("description", ""),
        "details": pkg.get("details", pkg.get("description", "")),
        "changelog": (release.get("body") or "")[:4000],
        "url": pkg.get("url", f"https://github.com/{pkg['repo']}"),
        "license": pkg.get("license", ""),
        "binary": pkg.get("binary", binary) or "none",
        "filesize": max(1, zip_size // 1024),
        "extracted": max(1, extracted // 1024),
        "md5": md5,
        "screens": 0,
        "app_dls": 0,
    }
    if pkg.get("icon"):
        entry["icon"] = pkg["icon"]
    return entry


def build_zip(asset_name, asset_bytes, install_dir, info):
    """Return (zip_bytes, extracted_size, binary) for one release asset."""
    members = None
    src = None
    if asset_name.lower().endswith(".zip"):
        src = zipfile.ZipFile(io.BytesIO(asset_bytes))
        members = src.namelist()
    layout = plan_layout(asset_name, members, install_dir)
    paths = [p for p, _ in layout]

    out = io.BytesIO()
    extracted = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for path, member in layout:
            data = asset_bytes if member is None else src.read(member)
            extracted += len(data)
            z.writestr(path, data)
        binary = first_nro(paths)
        info = dict(info, binary=binary)
        z.writestr("manifest.install", manifest_text(paths))
        z.writestr("info.json", json.dumps(info) + "\n")
    return out.getvalue(), extracted, binary


# ---- network + files -------------------------------------------------------------------

def fetch(url, binary=False):
    req = urllib.request.Request(url, headers={"User-Agent": "kefir-store-builder"})
    token = os.environ.get("GITHUB_TOKEN")
    if token and "api.github.com" in url:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    return data if binary else json.loads(data)


def load_previous(out_dir):
    try:
        with open(os.path.join(out_dir, "repo.json"), encoding="utf-8") as f:
            return {e["name"]: e for e in json.load(f).get("packages", [])}
    except (OSError, ValueError):
        return {}


def build_package(pkg, out_dir, previous):
    release = fetch(API.format(repo=pkg["repo"]))
    tag = release.get("tag_name", "")
    old = previous.get(pkg["name"])
    zip_path = os.path.join(out_dir, "zips", f"{pkg['name']}.zip")
    if old and old.get("version") == tag and os.path.exists(zip_path):
        print(f"  {pkg['name']}: {tag} unchanged")
        return old

    asset = pick_asset(release.get("assets", []), pkg.get("asset", r"\.(nro|zip)$"))
    if not asset:
        print(f"  {pkg['name']}: no asset matches {pkg.get('asset')!r} in {tag}", file=sys.stderr)
        return old
    print(f"  {pkg['name']}: {tag} <- {asset['name']}")
    asset_bytes = fetch(asset["browser_download_url"], binary=True)

    info = {"version": tag, "name": pkg["name"], "title": pkg.get("title", pkg["name"])}
    zip_bytes, extracted, binary = build_zip(asset["name"], asset_bytes, pkg.get("install_dir", f"/switch/{pkg['name']}"), info)
    os.makedirs(os.path.dirname(zip_path), exist_ok=True)
    with open(zip_path, "wb") as f:
        f.write(zip_bytes)

    if pkg.get("icon"):
        icon_dir = os.path.join(out_dir, "packages", pkg["name"])
        os.makedirs(icon_dir, exist_ok=True)
        try:
            with open(os.path.join(icon_dir, "icon.png"), "wb") as f:
                f.write(fetch(pkg["icon"], binary=True))
        except OSError as e:
            print(f"  {pkg['name']}: icon failed: {e}", file=sys.stderr)

    return repo_entry(pkg, release, len(zip_bytes), extracted, hashlib.md5(zip_bytes).hexdigest(), binary)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    with open(argv[1], encoding="utf-8") as f:
        sources = json.load(f)
    out_dir = argv[2]
    os.makedirs(out_dir, exist_ok=True)
    previous = load_previous(out_dir)

    packages = []
    for pkg in sources["packages"]:
        try:
            entry = build_package(pkg, out_dir, previous)
        except Exception as e:  # one broken release must not drop the whole store
            print(f"  {pkg['name']}: failed: {e}", file=sys.stderr)
            entry = previous.get(pkg["name"])
        if entry:
            packages.append(entry)

    with open(os.path.join(out_dir, "repo.json"), "w", encoding="utf-8") as f:
        json.dump({"packages": packages}, f, ensure_ascii=False, indent=1)
    print(f"{len(packages)} packages -> {out_dir}/repo.json")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
