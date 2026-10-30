#!/usr/bin/env python3
"""Build assets/romfs/bios/bios.json from a retrobios checkout.

    python3 tools/bios/build_manifest.py /path/to/retrobios [out.json]

The checkout needs platforms/retroarch.yml, database.json and emulators/*.yml
(a sparse clone without bios/ is enough). The manifest is the RetroArch BIOS set
(libretro System.dat as retrobios ships it): one entry per file with the path in
the retrobios repo, the destination under the emulator's system folder, sha1 and
size, plus the core -> systems map that filters "only installed cores".
The Hub downloads the files one by one from raw.githubusercontent.com; nothing
is bundled (see retrobios NOTICE: the BIOS files carry no licence).
"""
import json
import pathlib
import sys
import time

import yaml

SOURCE = "https://raw.githubusercontent.com/Abdess/retrobios/main/"

# emulator profiles name a few systems differently from platforms/retroarch.yml; the rest
# differ only by hyphens (sega-megacd / sega-mega-cd) and are matched with the hyphens removed
SYSTEM_ALIAS = {
    "sega-cd": "sega-mega-cd",
    "sega-genesis": "sega-mega-drive",
    "sega-naomi": "sega-dreamcast-arcade",
    "sega-naomi2": "sega-dreamcast-arcade",
    "sega-atomiswave": "sega-dreamcast-arcade",
    "odyssey2": "magnavox-odyssey2",
    "magnavox-odyssey-2": "magnavox-odyssey2",
    "videopac": "philips-videopac",
    "philips-videopac-plus": "philips-videopac",
    "msx": "microsoft-msx",
    "snes": "nintendo-snes",
    "sinclair-spectrum": "sinclair-zx-spectrum",
    "fairchild-channelf": "fairchild-channel-f",
}

# Tico keeps BIOS files in /tico/system/<folder>/ (HaulNX wiki, Emulator-TICO: psx verified,
# the other folder names follow HaulNX's table which Tico shares). Systems not listed here
# are not offered for Tico.
TICO_FOLDER = {
    "sony-playstation": "psx",
    "sony-playstation-2": "ps2",
    "sega-dreamcast": "dc",
    "sega-dreamcast-arcade": "naomi",
    "sega-saturn": "saturn",
    "sega-mega-cd": "sega-cd",
    "nintendo-gamecube": "gc",
    "3do": "3do",
    "nec-pc-engine": "pc-engine-cd",
    "nec-pc-fx": "pc-fx",
    "snk-neogeo-cd": "neo-geo-cd",
    "nintendo-ds": "nds",
    "nintendo-fds": "fds",
    "philips-cdi": "cd-i",
}


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    repo = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path("assets/romfs/bios/bios.json")

    ra = yaml.safe_load((repo / "platforms" / "retroarch.yml").read_text(encoding="utf-8"))
    db = json.loads((repo / "database.json").read_text(encoding="utf-8"))["files"]
    by_md5 = {row["md5"]: row for row in db.values() if row.get("md5")}

    systems = []
    missing = []
    for sid, sysdef in sorted(ra["systems"].items()):
        files = []
        name = ""
        for f in sysdef["files"]:
            name = name or f.get("native_system", sid)
            row = db.get(f.get("sha1") or "") or by_md5.get(f.get("md5") or "")
            if not row:
                missing.append((sid, f["name"]))
                continue
            files.append({
                "dest": f["destination"],
                "path": row["path"],
                "sha1": row["sha1"],
                "size": int(row["size"]),
            })
        if not files:
            continue
        entry = {"id": sid, "name": name, "files": files}
        if sid in TICO_FOLDER:
            entry["tico"] = TICO_FOLDER[sid]
        systems.append(entry)
    known = {s["id"] for s in systems}
    flat = {k.replace("-", ""): k for k in known}

    cores = {}
    for yml in sorted((repo / "emulators").glob("*.yml")):
        d = yaml.safe_load(yml.read_text(encoding="utf-8")) or {}
        ids = []
        for s in d.get("systems") or []:
            s = SYSTEM_ALIAS.get(s, s)
            s = flat.get(s.replace("-", ""), s)
            if s in known and s not in ids:
                ids.append(s)
        if not ids:
            continue
        for core in d.get("cores") or [yml.stem]:
            core = str(core).lower()
            cores.setdefault(core, [])
            for s in ids:
                if s not in cores[core]:
                    cores[core].append(s)

    manifest = {
        "source": SOURCE,
        "retroarch": ra.get("version", ""),
        "generated": time.strftime("%Y-%m-%d"),
        "systems": systems,
        "cores": dict(sorted(cores.items())),
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, separators=(",", ":"), ensure_ascii=False) + "\n", encoding="utf-8")

    total = sum(f["size"] for s in systems for f in s["files"])
    print(f"{out}: {len(systems)} systems, {sum(len(s['files']) for s in systems)} files, "
          f"{total / 1e6:.0f} MB, {len(cores)} cores, {out.stat().st_size // 1024} KB")
    if missing:
        print(f"{len(missing)} retroarch files not in database.json (skipped):", missing[:5])
    uncovered = sorted(known - {s for v in cores.values() for s in v})
    if uncovered:
        print("systems no core maps to:", uncovered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
