"""
Contract tests for Ownfoil integration in Sphaira / Kefir Hub.
Verifies file line limits, CMake source registration, i18n coverage,
discovery contracts, and security requirements.
"""

import json
import os
import re
import sys
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

NEW_PORT_FILES = [
    "sphaira/include/ui/menus/ownfoil.hpp",
    "sphaira/include/ui/menus/ownfoil_title.hpp",
    "sphaira/include/ui/popup_multi_select.hpp",
    "sphaira/include/utils/ownfoil.hpp",
    "sphaira/include/utils/ownfoil_api.hpp",
    "sphaira/include/utils/ownfoil_discovery.hpp",
    "sphaira/include/utils/ownfoil_installed.hpp",
    "sphaira/include/yati/source/http.hpp",
    "sphaira/source/ui/menus/ownfoil.cpp",
    "sphaira/source/ui/menus/ownfoil_catalog.cpp",
    "sphaira/source/ui/menus/ownfoil_internal.hpp",
    "sphaira/source/ui/menus/ownfoil_screenshot_viewer.cpp",
    "sphaira/source/ui/menus/ownfoil_servers.cpp",
    "sphaira/source/ui/menus/ownfoil_title.cpp",
    "sphaira/source/ui/menus/ownfoil_title_draw.cpp",
    "sphaira/source/ui/menus/ownfoil_title_install.cpp",
    "sphaira/source/ui/menus/ownfoil_title_internal.hpp",
    "sphaira/source/ui/popup_multi_select.cpp",
    "sphaira/source/utils/ownfoil.cpp",
    "sphaira/source/utils/ownfoil_api.cpp",
    "sphaira/source/utils/ownfoil_api_details.cpp",
    "sphaira/source/utils/ownfoil_api_internal.hpp",
    "sphaira/source/utils/ownfoil_discovery.cpp",
    "sphaira/source/utils/ownfoil_installed.cpp",
    "sphaira/source/yati/source/http.cpp",
    "tests/test_ownfoil_contract.py",
]

NEW_CPP_SOURCES = [
    "source/ui/menus/ownfoil.cpp",
    "source/ui/menus/ownfoil_servers.cpp",
    "source/ui/menus/ownfoil_catalog.cpp",
    "source/ui/menus/ownfoil_screenshot_viewer.cpp",
    "source/ui/menus/ownfoil_title.cpp",
    "source/ui/menus/ownfoil_title_draw.cpp",
    "source/ui/menus/ownfoil_title_install.cpp",
    "source/ui/popup_multi_select.cpp",
    "source/utils/ownfoil.cpp",
    "source/utils/ownfoil_discovery.cpp",
    "source/utils/ownfoil_api.cpp",
    "source/utils/ownfoil_api_details.cpp",
    "source/utils/ownfoil_installed.cpp",
    "source/yati/source/http.cpp",
]


class TestOwnfoilLineLimits(unittest.TestCase):
    """Ensures all new files conform to the <= 600 lines rule."""

    def test_all_new_files_within_600_lines(self):
        for rel_path in NEW_PORT_FILES:
            full_path = os.path.join(REPO_ROOT, rel_path)
            self.assertTrue(os.path.isfile(full_path), f"File missing: {rel_path}")
            with open(full_path, "r", encoding="utf-8", errors="ignore") as fp:
                line_count = len(fp.readlines())
            self.assertLessEqual(
                line_count,
                600,
                f"File {rel_path} has {line_count} lines, exceeding the 600 line limit!",
            )


class TestCMakeListsIntegrity(unittest.TestCase):
    """Verifies CMakeLists.txt configuration and version lock."""

    def setUp(self):
        self.cmake_path = os.path.join(REPO_ROOT, "sphaira", "CMakeLists.txt")
        with open(self.cmake_path, "r", encoding="utf-8") as fp:
            self.cmake_content = fp.read()

    def test_all_new_cpp_sources_registered(self):
        for src in NEW_CPP_SOURCES:
            self.assertIn(
                src,
                self.cmake_content,
                f"Source file {src} is missing from sphaira/CMakeLists.txt",
            )


class TestSecurityAndLogging(unittest.TestCase):
    """Ensures no sensitive credentials, passwords, or tokens are logged."""

    def test_no_credential_logging(self):
        sensitive_patterns = [
            re.compile(r'log_write\(.*pass', re.IGNORECASE),
            re.compile(r'log_write\(.*password', re.IGNORECASE),
            re.compile(r'log_write\(.*secret', re.IGNORECASE),
            re.compile(r'log_write\(.*token', re.IGNORECASE),
            re.compile(r'log_write\(.*auth', re.IGNORECASE),
        ]
        for rel_path in NEW_PORT_FILES:
            if not rel_path.endswith(".cpp"):
                continue
            full_path = os.path.join(REPO_ROOT, rel_path)
            with open(full_path, "r", encoding="utf-8") as fp:
                for line_num, line in enumerate(fp, 1):
                    for pat in sensitive_patterns:
                        self.assertIsNone(
                            pat.search(line),
                            f"Potential sensitive logging in {rel_path}:{line_num}: {line.strip()}",
                        )


class TestI18nCompleteness(unittest.TestCase):
    """Verifies that all 14 locale json files are valid JSON and contain required Ownfoil keys."""

    REQUIRED_KEYS = [
        "Browse and install titles from a self-hosted Ownfoil server.\n\nInternet connection required.",
        "Add server",
        "Edit server",
        "Delete server",
        "New games",
        "Updates",
        "All games",
        "Discover local servers",
        "Searching the network...",
        "Could not parse the server's response",
        "Could not reach the server",
        "Incorrect username or password",
        "Server returned an error",
        "The server rejected the request",
        "The shop has no details for this title",
        "Add-on content",
        "Installed version",
        "Installed",
        "Requires ",
    ]

    def test_all_locales_valid_and_complete(self):
        i18n_dir = os.path.join(REPO_ROOT, "assets", "romfs", "i18n")
        self.assertTrue(os.path.isdir(i18n_dir))

        for fname in os.listdir(i18n_dir):
            if not fname.endswith(".json"):
                continue
            fpath = os.path.join(i18n_dir, fname)
            with open(fpath, "r", encoding="utf-8") as fp:
                try:
                    data = json.load(fp)
                except Exception as e:
                    self.fail(f"Invalid JSON in {fname}: {e}")

            for key in self.REQUIRED_KEYS:
                self.assertIn(
                    key,
                    data,
                    f"Locale file {fname} is missing key: '{key}'",
                )
                self.assertTrue(
                    len(data[key]) > 0,
                    f"Locale file {fname} has empty value for key: '{key}'",
                )


class TestOwnfoilProtocolsAndContracts(unittest.TestCase):
    """Checks protocol constants, UDP discovery, range-resume, and Yati filter integration."""

    def test_discovery_constants(self):
        disc_hdr = os.path.join(REPO_ROOT, "sphaira", "include", "utils", "ownfoil_discovery.hpp")
        with open(disc_hdr, "r", encoding="utf-8") as fp:
            content = fp.read()
        self.assertIn("8465", content)

        disc_src = os.path.join(REPO_ROOT, "sphaira", "source", "utils", "ownfoil_discovery.cpp")
        with open(disc_src, "r", encoding="utf-8") as fp:
            src_content = fp.read()
        self.assertIn("PORT = 8465", src_content)
        self.assertIn("OWNFOIL_DISCOVER", src_content)

    def test_config_ini_path(self):
        ini_src = os.path.join(REPO_ROOT, "sphaira", "source", "utils", "ownfoil.cpp")
        with open(ini_src, "r", encoding="utf-8") as fp:
            content = fp.read()
        self.assertIn("/config/sphaira/ownfoil.ini", content)

    def test_http_range_resume_logic(self):
        http_src = os.path.join(REPO_ROOT, "sphaira", "source", "yati", "source", "http.cpp")
        with open(http_src, "r", encoding="utf-8") as fp:
            content = fp.read()
        self.assertIn("CURLOPT_RANGE", content)
        self.assertIn("RESUME_WINDOW_NS", content)
        self.assertIn("60'000'000'000ULL", content)
        self.assertIn("PushThreadData", content)
        self.assertIn("CURLAUTH_BASIC", content)
        self.assertIn("status != 206", content)
        self.assertIn("Result_YatiHttpReadFailed", content)

    def test_yati_title_ids_filter(self):
        yati_hdr = os.path.join(REPO_ROOT, "sphaira", "include", "yati", "yati.hpp")
        with open(yati_hdr, "r", encoding="utf-8") as fp:
            content = fp.read()
        self.assertIn("std::vector<u64> title_ids", content)

        yati_meta = os.path.join(REPO_ROOT, "sphaira", "source", "yati", "yati_metadata.cpp")
        with open(yati_meta, "r", encoding="utf-8") as fp:
            content = fp.read()
        self.assertIn("config.title_ids.empty()", content)

    def test_navigation_integration(self):
        sw_src = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "settings", "settings_software.cpp")
        with open(sw_src, "r", encoding="utf-8") as fp:
            content = fp.read()
        self.assertIn("ui::menu::ownfoil::Menu", content)

        inst_src = os.path.join(REPO_ROOT, "sphaira", "source", "ui", "menus", "install_share.cpp")
        with open(inst_src, "r", encoding="utf-8") as fp:
            content = fp.read()
        self.assertIn("ui::menu::ownfoil::Menu", content)


if __name__ == "__main__":
    unittest.main()
