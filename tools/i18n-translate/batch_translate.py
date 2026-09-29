import argparse
import codecs
import json
import re
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import urllib.request
import urllib.error

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(line_buffering=True, encoding="utf-8")
        sys.stderr.reconfigure(line_buffering=True, encoding="utf-8")
    except Exception:
        pass

ROOT = Path(__file__).resolve().parent
I18N = ROOT.parent.parent / "assets" / "romfs" / "i18n"
SRC = ROOT.parent.parent / "sphaira"
LANGS = json.loads((ROOT / "languages.json").read_text(encoding="utf-8"))

SPEC = re.compile(r"%[-+#0-9.']*(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGaAcsp%]")


def normalize_newlines(src, dst):
    if not dst or not isinstance(dst, str):
        return dst
    src_trailing = len(src) - len(src.rstrip("\n"))
    dst_trailing = len(dst) - len(dst.rstrip("\n"))
    if src_trailing > dst_trailing:
        dst = dst + ("\n" * (src_trailing - dst_trailing))
    elif dst_trailing > src_trailing:
        dst = dst.rstrip("\n") + ("\n" * src_trailing)

    src_leading = len(src) - len(src.lstrip("\n"))
    dst_leading = len(dst) - len(dst.lstrip("\n"))
    if src_leading > dst_leading:
        dst = ("\n" * (src_leading - dst_leading)) + dst
    elif dst_leading > src_leading:
        dst = ("\n" * src_leading) + dst.lstrip("\n")
    return dst


def problems(src, dst):
    if SPEC.findall(src) != SPEC.findall(dst):
        return f"format specifiers {SPEC.findall(src)} became {SPEC.findall(dst)}"
    if src.count("\n") != dst.count("\n"):
        return f"{src.count(chr(10))} newlines became {dst.count(chr(10))}"
    return None


def extract_json(content):
    fence = re.search(r"```(?:json)?\s*(.*?)\s*```", content, re.DOTALL)
    if fence:
        content = fence.group(1)
    first, last = content.find("{"), content.rfind("}")
    if first < 0 or last <= first:
        # maybe an array?
        first_arr, last_arr = content.find("["), content.rfind("]")
        if 0 <= first_arr < last_arr:
            body = re.sub(r",\s*([}\]])", r"\1", content[first_arr:last_arr + 1])
            arr = json.loads(body)
            res = {}
            for i, item in enumerate(arr):
                if isinstance(item, dict):
                    idx = str(item.get("id", i))
                    res[idx] = item
            return res
        raise ValueError(f"no JSON object or array in reply: {content[:200]!r}")
    body = re.sub(r",\s*([}\]])", r"\1", content[first:last + 1])
    return json.loads(body)


def request_batch(url, model, texts, codes, timeout=90):
    lang_desc = ", ".join(f"{c} ({LANGS[c]})" for c in codes)
    system_prompt = (
        f"You are a translation engine for the UI of a Nintendo Switch homebrew app called sphaira / Kefir Hub.\n"
        f"Translate the provided numbered list of English UI strings into: {lang_desc}.\n\n"
        f"INPUT FORMAT:\n"
        f"[{{\"id\": 0, \"text\": \"...\"}}, {{\"id\": 1, \"text\": \"...\"}}, ...]\n\n"
        f"OUTPUT FORMAT (JSON ONLY):\n"
        f"{{\"0\": {{\"{codes[0]}\": \"...\"{', ...' if len(codes) > 1 else ''}}}, \"1\": {{...}}}}\n\n"
        f"RULES:\n"
        f"1. Output ONLY a valid JSON object keyed by the string \"id\" of each item.\n"
        f"2. Every requested code ({', '.join(codes)}) must be present in each item's object.\n"
        f"3. Preserve printf specifiers (%s, %d, %u, %zu, %zd, %.1f, %.2f, %%) exactly in count and order.\n"
        f"4. Preserve newlines (\\n) and whitespace structure.\n"
        f"5. Technical terms, paths, file extensions, buttons (A, B, X, Y, L, R, ZL, ZR, HOME), and brands "
        f"(FTP, MTP, USB, NRO, NSP, NSZ, XCI, NCA, microSD, emuMMC, hbmenu, hekate, sphaira, Kefir, DBI, Tinfoil) "
        f"must remain untouched.\n"
        f"6. Do not include markdown code blocks or explanations."
    )

    items = [{"id": i, "text": t} for i, t in enumerate(texts)]

    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": json.dumps(items, ensure_ascii=False)}
        ],
        "stream": False
    }

    req = urllib.request.Request(
        url,
        data=json.dumps(payload, ensure_ascii=True).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=timeout) as res:
        res_data = json.load(res)
    content = res_data["choices"][0]["message"]["content"]
    return extract_json(content)


def load(code):
    path = I18N / f"{code}.json"
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def save(code, data):
    text = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").replace("\r\n", "\n").replace("\n", "\r\n")
    path = I18N / f"{code}.json"
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(text.encode("utf-8"))
    tmp.replace(path)


def run_group(codes, url, model, threads, batch_size, limit=None):
    en = load("en")
    data = {c: load(c) for c in codes}

    needed_keys = []
    for k in en:
        if any(not data[c].get(k, "").strip() for c in codes):
            needed_keys.append(k)

    if limit:
        needed_keys = needed_keys[:limit]

    print(f"\n=== Translating {codes} | {len(needed_keys)} keys needed | batch size: {batch_size} | threads: {threads} ===")
    if not needed_keys:
        print(f"All keys already present for {codes}!")
        return

    batches = [needed_keys[i:i + batch_size] for i in range(0, len(needed_keys), batch_size)]
    print(f"Total batches: {len(batches)}")

    lock = threading.Lock()
    done_batches = 0
    total_translated = 0

    def process_batch(batch_idx, batch_keys):
        nonlocal done_batches, total_translated
        attempts = 0
        while attempts < 3:
            attempts += 1
            try:
                out = request_batch(url, model, batch_keys, codes)
                valid_count = 0
                with lock:
                    for i, k in enumerate(batch_keys):
                        entry = out.get(str(i)) or out.get(i) or out.get(k)
                        if entry and isinstance(entry, dict):
                            for c in codes:
                                val = entry.get(c, "")
                                if val and isinstance(val, str):
                                    val = normalize_newlines(k, val)
                                    if not problems(k, val):
                                        data[c][k] = val
                            valid_count += 1
                    done_batches += 1
                    total_translated += valid_count
                    print(f"[{done_batches:>3}/{len(batches)}] Batch {batch_idx + 1:>3} done ({valid_count}/{len(batch_keys)} valid)")
                    for c in codes:
                        save(c, data[c])
                return True
            except Exception as e:
                print(f"Batch {batch_idx + 1} attempt {attempts} failed: {e}")
                time.sleep(2 * attempts)
        return False

    with ThreadPoolExecutor(max_workers=threads) as pool:
        futures = [pool.submit(process_batch, i, b) for i, b in enumerate(batches)]
        for f in as_completed(futures):
            pass

    for c in codes:
        save(c, data[c])
        missing = sum(1 for k in en if not data[c].get(k, "").strip())
        print(f"  {c}: {len(data[c])} keys, {missing} missing")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8081/v1/chat/completions")
    parser.add_argument("--model", default="gemini-3.7-flash")
    parser.add_argument("--threads", type=int, default=6)
    parser.add_argument("--batch-size", type=int, default=25)
    parser.add_argument("--langs", help="comma-separated languages, e.g. be,kk,pl,tr")
    parser.add_argument("--all-remaining", action="store_true", help="run all remaining language groups")
    parser.add_argument("--limit", type=int, help="limit number of strings")
    args = parser.parse_args()

    if args.all_remaining:
        # Groups ordered logically:
        # First existing languages missing ~750 keys
        groups = [
            ["es", "es419"],
            ["fr", "frca"],
            ["pt", "ptbr"],
            ["zh", "zhtw"],
            ["it"],
            ["ja"],
            ["ko"],
            ["nl"],
            ["se"],
            ["vi"],
            # Then new languages missing ~2807 keys
            ["pl"],
            ["tr"],
            ["be"],
            ["kk"],
            ["id"],
            ["et"],
            ["lt"],
            ["lv"],
        ]
        for g in groups:
            run_group(g, args.url, args.model, args.threads, args.batch_size, args.limit)
        print("\nAll remaining groups completed!")
        return

    codes = [c.strip() for c in args.langs.split(",")] if args.langs else list(LANGS)
    run_group(codes, args.url, args.model, args.threads, args.batch_size, args.limit)


if __name__ == "__main__":
    main()
