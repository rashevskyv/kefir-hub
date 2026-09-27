#!/usr/bin/env python3
"""Sphaira v0.13.895: Recursive Package Install Contract."""

import json, os, sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def read_file(*parts: str) -> str:
    with open(os.path.join(REPO_ROOT, *parts), "r", encoding="utf-8") as f:
        return f.read()

def check(cond: bool, msg: str) -> None:
    if not cond:
        print(f"FAIL: {msg}")
        sys.exit(1)

def main() -> None:
    print("Running v0.13.895 recursive install contract...")

    # 1. CMake version bump & source registration
    cmake = read_file("sphaira", "CMakeLists.txt")
    check("set(sphaira_VERSION 0.13.895)" in cmake, "CMakeLists.txt version mismatch")
    check("source/ui/menus/filebrowser/filebrowser_recursive_install.cpp" in cmake,
          "filebrowser_recursive_install.cpp not registered in CMakeLists.txt")

    # 2. Header declarations in filebrowser.hpp
    header = read_file("sphaira", "include", "ui", "menus", "filebrowser.hpp")
    check("void InstallFolderRecursively();" in header, "filebrowser.hpp missing InstallFolderRecursively")
    check("auto GetRecursiveInstallTargets() const -> std::vector<fs::FsPath>;" in header,
          "filebrowser.hpp missing GetRecursiveInstallTargets")

    # 3. Context action wiring and compile-time guard in filebrowser_options.cpp
    opts = read_file("sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_options.cpp")
    check('#if ENABLE_NETWORK_INSTALL' in opts, "filebrowser_options.cpp missing ENABLE_NETWORK_INSTALL guard")
    check('"Install recursively"_i18n' in opts, "filebrowser_options.cpp missing 'Install recursively' string")
    check('InstallFolderRecursively();' in opts, "filebrowser_options.cpp missing InstallFolderRecursively call")
    check('entry->Depends(App::GetInstallEnable' in opts, "filebrowser_options.cpp missing install enable dependency")

    # 4. Implementation wiring and compile-time guard in filebrowser_recursive_install.cpp
    impl = read_file("sphaira", "source", "ui", "menus", "filebrowser", "filebrowser_recursive_install.cpp")
    check('#if ENABLE_NETWORK_INSTALL' in impl and '#else' in impl,
          "filebrowser_recursive_install.cpp missing ENABLE_NETWORK_INSTALL / #else guard")
    check('FsView::GetRecursiveInstallTargets()' in impl, "missing GetRecursiveInstallTargets implementation")
    check('FsView::InstallFolderRecursively()' in impl, "missing InstallFolderRecursively implementation")
    check('PauseRemoteMetadata();' in impl, "missing PauseRemoteMetadata call in install flow")
    check('detail::INSTALL_EXTENSIONS' in impl, "missing detail::INSTALL_EXTENSIONS extension filtering")
    check('App::Push<ui::menu::dbi::Menu>' in impl, "missing DBI queue push in recursive install")

    # 5. Scoped file line count constraints (strictly <= 600 lines)
    for path, max_l in [
        ("sphaira/source/ui/menus/filebrowser/filebrowser_options.cpp", 600),
        ("sphaira/source/ui/menus/filebrowser/filebrowser_ops_archive.cpp", 600),
        ("sphaira/source/ui/menus/filebrowser/filebrowser_recursive_install.cpp", 600),
        ("sphaira/include/ui/menus/filebrowser.hpp", 600),
        ("sphaira/source/ui/menus/dbi/dbi_local.cpp", 600),
        ("sphaira/CMakeLists.txt", 600),
    ]:
        cnt = len(read_file(*path.split("/")).splitlines())
        check(cnt <= max_l, f"{path} exceeded {max_l} lines: {cnt}")

    # 6. EN/UK translation keys
    en_json = json.loads(read_file("assets", "romfs", "i18n", "en.json"))
    uk_json = json.loads(read_file("assets", "romfs", "i18n", "uk.json"))
    for key, uk_val in [
        ("Install recursively", "Встановити рекурсивно"),
        ("Scan selected folder(s) recursively for packages and open the install queue.",
         "Рекурсивно просканувати вибрані папки на наявність пакетів та відкрити чергу встановлення."),
        ("No packages found.", "Пакети не знайдено."),
        ("Failed to scan folder", "Не вдалося просканувати папку"),
    ]:
        check(key in en_json and en_json[key] == key, f"en.json key mismatch: {key}")
        check(key in uk_json and uk_json[key] == uk_val, f"uk.json translation mismatch: {key}")

    print("CONTRACT PASSED: Source wiring, guards, line limits, and i18n verified.")

if __name__ == "__main__":
    main()
