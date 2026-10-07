"""Turn the draft blocks the coding agent left in docs/site pages into finished prose through the LLM proxy.

    python tools/docs/finish.py [--root docs/site] [--url URL] [--model MODEL] [--dry-run]

A draft block is an HTML comment that starts with "draft":
    <!-- draft
    - [[Saves]] now shows one tile per game; badges name the save types (Device, BCAT, Cache)
    - the Restore dialog asks for the source account first
    -->
The coding agent writes the facts (UI names as [[en.json key]], shot markers as usual) and the proxy writes
the page in the page's language, following docs/site/STYLE.md. The reply replaces the page only when it
passes validate() (no draft left, the same shot markers, no label that was not in the page or the draft,
not much shorter than before) and a second, independent request to the proxy reviews the diff against the
draft facts (facts in, nothing invented, unrelated lines untouched). A rejection feeds the reviewer's
problems into the next attempt, three attempts in all. The label contract test and the strict mkdocs
build run after this in tools/docs/publish.ps1. Exit 0 = nothing to do or every page finished, 1 = a page was rejected or the proxy
is down (start D:\\git\\dev\\gemini-web2api\\run.bat).
"""
import argparse
import difflib
import json
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DRAFT = re.compile(r"<!--\s*draft\b(.*?)-->", re.S)
SHOT = re.compile(r"<!--\s*shot:.*?-->", re.S)
LABEL = re.compile(r"\[\[([^\]]+)\]\]")
LANG_NAME = {"en": "English", "uk": "Ukrainian"}


def find_drafts(text):
    """The draft blocks of a page, as stripped strings."""
    return [m.group(1).strip() for m in DRAFT.finditer(text)]


def strip_fence(reply):
    """The proxy sometimes wraps the page in a ```markdown fence; unwrap it."""
    m = re.fullmatch(r"\s*```[a-zA-Z]*\n(.*?)\n```\s*", reply, re.S)
    return m.group(1) + "\n" if m else reply


def validate(old, new):
    """Why the new page must be rejected, or None when it is fine."""
    if find_drafts(new):
        return "a draft block is still there"
    if SHOT.findall(new) != SHOT.findall(old):
        return "shot markers changed"
    allowed = set(LABEL.findall(old))
    stray = sorted(set(LABEL.findall(new)) - allowed)
    if stray:
        return f"labels not in the page or the draft: {stray}"
    if len(new) < len(old) * 0.5:
        return "the page became much shorter"
    if not new.lstrip().startswith("#"):
        return "the page does not start with a heading"
    return None


def other_changes(old, new):
    """How many lines outside the draft blocks the proxy changed or dropped. The prose for a draft may
    land in an existing step, so this is reported, not rejected: the docs: commit is there to review."""
    kept = {line for line in DRAFT.sub("", old).splitlines() if line.strip()}
    return sum(1 for line in kept if line not in new.splitlines())


def prompt(lang, style, page):
    system = (
        f"You finish the user documentation of Kefir Hub, a Nintendo Switch homebrew app. The page below is "
        f"written in {LANG_NAME.get(lang, lang)} and contains one or more <!-- draft ... --> blocks with facts "
        f"written by the developer. Rewrite the page so the facts become finished prose in the page's language, "
        f"in the place of each draft block, following the style guide exactly.\n\n"
        f"Rules:\n"
        f"1. Output the whole page as Markdown and nothing else: no code fence, no explanation.\n"
        f"2. Remove every draft block. Keep every <!-- shot: ... --> comment exactly where and as it is.\n"
        f"3. [[Label]] is a UI name. Use only labels that already appear in the page or in a draft block; "
        f"never invent, translate or alter one.\n"
        f"4. Keep the parts of the page the drafts do not touch unchanged, word for word.\n"
        f"5. Headings: keep the existing ones; add a heading only when a draft asks for a new section.\n"
        f"6. Do not add facts that are not in the drafts or the page.\n\n"
        f"Style guide (docs/site/STYLE.md):\n\n{style}"
    )
    return system, page


def request(url, model, system, user, timeout=180):
    payload = {"model": model, "stream": False,
               "messages": [{"role": "system", "content": system}, {"role": "user", "content": user}]}
    req = urllib.request.Request(url, data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as res:
        return json.load(res)["choices"][0]["message"]["content"]


def review_diff(old, new):
    """What the writer changed: unified diff from the page without its draft blocks to the new page."""
    before = DRAFT.sub("", old).splitlines()
    return "\n".join(difflib.unified_diff(before, new.splitlines(), "before", "after", lineterm="", n=1))


def review_prompt(lang, drafts, diff):
    system = (
        f"You review one edit of a {LANG_NAME.get(lang, lang)} documentation page of Kefir Hub, a Nintendo Switch "
        f"homebrew app. A writer had to turn the developer's draft facts into prose in that page. You get the "
        f"draft facts and the unified diff of the writer's edit. Reject the edit when:\n"
        f"1. a fact from the drafts is missing or changed in meaning;\n"
        f"2. the writer added a fact that is not in the drafts;\n"
        f"3. a removed line (-) that the drafts do not concern is not restored unchanged in a (+) line: "
        f"any altered word, typo, dropped sentence or re-wording of unrelated text is a defect;\n"
        f"4. the new text is not in {LANG_NAME.get(lang, lang)}, or a [[Label]] was translated or altered.\n"
        f"Reply with JSON only: {{\"ok\": true}} or {{\"ok\": false, \"problems\": [\"...\", ...]}}, each problem "
        f"one short sentence that quotes the offending text."
    )
    user = "DRAFT FACTS:\n" + "\n\n".join(drafts) + "\n\nDIFF:\n" + diff
    return system, user


def parse_verdict(reply):
    """(ok, problems) from the reviewer's reply; an unreadable reply counts as a rejection."""
    first, last = reply.find("{"), reply.rfind("}")
    try:
        v = json.loads(reply[first:last + 1])
        return bool(v.get("ok")), [str(p) for p in v.get("problems", [])]
    except (ValueError, AttributeError):
        return False, [f"unreadable verdict: {reply[:120]!r}"]


def finish_page(path, lang, style, args):
    old = path.read_text(encoding="utf-8")
    drafts = find_drafts(old)
    if not drafts:
        return True
    print(f"{path.relative_to(args.root)}: {len(drafts)} draft block(s)")
    if args.dry_run:
        return True
    system, user = prompt(lang, style, old)
    feedback = ""
    for attempt in (1, 2, 3):
        new = strip_fence(request(args.url, args.model, system, user + feedback)).replace("\r\n", "\n").rstrip("\n") + "\n"
        why = validate(old, new)
        if why is None:
            # a second request reviews the first one's work before the page is replaced
            ok, problems = parse_verdict(request(args.url, args.model, *review_prompt(lang, drafts, review_diff(old, new))))
            if ok:
                path.write_text(new, encoding="utf-8", newline="\n")
                print(f"  finished ({len(old)} -> {len(new)} chars, {other_changes(old, new)} other line(s) changed, reviewed)")
                return True
            why = "reviewer: " + "; ".join(problems)
            feedback = "\n\nA reviewer rejected your previous attempt. Fix these problems:\n- " + "\n- ".join(problems)
        print(f"  attempt {attempt} rejected: {why}")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=ROOT / "docs" / "site")
    ap.add_argument("--url", default="http://127.0.0.1:8081/v1/chat/completions")
    ap.add_argument("--model", default="gemini-3.7-flash")
    ap.add_argument("--dry-run", action="store_true", help="list the pages with drafts, do not call the proxy")
    args = ap.parse_args()
    style = (args.root / "STYLE.md").read_text(encoding="utf-8")
    ok = True
    try:
        for lang_dir in sorted(p for p in args.root.iterdir() if p.is_dir() and (p / "index.md").exists()):
            for page in sorted(lang_dir.rglob("*.md")):
                ok &= finish_page(page, lang_dir.name, style, args)
    except urllib.error.URLError as e:
        print(f"proxy {args.url} unreachable ({e.reason}); start D:\\git\\dev\\gemini-web2api\\run.bat")
        return 1
    if ok:
        print("drafts: done")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
