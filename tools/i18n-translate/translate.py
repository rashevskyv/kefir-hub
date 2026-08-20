"""Translate sphaira i18n strings from en.json into every language in languages.json.

One string per request: {"text": ..., "languages": [...]} -> {"code": "translation", ...},
fanned out over N threads against the local Gemini Web2API proxy.
Only keys missing from a target file are requested, so a re-run resumes where it stopped.
"""

import argparse
import codecs
import json
import re
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import requests

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = Path(__file__).resolve().parent
I18N = ROOT.parent.parent / "assets" / "romfs" / "i18n"
SRC = ROOT.parent.parent / "sphaira"
LANGS = json.loads((ROOT / "languages.json").read_text(encoding="utf-8"))
FAIL_LOG = ROOT / "failures.log"

PROMPT = """You are a translation engine for the UI of a Nintendo Switch homebrew app called sphaira.

INPUT (JSON): {{"text": "<english_ui_string>", "languages": ["<code>", ...]}}
OUTPUT (JSON only): {{"<code>": "<translation>", ...}}

LANGUAGE CODES:
{table}

RULES:
1. Output ONLY the JSON object. No markdown, no code fences, no explanations, no thinking.
2. Answer with every requested code, using exactly the codes listed above.
3. Preserve printf specifiers exactly (%s %d %u %zu %zd %.1f %.2f %%): same order, same count.
4. Preserve newlines as \\n escapes and keep the same number of lines.
5. Leave technical terms and brand names untouched: FTP, MTP, USB, NRO, NSP, NSZ, XCI, NCA,
   microSD, emuMMC, hbmenu, hekate, sphaira, Kefir, DBI, Tinfoil, HOME, A/B/X/Y/L/R/ZL/ZR.
6. These are short UI labels, buttons and dialogs: translate naturally and concisely, the way a
   native app would word it, not literally. Keep them roughly as short as the English.
7. If the text is a bare technical token, file extension or path, return it unchanged.
8. Punctuate the way the target language does: the existing zh/ja files use fullwidth 。！（）."""


def build_prompt(codes):
    table = "\n".join(f"{c} = {LANGS[c]}" for c in codes)
    return PROMPT.format(table=table)


def extract_json(content):
    """Pull a JSON object out of a model reply that may be fenced or padded with prose."""
    fence = re.search(r"```(?:json)?\s*(.*?)\s*```", content, re.DOTALL)
    if fence:
        content = fence.group(1)
    first, last = content.find("{"), content.rfind("}")
    if first < 0 or last <= first:
        raise ValueError(f"no JSON object in reply: {content[:200]!r}")
    body = re.sub(r",\s*([}\]])", r"\1", content[first:last + 1])
    return json.loads(body)


# No space flag on purpose: "100% done" is a literal percent in a UI string, not "% d".
SPEC = re.compile(r"%[-+#0-9.']*(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGaAcsp%]")


def problems(src, dst):
    """Why a translation is unusable, or None. A dropped %s is a crash, not a typo."""
    if SPEC.findall(src) != SPEC.findall(dst):
        return f"format specifiers {SPEC.findall(src)} became {SPEC.findall(dst)}"
    if src.count("\n") != dst.count("\n"):
        return f"{src.count(chr(10))} newlines became {dst.count(chr(10))}"
    return None


def retry_after(resp, default):
    """How long the proxy says to wait. It knows the real number: its egress blocks
    escalate to half an hour, so guessing 60s just means knocking again for nothing."""
    try:
        return max(0.0, float(resp.headers.get("Retry-After")))
    except (AttributeError, TypeError, ValueError):
        return default


def translate(session, args, text, codes):
    payload = {
        "model": args.model,
        "messages": [
            {"role": "system", "content": build_prompt(codes)},
            {"role": "user", "content": json.dumps({"text": text, "languages": codes}, ensure_ascii=True)},
        ],
        "stream": False,
    }
    # ensure_ascii on the wire: the proxy has mangled raw non-ASCII bodies before.
    body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
    last, waited, attempt = None, 0, 0
    while attempt <= args.retries:
        try:
            r = session.post(args.url, data=body, headers={"Content-Type": "application/json"}, timeout=args.timeout)
            r.raise_for_status()
            out = extract_json(r.json()["choices"][0]["message"]["content"])
            missing = [c for c in codes if not isinstance(out.get(c), str) or not out[c].strip()]
            if missing:
                raise ValueError(f"reply is missing {','.join(missing)}")
            bad = [f"{c}: {problems(text, out[c])}" for c in codes if problems(text, out[c])]
            if bad:
                raise ValueError("; ".join(bad))
            return {c: out[c] for c in codes}
        except Exception as e:  # noqa: BLE001 - retry anything the proxy throws at us
            # 429 means every egress IP the proxy has is benched. That is not a bad
            # answer, so wait it out instead of burning the retry budget - and wait as
            # long as the proxy's own Retry-After says, rather than guessing and
            # knocking again while the door is still shut.
            resp = getattr(e, "response", None)
            if getattr(resp, "status_code", None) == 429 and waited < args.max_wait:
                nap = min(retry_after(resp, args.cooldown), args.max_wait - waited)
                waited += nap
                print(f"  rate limited, waiting {nap:.0f}s ({waited:.0f}/{args.max_wait:.0f}s used)")
                time.sleep(nap)
                continue
            last = e
            attempt += 1
            if attempt <= args.retries:
                time.sleep(args.retry_delay * attempt)
    raise RuntimeError(last)


# The i18n files are not consistent (ru.json is 4 spaces, the rest 2; some LF, some CRLF),
# so each file's own style is remembered on load and reused on save to keep diffs clean.
_STYLE = {}


def load(code):
    path = I18N / f"{code}.json"
    if not path.exists():
        return {}
    raw = path.read_bytes().decode("utf-8")
    m = re.search(r'\r?\n( +)"', raw)
    _STYLE[code] = (len(m.group(1)) if m else 2, "\r\n" if "\r\n" in raw else "\n")
    return json.loads(raw)


def save(code, data):
    indent, eol = _STYLE.get(code, (2, "\n"))
    text = (json.dumps(data, ensure_ascii=False, indent=indent) + "\n").replace("\n", eol)
    path = I18N / f"{code}.json"
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(text.encode("utf-8"))
    tmp.replace(path)


I18N_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"\s*_i18n')


def source_strings(src=None):
    """Every "..."_i18n literal the app actually uses.

    en.json is the list of what gets translated - both here and in the app, where a key
    missing from it falls through to the raw English. So a string that never reaches
    en.json is untranslatable in all thirteen languages at once, silently.
    """
    src = src or SRC
    found = set()
    for pattern in ("**/*.cpp", "**/*.hpp"):
        for path in src.glob(pattern):
            text = path.read_text(encoding="utf-8", errors="replace")
            for m in I18N_LITERAL.finditer(text):
                try:
                    literal = codecs.decode(m.group(1), "unicode_escape")
                except Exception:  # noqa: BLE001 - an odd escape is not worth dying over
                    literal = m.group(1)
                if literal.strip():
                    found.add(literal)
    return found


def sync_en(en, src=None):
    """Add the literals the code uses but en.json lacks, and report them.

    This is why uk.json ended up bigger than English: strings were written straight
    into a translation file, so nothing else ever saw them. Keys that live only in a
    translation are left alone - most are stale wording the code moved on from, and
    reviving them would mean paying to translate strings the app never shows.
    """
    missing = sorted(source_strings(src) - set(en))
    for key in missing:
        en[key] = key
    return missing


def build_jobs(en, data, codes, force=False):
    """One job per source string: the text, plus the languages still missing it. Only
    the gaps, so an interrupted run resumes by simply being started again.

    A blank value counts as missing. The upstream files carry hundreds of keys mapped
    to "" - present, but untranslated - and treating "key exists" as "done" left them
    that way (201 of them in nl.json alone)."""
    jobs = []
    for key in en:
        todo = [c for c in codes if force or not data[c].get(key, "").strip()]
        if todo:
            jobs.append((key, en[key] or key, todo))
    return jobs


def build_parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--url", default="http://127.0.0.1:8081/v1/chat/completions")
    p.add_argument("--model", default="gemini-3.6-flash")
    p.add_argument("--threads", type=int, default=6)
    p.add_argument("--langs", help="comma separated subset, e.g. ru,uk")
    p.add_argument("--limit", type=int, help="only the first N strings (smoke test)")
    p.add_argument("--force", action="store_true", help="re-translate keys that already exist")
    p.add_argument("--retries", type=int, default=2)
    p.add_argument("--retry-delay", type=float, default=2.0)
    p.add_argument("--cooldown", type=float, default=60.0, help="how long to sit out a 429")
    p.add_argument("--max-wait", type=float, default=1800.0, help="total 429 waiting per string")
    p.add_argument("--timeout", type=int, default=180)
    p.add_argument("--flush-every", type=int, default=25)
    p.add_argument("--no-sync", action="store_true",
                   help="skip adding source literals missing from en.json")
    return p


def main():
    args = build_parser().parse_args()

    codes = [c.strip() for c in args.langs.split(",")] if args.langs else list(LANGS)
    unknown = [c for c in codes if c not in LANGS]
    if unknown:
        sys.exit(f"unknown language code(s): {', '.join(unknown)}")

    en = load("en")
    if not args.no_sync:
        added = sync_en(en)
        if added:
            save("en", en)
            print(f"en.json: added {len(added)} string(s) the code uses but English lacked")
            for key in added[:5]:
                print(f"    {key[:70]!r}")
            if len(added) > 5:
                print(f"    ... and {len(added) - 5} more")

    data = {c: load(c) for c in codes}

    jobs = build_jobs(en, data, codes, args.force)
    if args.limit:
        jobs = jobs[:args.limit]

    if not jobs:
        print("Nothing to translate: every language already has every key.")
        return

    print(f"{len(en)} source strings | {len(jobs)} need work | {len(codes)} languages | {args.threads} threads")
    print(f"proxy: {args.url} ({args.model})\n")

    lock = threading.Lock()
    state = {"done": 0, "failed": 0, "dirty": set()}
    started = time.monotonic()
    FAIL_LOG.write_text("", encoding="utf-8")

    def worker(job):
        key, text, todo = job
        session = requests.Session()
        try:
            return job, translate(session, args, text, todo), None
        except Exception as e:  # noqa: BLE001
            return job, None, e
        finally:
            session.close()

    with ThreadPoolExecutor(max_workers=args.threads) as pool:
        futures = [pool.submit(worker, j) for j in jobs]
        for f in as_completed(futures):
            (key, text, todo), out, err = f.result()
            with lock:
                state["done"] += 1
                n = state["done"]
                preview = text.replace("\n", " ")[:52]
                if err:
                    state["failed"] += 1
                    print(f"[{n:>5}/{len(jobs)}] FAIL {' '.join(todo)} | {preview} -> {err}")
                    with FAIL_LOG.open("a", encoding="utf-8") as fh:
                        fh.write(f"{key!r}\t{','.join(todo)}\t{err}\n")
                    continue
                for c in todo:
                    data[c][key] = out[c]
                state["dirty"].update(todo)
                print(f"[{n:>5}/{len(jobs)}] ok   {' '.join(todo)} | {preview}")
                if n % args.flush_every == 0:
                    for c in state["dirty"]:
                        save(c, data[c])
                    state["dirty"].clear()

    for c in state["dirty"]:
        save(c, data[c])

    took = time.monotonic() - started
    print(f"\ndone in {took / 60:.1f} min | {state['done'] - state['failed']} translated, {state['failed']} failed")
    for c in codes:
        missing = len(set(en) - set(data[c]))
        print(f"  {c}: {len(data[c]):>5} keys, {missing} still missing")
    if state["failed"]:
        print(f"\nfailures logged to {FAIL_LOG} - just re-run, missing keys are picked up again")


if __name__ == "__main__":
    main()
