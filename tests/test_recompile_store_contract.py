"""tools/recompile-store/build_store.py produces zips and repo.json entries that
the App Store installer (appstore_ops.cpp) accepts: manifest 'U: path' lines
without a leading slash, info.json with the version, hbstore field names."""
import hashlib
import importlib.util
import io
import json
import os
import unittest
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TOOL = os.path.join(ROOT, "tools", "recompile-store", "build_store.py")
spec = importlib.util.spec_from_file_location("build_store", TOOL)
bs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bs)


def make_zip(names):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for n in names:
            z.writestr(n, b"x" * 10)
    return buf.getvalue()


class Layout(unittest.TestCase):
    def test_bare_nro_goes_under_install_dir(self):
        self.assertEqual(bs.plan_layout("sm64.nro", None, "/switch/sm64/"), [("switch/sm64/sm64.nro", None)])

    def test_rooted_zip_is_kept(self):
        members = ["switch/", "switch/port/port.nro", "switch/port/readme.txt"]
        self.assertEqual(bs.plan_layout("port.zip", members, "/switch/port"),
                         [("switch/port/port.nro", "switch/port/port.nro"), ("switch/port/readme.txt", "switch/port/readme.txt")])

    def test_loose_zip_is_moved_and_junk_dropped(self):
        members = ["port.nro", "__MACOSX/._port.nro", "data/.DS_Store", "data/a.bin"]
        self.assertEqual(bs.plan_layout("port.zip", members, "/switch/port"),
                         [("switch/port/port.nro", "port.nro"), ("switch/port/data/a.bin", "data/a.bin")])


class Manifest(unittest.TestCase):
    def test_lines_match_appstore_parser(self):
        text = bs.manifest_text(["switch/a/a.nro", "switch/a/b.txt"])
        self.assertEqual(text, "U: switch/a/a.nro\nU: switch/a/b.txt\n")
        for line in text.splitlines():
            self.assertGreater(len(line), 3)       # ParseManifest skips lines <= 3 chars
            self.assertEqual(line[1:3], ": ")      # path starts at offset 3
            self.assertFalse(line[3] == "/")       # AppendPath("/", path) adds the slash


class Zip(unittest.TestCase):
    def test_zip_has_metadata_and_binary(self):
        data, extracted, binary = bs.build_zip("game.nro", b"NRO0" * 4, "/switch/game", {"version": "v1.2", "name": "game", "title": "Game"})
        self.assertEqual(binary, "/switch/game/game.nro")
        self.assertEqual(extracted, 16)
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            self.assertEqual(sorted(z.namelist()), ["info.json", "manifest.install", "switch/game/game.nro"])
            self.assertEqual(json.loads(z.read("info.json")), {"version": "v1.2", "name": "game", "title": "Game", "binary": "/switch/game/game.nro"})
            self.assertEqual(z.read("manifest.install"), b"U: switch/game/game.nro\n")

    def test_release_zip_is_repacked(self):
        data, _, binary = bs.build_zip("port.zip", make_zip(["port.nro", "res/x.dat"]), "/switch/port", {"version": "3"})
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            self.assertIn("switch/port/res/x.dat", z.namelist())
        self.assertEqual(binary, "/switch/port/port.nro")


class Entry(unittest.TestCase):
    def test_fields(self):
        pkg = {"name": "sm64", "title": "Super Mario 64", "repo": "owner/sm64-port", "icon": "https://i/x.png"}
        release = {"tag_name": "v2.0", "published_at": "2026-10-07T10:00:00Z", "body": "notes"}
        e = bs.repo_entry(pkg, release, 2048 * 3, 1024 * 10, "d41d8cd9", "/switch/sm64/sm64.nro")
        self.assertEqual(e["category"], "recompile")
        self.assertEqual(e["updated"], "07/10/2026")       # dd/mm/yyyy, what ScanHomebrew parses
        self.assertEqual((e["filesize"], e["extracted"]), (6, 10))
        self.assertEqual(e["author"], "owner")
        self.assertEqual(e["url"], "https://github.com/owner/sm64-port")
        self.assertEqual(e["icon"], "https://i/x.png")
        self.assertEqual(e["binary"], "/switch/sm64/sm64.nro")
        for key in ("name", "title", "version", "md5", "description", "details", "changelog", "license", "screens", "app_dls"):
            self.assertIn(key, e)

    def test_pick_asset(self):
        assets = [{"name": "src.tar.gz"}, {"name": "port-switch.zip"}, {"name": "port.nro"}]
        self.assertEqual(bs.pick_asset(assets, r"\.nro$")["name"], "port.nro")
        self.assertEqual(bs.pick_asset(assets, r"\.(nro|zip)$")["name"], "port-switch.zip")
        self.assertIsNone(bs.pick_asset(assets, r"\.7z$"))

    def test_example_sources_file_is_valid(self):
        with open(os.path.join(ROOT, "tools", "recompile-store", "sources.json"), encoding="utf-8") as f:
            src = json.load(f)
        for pkg in src["packages"]:
            self.assertRegex(pkg["repo"], r"^[^/]+/[^/]+$")
            self.assertTrue(pkg["name"])


if __name__ == "__main__":
    unittest.main()
