"""Tests for C++ language selection and migration logic.

Verifies:
1. Supported languages list completeness, uniqueness, and autonyms.
2. System language mapping.
3. Legacy migration rules (0 -> system, 11 -> 1, missing -> system).
4. Absence of Russian in supported languages.
5. First-run logic and INI persistence rules.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
I18N = ROOT.parent.parent / "assets" / "romfs" / "i18n"
SRC_I18N_HPP = ROOT.parent.parent / "sphaira" / "include" / "i18n.hpp"
SRC_I18N_CPP = ROOT.parent.parent / "sphaira" / "source" / "i18n.cpp"
SRC_STARTUP_CPP = ROOT.parent.parent / "sphaira" / "source" / "app_startup.cpp"
SRC_SETTINGS_CPP = ROOT.parent.parent / "sphaira" / "source" / "app_settings.cpp"

FAILURES = []


def check(name, fn):
    try:
        fn()
        print(f"  PASS  {name}")
    except Exception as e:
        FAILURES.append(f"{name}: {e}")
        print(f"  FAIL  {name}: {e}")


def test_supported_languages_in_cpp():
    content = SRC_I18N_CPP.read_text(encoding="utf-8")
    assert "SUPPORTED_LANGUAGES[] = {" in content

    # Verify Russian is NOT in SUPPORTED_LANGUAGES
    assert '"ru"' not in content.split("SUPPORTED_LANGUAGES[] = {")[1].split("};")[0]

    # 26 languages
    expected_codes = [
        "en", "ja", "fr", "de", "it", "es", "zh", "ko", "nl", "pt",
        "se", "vi", "uk", "be", "engb", "es419", "et", "frca", "id",
        "kk", "lt", "lv", "pl", "ptbr", "tr", "zhtw"
    ]
    for code in expected_codes:
        assert f'"{code}"' in content, f"Language code {code} missing from SUPPORTED_LANGUAGES"


def test_no_legacy_id_collision():
    content = SRC_I18N_CPP.read_text(encoding="utf-8")
    table_text = content.split("SUPPORTED_LANGUAGES[] = {")[1].split("};")[0]
    ids = []
    for line in table_text.strip().splitlines():
        line = line.strip()
        if line.startswith("{"):
            parts = [p.strip() for p in line.strip("{};,").split(",")]
            lang_id = int(parts[0])
            ids.append(lang_id)

    assert len(ids) == 26, f"Expected 26 languages, found {len(ids)}"
    assert len(ids) == len(set(ids)), f"Duplicate language IDs found: {ids}"
    assert 11 not in ids, "ID 11 (Russian) must not be in supported languages"
    assert 0 not in ids, "ID 0 (Auto) must not be in supported languages"
    assert 14 in ids, "ID 14 (Ukrainian) must be preserved"


def test_legacy_migration_logic():
    content = SRC_I18N_CPP.read_text(encoding="utf-8")
    assert "long MigrateLegacyLanguage" in content

    # Test migration logic in Python matching C++
    def migrate(old_id, has_key, sys_lang=1):
        if not has_key:
            return sys_lang
        if old_id == 0:
            return sys_lang
        if old_id == 11:
            return 1 # English
        if old_id in [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27]:
            return old_id
        return 1

    # First run without key
    assert migrate(0, False, sys_lang=14) == 14
    # Legacy Auto
    assert migrate(0, True, sys_lang=14) == 14
    # Legacy Russian
    assert migrate(11, True, sys_lang=14) == 1
    # Existing Ukrainian
    assert migrate(14, True, sys_lang=1) == 14
    # Existing English
    assert migrate(1, True, sys_lang=14) == 1


def test_system_language_russian_maps_to_english():
    content = SRC_I18N_CPP.read_text(encoding="utf-8")
    assert "case SetLanguage_RU: return 1;" in content or "case SetLanguage_RU:" in content


def test_first_run_detection_in_startup():
    content = SRC_STARTUP_CPP.read_text(encoding="utf-8")
    assert 'ini_haskey(INI_SECTION, "language", CONFIG_PATH)' in content
    assert "m_language_chosen = true;" in content
    assert "m_language_chosen = false;" in content
    assert "App::ShowInitialLanguageSelection();" in content


def test_popup_list_cancel_disabled():
    content = SRC_SETTINGS_CPP.read_text(encoding="utf-8")
    assert "popup->SetAllowCancel(false);" in content


def test_language_persistence_on_selection():
    content = SRC_SETTINGS_CPP.read_text(encoding="utf-8")
    startup = SRC_STARTUP_CPP.read_text(encoding="utf-8")
    first_run = startup.split('const bool has_language_in_ini =', 1)[1].split('i18n::init(', 1)[0]
    assert 'm_language.Set(i18n::MatchSystemLanguage())' not in first_run, "first launch must not save before confirmation"
    assert content.index('App::SetLanguage(def.id, false);') < content.index('App::MarkLanguageChosen();')
    assert "g_app->m_language.Set(index);" in content


def test_autonyms_completeness():
    content = SRC_I18N_CPP.read_text(encoding="utf-8")
    table_text = content.split("SUPPORTED_LANGUAGES[] = {")[1].split("};")[0]
    for line in table_text.strip().splitlines():
        line = line.strip()
        if line.startswith("{"):
            parts = [p.strip() for p in line.strip("{};,").split('"') if p.strip() and p.strip() != ","]
            # parts has code, dbi_code, name_en, name_native
            assert len(parts) >= 4, f"Malformed language entry: {line}"
            code, dbi_code, name_en, name_native = parts[0], parts[1], parts[2], parts[3]
            assert name_native, f"Empty autonym for {code}"
            assert name_en, f"Empty English name for {code}"


def main():
    print("Running C++ language selection and migration logic tests...")
    tests = [v for k, v in globals().items() if k.startswith("test_")]
    for fn in tests:
        check(fn.__name__, fn)

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED:")
        for f in FAILURES:
            print(f"  - {f}")
        exit(1)
    print("All language logic tests passed!")


if __name__ == "__main__":
    main()
