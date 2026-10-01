"""
Contract tests for Ownfoil integration in Sphaira / Kefir Hub.
Verifies that every locale JSON carries the Ownfoil keys.
"""

import json
import os
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


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


if __name__ == "__main__":
    unittest.main()
