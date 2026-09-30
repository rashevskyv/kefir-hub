"""Source contract for Kefir Hub i18n deployment and translation completeness."""

import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
I18N = ROOT / "assets/romfs/i18n"
LANGUAGES_JSON = ROOT / "tools/i18n-translate/languages.json"
SPEC = re.compile(r"%[-+#0-9.']*(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGaAcsp%]")

ALLOWED_IDENTICAL = {
    'USB 2.0 High Speed (480 Mbps)',
    'USB 3.0 SuperSpeed (5 Gbps)',
    'USB 3.0 Enabled · Link: USB 2.0 High Speed (480 Mbps)',
    'USB 3.0 Enabled Â· Link: USB 2.0 High Speed (480 Mbps)',
    'USB 3.0 Enabled',
    'USB 2.0',
    'USB 3.0',
    'Rank: %s · Index: %u',
    'Rank: %s Â· Index: %u',
    'Save ID: %016lX',
    'Page %zu / %zu',
    'Page %ld / %ld',
    'Homebrew App Store',
    'UAModDownloader',
    'SimpleModDownloader',
    'SteamGridDB',
    'Kefir Cheats',
    'Kefir Settings',
    'Ownfoil Server',
    '/dev/null (Speed Test)',
    'microSD card (/dumps/)',
    'WebDAV → SD',
    'Local → WebDAV',
    'WebDAV â\x86\x92 SD',
    '60FPS/GFX Cheats',
    'Switch-Handheld!',
    'Switch-Docked!'
}


def test_ru_json_absent():
    assert not (I18N / "ru.json").exists(), "ru.json must not exist in assets/romfs/i18n"


def test_all_25_languages_present():
    langs = json.loads(LANGUAGES_JSON.read_text(encoding="utf-8"))
    assert len(langs) == 25, f"Expected 25 target languages, found {len(langs)}"
    for code in langs:
        file_path = I18N / f"{code}.json"
        assert file_path.exists(), f"Missing translation file: {code}.json"


def test_key_completeness():
    en = json.loads((I18N / "en.json").read_text(encoding="utf-8"))
    langs = json.loads(LANGUAGES_JSON.read_text(encoding="utf-8"))
    missing_report = {}
    for code in langs:
        data = json.loads((I18N / f"{code}.json").read_text(encoding="utf-8"))
        missing = [k for k in en if not data.get(k, "").strip()]
        if missing:
            missing_report[code] = len(missing)
    assert not missing_report, f"Languages with missing or empty keys: {missing_report}"


def test_language_name_metadata_present():
    en = json.loads((I18N / "en.json").read_text(encoding="utf-8"))
    assert "__language_name" in en and en["__language_name"].strip()
    langs = json.loads(LANGUAGES_JSON.read_text(encoding="utf-8"))
    for code in langs:
        data = json.loads((I18N / f"{code}.json").read_text(encoding="utf-8"))
        assert "__language_name" in data, f"Missing __language_name in {code}.json"
        assert isinstance(data["__language_name"], str) and data["__language_name"].strip(), f"Empty __language_name in {code}.json"


def test_printf_specifiers_and_newlines():
    en = json.loads((I18N / "en.json").read_text(encoding="utf-8"))
    langs = json.loads(LANGUAGES_JSON.read_text(encoding="utf-8"))
    spec_errors = []
    newline_errors = []
    for code in langs:
        data = json.loads((I18N / f"{code}.json").read_text(encoding="utf-8"))
        for k in en:
            if k.startswith("__"):
                continue
            v = data.get(k, "")
            if v:
                if SPEC.findall(k) != SPEC.findall(v):
                    spec_errors.append(f"{code} '{k}': {SPEC.findall(k)} vs {SPEC.findall(v)}")
                if k.count("\n") != v.count("\n"):
                    newline_errors.append(f"{code} '{k}': {k.count(chr(10))} vs {v.count(chr(10))}")
    assert not spec_errors, f"{len(spec_errors)} specifier errors:\n" + "\n".join(spec_errors[:5])
    assert not newline_errors, f"{len(newline_errors)} newline errors:\n" + "\n".join(newline_errors[:5])


def test_no_untranslated_english_sentences():
    en = json.loads((I18N / "en.json").read_text(encoding="utf-8"))
    langs = json.loads(LANGUAGES_JSON.read_text(encoding="utf-8"))
    offenders = []
    for code in langs:
        if code == "engb":
            continue
        data = json.loads((I18N / f"{code}.json").read_text(encoding="utf-8"))
        for k, v in data.items():
            if k.startswith("__"):
                continue
            if k in en and v == en[k]:
                if k in ALLOWED_IDENTICAL:
                    continue
                words = [w for w in k.split() if len(w) > 1]
                if len(k) >= 40 or (len(words) >= 4 and any(p in k for p in '.!?')):
                    offenders.append(f"{code}: [{len(k)}] {k[:70]!r}")
    assert not offenders, f"Found {len(offenders)} untranslated English sentences:\n" + "\n".join(offenders[:10])


if __name__ == "__main__":
    test_ru_json_absent()
    test_all_25_languages_present()
    test_language_name_metadata_present()
    test_key_completeness()
    test_printf_specifiers_and_newlines()
    test_no_untranslated_english_sentences()
    print("All i18n deployment contracts verified successfully.")
