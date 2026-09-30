"""Tests for Kefir Hub dynamic JSON language selection and migration logic.

Verifies:
1. Available languages list is formed dynamically from valid JSONs in romfs:/i18n/.
2. Language display names are read from __language_name metadata in JSON.
3. Language list is sorted alphabetically by displayed name.
4. Missing, damaged, or empty JSONs and unknown codes trigger selection screen.
5. Valid codes are preserved and saved.
6. Valid legacy numeric values migrate to corresponding JSON codes.
7. Old 0 (Auto), 11 (Russian), unknown numbers, and "ru" lead to selection screen.
8. Russian locale does not exist and is never added.
9. Metadata (__language_name) is excluded from UI translation corpus.
10. Single shared dialog function is used for both first-run and settings.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
I18N = ROOT.parent.parent / "assets" / "romfs" / "i18n"
SRC_I18N_HPP = ROOT.parent.parent / "sphaira" / "include" / "i18n.hpp"
SRC_I18N_CPP = ROOT.parent.parent / "sphaira" / "source" / "i18n.cpp"
SRC_STARTUP_CPP = ROOT.parent.parent / "sphaira" / "source" / "app_startup.cpp"
SRC_SETTINGS_CPP = ROOT.parent.parent / "sphaira" / "source" / "app_settings.cpp"
SRC_CATEGORIES_CPP = ROOT.parent.parent / "sphaira" / "source" / "ui" / "menus" / "settings" / "settings_categories.cpp"

FAILURES = []


def check(name, fn):
    try:
        fn()
        print(f"  PASS  {name}")
    except Exception as e:
        FAILURES.append(f"{name}: {e}")
        print(f"  FAIL  {name}: {e}")


def scan_valid_languages_python():
    """Python reference implementation of C++ ScanAvailableLanguages."""
    valid_langs = []
    for p in I18N.glob("*.json"):
        if p.name.startswith(".") or p.stem == "ru":
            continue
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except Exception:
            continue
        if not isinstance(data, dict):
            continue
        name = data.get("__language_name")
        if not isinstance(name, str) or not name.strip():
            continue
        if not any(
            not key.startswith("__") and isinstance(value, str) and value
            for key, value in data.items()
        ):
            continue
        valid_langs.append({"code": p.stem, "name": name.strip()})

    valid_langs.sort(key=lambda x: (x["name"].lower(), x["name"]))
    return valid_langs


def test_list_formed_from_existing_jsons():
    langs = scan_valid_languages_python()
    assert len(langs) == 26, f"Expected 26 valid languages, found {len(langs)}"
    codes = {l["code"] for l in langs}
    assert "uk" in codes
    assert "en" in codes
    assert "zhtw" in codes
    assert "es419" in codes
    assert "ru" not in codes


def test_name_read_from_json():
    for p in I18N.glob("*.json"):
        data = json.loads(p.read_text(encoding="utf-8"))
        name = data.get("__language_name")
        assert isinstance(name, str) and name.strip(), f"Empty or missing __language_name in {p.name}"

    uk_data = json.loads((I18N / "uk.json").read_text(encoding="utf-8"))
    assert uk_data["__language_name"] == "Ukrainian — Українська"
    en_data = json.loads((I18N / "en.json").read_text(encoding="utf-8"))
    assert en_data["__language_name"] == "English"


def test_alphabetical_order():
    langs = scan_valid_languages_python()
    names = [l["name"] for l in langs]
    sorted_names = sorted(names, key=lambda s: (s.lower(), s))
    assert names == sorted_names, f"Languages are not in alphabetical order:\n{names}"
    assert names[0].startswith("Belarusian")
    assert names[-1].startswith("Vietnamese")


def test_ru_not_present_and_rejected():
    assert not (I18N / "ru.json").exists(), "ru.json must not exist in assets/romfs/i18n"
    content = SRC_I18N_CPP.read_text(encoding="utf-8")
    assert 'code == "ru"' in content, "C++ must explicitly reject 'ru'"
    assert 'if (s_val.empty() || s_val == "ru")' in content, "C++ migration must reject 'ru'"


def test_missing_or_damaged_json_and_unknown_code():
    # Helper replicating C++ isValid
    valid_codes = {l["code"] for l in scan_valid_languages_python()}

    def check_valid_or_migrate(saved_val, has_key):
        if not has_key:
            return ""
        s = saved_val.strip()
        if not s or s == "ru":
            return ""
        try:
            num = int(s)
            if num in (0, 11):
                return ""
            mapping = {
                1: "en", 2: "ja", 3: "fr", 4: "de", 5: "it", 6: "es", 7: "zh",
                8: "ko", 9: "nl", 10: "pt", 12: "se", 13: "vi", 14: "uk",
                15: "be", 16: "engb", 17: "es419", 18: "et", 19: "frca",
                20: "id", 21: "kk", 22: "lt", 23: "lv", 24: "pl", 25: "ptbr",
                26: "tr", 27: "zhtw"
            }
            code = mapping.get(num)
            return code if code in valid_codes else ""
        except ValueError:
            return s if s in valid_codes else ""

    # Unknown code -> selection screen ("")
    assert check_valid_or_migrate("nonexistent_lang", True) == ""
    assert check_valid_or_migrate("invalid_code_123", True) == ""
    assert check_valid_or_migrate("", True) == ""
    assert check_valid_or_migrate("   ", True) == ""
    assert check_valid_or_migrate("ru", True) == ""
    assert check_valid_or_migrate("0", True) == ""
    assert check_valid_or_migrate("11", True) == ""
    assert check_valid_or_migrate("-1", True) == ""
    assert check_valid_or_migrate("999", True) == ""
    assert check_valid_or_migrate("uk", False) == ""

    # Valid codes preserved
    assert check_valid_or_migrate("uk", True) == "uk"
    assert check_valid_or_migrate("en", True) == "en"
    assert check_valid_or_migrate("es419", True) == "es419"
    assert check_valid_or_migrate("zhtw", True) == "zhtw"

    # Valid legacy numbers migrated
    assert check_valid_or_migrate("14", True) == "uk"
    assert check_valid_or_migrate("1", True) == "en"
    assert check_valid_or_migrate("27", True) == "zhtw"


def test_first_run_does_not_save_before_confirmation():
    startup = SRC_STARTUP_CPP.read_text(encoding="utf-8")
    first_run_block = startup.split("const bool has_language_in_ini =", 1)[1].split("i18n::init(", 1)[0]
    assert "m_language.Set(" in first_run_block
    # Verify m_language.Set is ONLY called inside if (!validated_code.empty())
    assert "if (!validated_code.empty())" in first_run_block
    assert "m_language_chosen = false;" in first_run_block
    # Check that m_language.Set is NOT called in else branch
    else_block = first_run_block.split("else {")[1].split("}")[0]
    assert "m_language.Set" not in else_block


def test_metadata_not_in_ui_translations():
    i18n_cpp = SRC_I18N_CPP.read_text(encoding="utf-8")
    assert 'if (str.starts_with("__"))' in i18n_cpp, "i18n.cpp must reject keys starting with __ in get_internal"

    # Verify en.json has safe fallback
    en_json = json.loads((I18N / "en.json").read_text(encoding="utf-8"))
    assert "__language_name" in en_json

    # Verify translate jobs skip __ keys
    from translate import build_jobs
    jobs = build_jobs(en_json, {"uk": {}}, ["uk"])
    queued_keys = [j[0] for j in jobs]
    assert "__language_name" not in queued_keys, "Metadata key must not be queued for translation"


def test_shared_dialog_component_used():
    settings_cpp = SRC_SETTINGS_CPP.read_text(encoding="utf-8")
    categories_cpp = SRC_CATEGORIES_CPP.read_text(encoding="utf-8")
    startup_cpp = SRC_STARTUP_CPP.read_text(encoding="utf-8")

    # Both call OpenLanguageSelectDialog
    assert "void App::OpenLanguageSelectDialog(bool is_initial_setup)" in settings_cpp
    assert "OpenLanguageSelectDialog(true);" in settings_cpp
    assert "App::OpenLanguageSelectDialog(false);" in categories_cpp

    # settings_categories does NOT construct its own PopupList for language
    assert 'App::Push<PopupList>("Language"_i18n' not in categories_cpp


def main():
    print("Running dynamic language selection and migration tests...")
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
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
