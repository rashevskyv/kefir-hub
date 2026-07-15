import unittest
import sys
import os
import json
from typing import Dict, Any

# Adjust paths to import
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from models import ModuleInfo
from merge import (
    normalize_tid,
    clean_description,
    process_ndeadly_line,
    BLOCKED_MAPPINGS,
    MANDATORY_TIDS,
    load_manual_overrides,
)

class TestModuleCatalog(unittest.TestCase):

    @staticmethod
    def project_root():
        return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

    def test_normalize_tid(self):
        # Valid TIDs
        self.assertEqual(normalize_tid("00FF0000636C6BFF"), "00FF0000636C6BFF")
        self.assertEqual(normalize_tid("  00ff0000636c6bff  "), "00FF0000636C6BFF")
        self.assertEqual(normalize_tid("010000000000BD00"), "010000000000BD00")
        
        # Invalid TIDs
        self.assertIsNone(normalize_tid("00FF0000636C6BF"))  # 15 chars
        self.assertIsNone(normalize_tid("00FF0000636C6BFF1")) # 17 chars
        self.assertIsNone(normalize_tid("00FF0000636C6B Z"))  # invalid hex char
        self.assertIsNone(normalize_tid(""))
        self.assertIsNone(normalize_tid(None))

    def test_clean_description(self):
        # Test markdown removal
        self.assertEqual(
            clean_description("Configure CPU, GPU and memory clocks with [link](http://example.com)"),
            "Configure CPU, GPU and memory clocks with link."
        )
        self.assertEqual(
            clean_description("Configure **CPU**, *GPU* and `memory` clocks."),
            "Configure CPU, GPU and memory clocks."
        )
        self.assertEqual(
            clean_description("Configure CPU clocks. ![badge](http://badge.com)"),
            "Configure CPU clocks."
        )
        
        # Test version removal
        self.assertEqual(
            clean_description("Configure clocks for v1.2.3. Compatible with Switch."),
            "Configure clocks for."
        )
        self.assertEqual(
            clean_description("Configure clocks (version 2.0)."),
            "Configure clocks ()."
        )
        
        # Test first sentence extraction
        self.assertEqual(
            clean_description("This is the first sentence. And this is the second one."),
            "This is the first sentence."
        )
        
        # Test length limit and ellipsis
        long_desc = "A " * 100 # 200 chars
        cleaned = clean_description(long_desc)
        self.assertTrue(len(cleaned) <= 180)
        self.assertTrue(cleaned.endswith("..."))

    def test_process_ndeadly_line(self):
        self.assertEqual(
            process_ndeadly_line("00FF0000636C6BFF sys-clk"),
            ("00FF0000636C6BFF", "sys-clk")
        )
        self.assertEqual(
            process_ndeadly_line("  010000000000BD00   MissionControl  "),
            ("010000000000BD00", "MissionControl")
        )
        # Comment line
        self.assertIsNone(process_ndeadly_line("# This is a comment"))
        # Invalid line
        self.assertIsNone(process_ndeadly_line("invalid-line"))

    def test_blocked_mappings(self):
        # Ensure rules:
        # 0100000000000BD5 != MissionControl
        # 4200000000000010 != sys-clk
        # 420000000000000B != ldn_mitm
        self.assertEqual(BLOCKED_MAPPINGS.get("0100000000000BD5"), "MissionControl")
        self.assertEqual(BLOCKED_MAPPINGS.get("4200000000000010"), "sys-clk")
        self.assertEqual(BLOCKED_MAPPINGS.get("420000000000000B"), "ldn_mitm")

    def test_mandatory_tids(self):
        # Program must resolve at least:
        # 0100000000000352 emuiibo
        # 010000000000BD00 MissionControl
        # 00FF0000636C6BFF sys-clk
        # 4200000000000010 ldn_mitm
        # 420000000000000B sys-patch
        # 420000000000000E sys-ftpd/sys-ftpd-light
        # 420000000007E51A nx-ovlloader
        # 690000000000000D sys-con
        self.assertEqual(MANDATORY_TIDS["0100000000000352"], "emuiibo")
        self.assertEqual(MANDATORY_TIDS["010000000000BD00"], "MissionControl")
        self.assertEqual(MANDATORY_TIDS["00FF0000636C6BFF"], "sys-clk")
        self.assertEqual(MANDATORY_TIDS["4200000000000010"], "ldn_mitm")
        self.assertEqual(MANDATORY_TIDS["420000000000000B"], "sys-patch")
        self.assertEqual(MANDATORY_TIDS["420000000000000E"], "sys-ftpd")
        self.assertEqual(MANDATORY_TIDS["420000000007E51A"], "nx-ovlloader")
        self.assertEqual(MANDATORY_TIDS["420000000007E51B"], "nx-ovlreloader")
        self.assertEqual(MANDATORY_TIDS["4200000000003103"], "NSParentalControl")
        self.assertEqual(MANDATORY_TIDS["690000000000000D"], "sys-con")
        self.assertNotIn("0100000000000035", MANDATORY_TIDS)
        self.assertNotIn("0100000000554443", MANDATORY_TIDS)

    def test_manual_overrides_do_not_claim_nintendo_system_ids(self):
        overrides = load_manual_overrides()
        self.assertNotIn("0100000000000035", overrides)
        self.assertNotIn("0100000000554443", overrides)

    def test_all_manual_verified_entries_are_complete(self):
        for tid, override in load_manual_overrides().items():
            self.assertEqual(normalize_tid(tid), tid)
            if override.get("confidence") == "verified":
                self.assertTrue(override.get("repository"), tid)
                self.assertTrue(override.get("description_en"), tid)
                self.assertTrue(override.get("tid_evidence"), tid)

    def test_runtime_catalog_and_translations_are_in_sync(self):
        root = self.project_root()
        with open(os.path.join(root, "assets", "romfs", "modules", "homebrew_sysmodules.json"), encoding="utf-8") as f:
            runtime = json.load(f)["modules"]
        with open(os.path.join(root, "tools", "module_catalog", "i18n_en_candidates.json"), encoding="utf-8") as f:
            candidates = json.load(f)
        with open(os.path.join(root, "assets", "romfs", "i18n", "en.json"), encoding="utf-8") as f:
            en = json.load(f)
        with open(os.path.join(root, "assets", "romfs", "i18n", "uk.json"), encoding="utf-8") as f:
            uk = json.load(f)

        expected_keys = {f"module.{tid}.description" for tid in runtime}
        self.assertEqual(expected_keys, set(candidates))
        for key in expected_keys:
            self.assertEqual(en.get(key), candidates[key])
            self.assertTrue(uk.get(key), key)
            self.assertNotEqual(uk[key], en[key], key)

    def test_module_manager_uses_runtime_catalog_and_not_legacy_description_json(self):
        source_path = os.path.join(
            self.project_root(), "sphaira", "source", "ui", "menus", "uninstaller_menu.cpp"
        )
        with open(source_path, encoding="utf-8") as f:
            source = f.read()

        self.assertIn("romfs:/modules/homebrew_sysmodules.json", source)
        self.assertIn("gist.githubusercontent.com/ndeadly/a4b8c01bb453028cd0008f282098f696", source)
        self.assertIn("module.\" + FormatProgramId(program_id) + \".description", source)
        self.assertNotIn("DEFAULT_MODULES_JSON", source)
        self.assertNotIn("paths::MODULES", source)

if __name__ == "__main__":
    unittest.main()
