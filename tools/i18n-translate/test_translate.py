"""Tests for the i18n translation pipeline.

    python test_translate.py          offline only
    python test_translate.py --live   also sends real requests to the proxy

Live tests print every translation they get back so the output can be eyeballed.
"""

import json
import shutil
import sys
import tempfile
from pathlib import Path

import requests

import translate as t

FAILURES = []


def check(name, fn):
    try:
        fn()
        print(f"  PASS  {name}")
    except Exception as e:  # noqa: BLE001 - a failing test must not stop the rest
        FAILURES.append(f"{name}: {e}")
        print(f"  FAIL  {name}: {e}")


def raises(exc, fn):
    try:
        fn()
    except exc:
        return
    raise AssertionError(f"expected {exc.__name__}")


# ── offline ──────────────────────────────────────────────────────────

def test_extract_plain():
    assert t.extract_json('{"ru": "Привет"}') == {"ru": "Привет"}


def test_extract_fenced():
    assert t.extract_json('```json\n{"ru": "Привет"}\n```') == {"ru": "Привет"}


def test_extract_with_prose():
    assert t.extract_json('Sure, here you go:\n{"ru": "a", "uk": "b"}\nHope that helps!') == {"ru": "a", "uk": "b"}


def test_extract_trailing_comma():
    assert t.extract_json('{"ru": "a", "uk": "b",}') == {"ru": "a", "uk": "b"}


def test_extract_keeps_braces_in_value():
    assert t.extract_json('{"ru": "a {x} b"}') == {"ru": "a {x} b"}


def test_extract_rejects_garbage():
    raises(ValueError, lambda: t.extract_json("I cannot translate that."))


def test_prompt_names_the_language():
    # "se" is Swedish here, not Northern Sami - the model only knows that from the table.
    assert "se = Swedish" in t.build_prompt(["se"])
    assert "zh = Chinese (Simplified)" in t.build_prompt(["zh"])


def test_problems_accepts_good():
    assert t.problems("Page %zu / %zu", "Сторінка %zu / %zu") is None
    assert t.problems("a\nb", "а\nб") is None
    assert t.problems("100% done", "100% готово") is None  # a literal percent, not "% d"
    assert t.problems("Fan 100%", "Вентилятор 100%") is None
    assert t.problems("%zu hours %zu minutes remaining", "залишилось %zu год %zu хв") is None


def test_problems_catches_dropped_specifier():
    assert t.problems("Page %zu / %zu", "Сторінка %zu") is not None


def test_problems_catches_swapped_specifiers():
    # printf has no positional args, so reordering %s and %d reads the wrong argument.
    assert t.problems("%s: %d", "%d: %s") is not None


def test_problems_catches_mangled_specifier():
    assert t.problems("%.1f GB", "%.1 f GB") is not None
    assert t.problems("%zu files", "%u files") is not None


def test_problems_catches_lost_newline():
    assert t.problems("line one\nline two", "рядок один рядок два") is not None


def test_languages_match_the_app():
    # every code in languages.json must be one the app actually loads, and have a file
    src = (Path(t.__file__).parent.parent.parent / "sphaira" / "source" / "i18n.cpp").read_text(encoding="utf-8")
    for code in t.LANGS:
        assert f'"{code}"' in src, f"i18n.cpp never selects {code}"
        assert (t.I18N / f"{code}.json").exists(), f"{code}.json missing"
    assert "en" not in t.LANGS, "en.json is the source, not a target"


def test_style_roundtrip_is_byte_identical():
    for code in t.LANGS:
        path = t.I18N / f"{code}.json"
        before = path.read_bytes()
        t.save(code, t.load(code))
        assert path.read_bytes() == before, f"{code}.json changed on a no-op round-trip"


def test_style_detection():
    assert t._STYLE["ru"][0] == 4, "ru.json is indented with 4 spaces"
    assert all(s[0] == 2 for c, s in t._STYLE.items() if c != "ru"), "the rest use 2"


def test_save_appends_without_touching_existing():
    with tempfile.TemporaryDirectory() as tmp:
        real, t.I18N = t.I18N, Path(tmp)
        try:
            (t.I18N / "ru.json").write_bytes('{\r\n    "old": "старий"\r\n}\r\n'.encode("utf-8"))
            data = t.load("ru")
            data["new"] = "новий"
            t.save("ru", data)
            raw = (t.I18N / "ru.json").read_bytes().decode("utf-8")
            assert raw == '{\r\n    "old": "старий",\r\n    "new": "новий"\r\n}\r\n', repr(raw)
        finally:
            t.I18N = real


class FakeResponse:
    def __init__(self, status, content="", headers=None):
        self.status_code, self._content = status, content
        self.headers = headers or {}

    def raise_for_status(self):
        if self.status_code != 200:
            raise requests.HTTPError(f"{self.status_code} error", response=self)

    def json(self):
        return {"choices": [{"message": {"content": self._content}}]}


class FakeSession:
    """Replays a canned list of responses and records how many requests were made."""

    def __init__(self, *responses):
        self.responses, self.calls = list(responses), 0

    def post(self, *_a, **_kw):
        self.calls += 1
        return self.responses.pop(0)


def offline_args(**kw):
    args = t.build_parser().parse_args([])
    args.cooldown, args.retry_delay = 0, 0
    for k, v in kw.items():
        setattr(args, k, v)
    return args


def test_rate_limit_is_waited_out_not_retried_away():
    ok = FakeResponse(200, '{"ru": "Привіт"}')
    s = FakeSession(FakeResponse(429), FakeResponse(429), FakeResponse(429), FakeResponse(429), ok)
    # 4 x 429 with only 2 retries allowed: a 429 must not consume the retry budget
    assert t.translate(s, offline_args(retries=2), "Hi", ["ru"]) == {"ru": "Привіт"}
    assert s.calls == 5


def test_rate_limit_honours_retry_after():
    slept = []
    real_sleep, t.time.sleep = t.time.sleep, slept.append
    try:
        s = FakeSession(FakeResponse(429, headers={"Retry-After": "900"}),
                        FakeResponse(200, '{"ru": "Привіт"}'))
        assert t.translate(s, offline_args(retries=0), "Hi", ["ru"]) == {"ru": "Привіт"}
        assert slept == [900], f"waited {slept} instead of the 900s the proxy asked for"
    finally:
        t.time.sleep = real_sleep


def test_retry_after_falls_back_when_absent_or_junk():
    assert t.retry_after(FakeResponse(429), 60) == 60
    assert t.retry_after(FakeResponse(429, headers={"Retry-After": "Wed, 21 Oct 2026 07:28:00 GMT"}), 60) == 60
    assert t.retry_after(FakeResponse(429, headers={"Retry-After": "30"}), 60) == 30


def test_retry_after_never_exceeds_the_wait_budget():
    slept = []
    real_sleep, t.time.sleep = t.time.sleep, slept.append
    try:
        s = FakeSession(FakeResponse(429, headers={"Retry-After": "9000"}), FakeResponse(429))
        raises(RuntimeError, lambda: t.translate(s, offline_args(retries=0, max_wait=100), "Hi", ["ru"]))
        assert slept == [100], f"slept {slept}, past the budget"
    finally:
        t.time.sleep = real_sleep


def test_rate_limit_gives_up_once_the_wait_budget_is_spent():
    s = FakeSession(*[FakeResponse(429) for _ in range(10)])
    raises(RuntimeError, lambda: t.translate(s, offline_args(retries=0, cooldown=1, max_wait=2), "Hi", ["ru"]))


def test_server_error_does_consume_retries():
    s = FakeSession(FakeResponse(500), FakeResponse(500), FakeResponse(500))
    raises(RuntimeError, lambda: t.translate(s, offline_args(retries=2), "Hi", ["ru"]))
    assert s.calls == 3


def test_bad_translation_is_retried_then_accepted():
    bad = FakeResponse(200, '{"ru": "Сторінка 1"}')   # dropped both %zu
    good = FakeResponse(200, '{"ru": "Сторінка %zu / %zu"}')
    s = FakeSession(bad, good)
    assert t.translate(s, offline_args(retries=2), "Page %zu / %zu", ["ru"]) == {"ru": "Сторінка %zu / %zu"}
    assert s.calls == 2


def test_every_translation_on_disk_is_safe():
    """Audit the whole corpus for printf mismatches: those misread the varargs at runtime.

    Only specifiers, not line counts: hand-written translations legitimately rewrap text,
    and empty values are skipped because i18n.cpp falls back to English on a zero-length value.
    """
    en = json.loads((t.I18N / "en.json").read_text(encoding="utf-8"))
    offenders = []
    for code in t.LANGS:
        for key, value in t.load(code).items():
            src = en.get(key)
            if src is None or not value:
                continue
            if t.SPEC.findall(src or key) != t.SPEC.findall(value):
                offenders.append(f"{code}: {key!r} -> {value!r}")
    for line in offenders:
        print(f"        {line}")
    assert not offenders, f"{len(offenders)} translations misuse printf specifiers"


def test_jobs_only_cover_missing_keys():
    en = {"a": "A", "b": "B", "no value": ""}
    data = {"ru": {"a": "А"}, "uk": {"a": "А", "b": "Б", "no value": "-"}}
    # only the gaps, and a key with no English value is its own source text
    assert t.build_jobs(en, data, ["ru", "uk"]) == [("b", "B", ["ru"]), ("no value", "no value", ["ru"])]


def test_source_literals_are_extracted():
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp)
        (src / "a.cpp").write_text(
            'Widget("Install failed!"_i18n);\n'
            'auto x = "Page %zu / %zu"_i18n;\n'
            'log("not translated");\n'
            'auto e = ""_i18n;\n'
            'auto n = "Line one\\nLine two"_i18n;\n'
            'auto q = "He said \\"no\\""_i18n;\n', encoding="utf-8")
        (src / "b.hpp").write_text('static auto T = "From a header"_i18n;\n', encoding="utf-8")
        found = t.source_strings(src)
    assert found == {"Install failed!", "Page %zu / %zu", "Line one\nLine two",
                     'He said "no"', "From a header"}, found


def test_sync_adds_only_what_the_code_uses():
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp)
        (src / "a.cpp").write_text('"New string"_i18n; "Already there"_i18n;\n', encoding="utf-8")
        en = {"Already there": "Already there", "Old wording": "Old wording"}
        added = t.sync_en(en, src)
    assert added == ["New string"]
    assert en["New string"] == "New string", "a new key is its own English text"
    assert en["Old wording"] == "Old wording", "sync must not remove anything"


def test_every_source_literal_reaches_en_json():
    """A literal the code uses but en.json lacks is untranslatable in all thirteen
    languages at once - and silently, because the app falls back to raw English."""
    en = t.load("en")
    missing = sorted(t.source_strings() - set(en))
    for key in missing[:10]:
        print(f"        {key[:70]!r}")
    assert not missing, f"{len(missing)} source strings never reach en.json"


def test_a_blank_translation_counts_as_missing():
    """The upstream files map hundreds of keys to "" - present, but untranslated."""
    en = {"a": "A", "b": "B", "c": "C"}
    data = {"nl": {"a": "", "b": "   ", "c": "C-nl"}}
    assert t.build_jobs(en, data, ["nl"]) == [("a", "A", ["nl"]), ("b", "B", ["nl"])]


def test_force_rebuilds_every_key():
    en = {"a": "A", "b": "B"}
    data = {"ru": {"a": "А", "b": "Б"}}
    assert t.build_jobs(en, data, ["ru"]) == []
    assert t.build_jobs(en, data, ["ru"], force=True) == [("a", "A", ["ru"]), ("b", "B", ["ru"])]


def test_jobs_resume_a_finished_corpus_as_a_no_op():
    en = json.loads((t.I18N / "en.json").read_text(encoding="utf-8"))
    data = {c: t.load(c) for c in t.LANGS}
    for key, _text, todo in t.build_jobs(en, data, list(t.LANGS)):
        for c in todo:
            assert not data[c].get(key, "").strip(), f"{key!r} queued for {c}, which has it"


# ── live: real requests through the proxy ────────────────────────────

def live_args():
    args = t.build_parser().parse_args([])
    args.retries = 1
    return args


def show(text, out):
    print(f"        source: {text!r}")
    for code, value in out.items():
        print(f"        {code}: {value}")


def live(text, codes=None):
    codes = codes or list(t.LANGS)
    with requests.Session() as s:
        out = t.translate(s, live_args(), text, codes)
    show(text, out)
    return out


def test_live_all_languages_answer():
    out = live("Delete this file?")
    assert list(out) == list(t.LANGS), f"got {list(out)}"
    assert all(v.strip() for v in out.values())
    # a real translation, not the English echoed back
    for code in ("ru", "uk", "ja", "zh", "ko"):
        assert out[code] != "Delete this file?", f"{code} was not translated"
        assert not out[code].isascii(), f"{code} came back as ASCII: {out[code]!r}"


def test_live_keeps_format_specifiers():
    text = "Page %zu / %zu"
    out = live(text)
    for code, value in out.items():
        assert t.problems(text, value) is None, f"{code}: {t.problems(text, value)}"


def test_live_keeps_mixed_specifiers():
    text = "Filter: %s | Sort: %s | Order: %s"
    out = live(text)
    for code, value in out.items():
        assert value.count("%s") == 3, f"{code}: {value!r}"


def test_live_keeps_float_specifier():
    text = "microSD card %.1f GB"
    out = live(text)
    for code, value in out.items():
        assert "%.1f" in value, f"{code}: {value!r}"


def test_live_keeps_line_structure():
    text = "Install disabled...\nPlease enable installing via the install options."
    out = live(text)
    for code, value in out.items():
        assert value.count("\n") == 1, f"{code}: {value!r}"


def test_live_keeps_technical_terms():
    text = "Install games from a PC over USB: DBI Backend, ns-usbloader and fluffy."
    out = live(text)
    for code, value in out.items():
        for term in ("USB", "DBI", "fluffy"):
            assert term in value, f"{code} lost {term}: {value!r}"


def test_live_leaves_bare_tokens_alone():
    out = live("FTP", ["ru", "ja", "zh", "uk"])
    for code, value in out.items():
        assert value.upper().startswith("FTP"), f"{code}: {value!r}"


def test_live_rejects_a_broken_reply():
    # the retry path must trigger on a reply that drops a specifier, not silently accept it
    assert t.problems("Page %zu / %zu", "Сторінка 1 / 2") is not None


def test_live_survives_a_long_string():
    text = ("Only for consoles with physically soldered 8GB RAM. Other consoles will not boot correctly.\n"
            "To disable it if the console does not boot:\n"
            "hekate > payloads > TegraExplorer > Remove_8GB-RAM_config.te")
    out = live(text, ["ru", "de", "ja"])
    for code, value in out.items():
        assert value.count("\n") == 2, f"{code}: {value!r}"
        assert "Remove_8GB-RAM_config.te" in value, f"{code} mangled the filename: {value!r}"


def test_live_unknown_model_fails_loudly():
    args = live_args()
    args.model = "definitely-not-a-model"
    args.retries = 0
    with requests.Session() as s:
        raises(RuntimeError, lambda: t.translate(s, args, "Hello", ["ru"]))


def main():
    offline = [v for k, v in sorted(globals().items()) if k.startswith("test_") and not k.startswith("test_live_")]
    print(f"offline ({len(offline)} tests)")
    for fn in offline:
        check(fn.__name__, fn)

    if "--live" in sys.argv:
        proxy = [v for k, v in sorted(globals().items()) if k.startswith("test_live_")]
        print(f"\nlive ({len(proxy)} tests, real proxy requests)")
        for fn in proxy:
            check(fn.__name__, fn)
    else:
        print("\nskipping live tests (pass --live to run them)")

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED:")
        for f in FAILURES:
            print(f"  - {f}")
        sys.exit(1)
    print("all green")


if __name__ == "__main__":
    main()
