"""Draft subtitles from a video script: docs/video/<NN-topic>/script.md -> subs.uk.srt, subs.en.srt.

    python docs/video/srt.py            all videos
    python docs/video/srt.py 03-saves   one video

The script table has the columns | # | Shot | Action | Voiceover (UK) | Voiceover (EN) |.
Timing is an estimate from reading speed (15 chars/s, at least 2 s per cue). After recording,
re-time against the real voice track (e.g. align the .srt with Whisper or in Subtitle Edit);
the text stays the same.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CPS, MIN_S, MAX_CHARS = 15.0, 2.0, 84


def rows(script):
    for line in script.read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) == 5 and cells[0].isdigit():
            yield cells[3], cells[4]


def chunks(text):
    """Split a voiceover line into cues of at most MAX_CHARS, at sentence then word boundaries."""
    out = []
    for sentence in re.split(r"(?<=[.!?…])\s+", text):
        words, cur = sentence.split(), ""
        for w in words:
            if cur and len(cur) + 1 + len(w) > MAX_CHARS:
                out.append(cur)
                cur = w
            else:
                cur = f"{cur} {w}".strip()
        if cur:
            out.append(cur)
    return out


def stamp(t):
    ms = round(t * 1000)
    return f"{ms // 3600000:02}:{ms // 60000 % 60:02}:{ms // 1000 % 60:02},{ms % 1000:03}"


def write_srt(cues, path):
    t, out = 0.0, []
    for n, text in enumerate(cues, 1):
        d = max(MIN_S, len(text) / CPS)
        out.append(f"{n}\n{stamp(t)} --> {stamp(t + d)}\n{text}\n")
        t += d
    path.write_text("\n".join(out), encoding="utf-8")
    return t


def main(names):
    dirs = [ROOT / n for n in names] or sorted(p for p in ROOT.iterdir() if (p / "script.md").exists())
    for d in dirs:
        table = list(rows(d / "script.md"))
        for col, lang in ((0, "uk"), (1, "en")):
            cues = [c for r in table for c in chunks(r[col])]
            total = write_srt(cues, d / f"subs.{lang}.srt")
            print(f"{d.name}: {lang} {len(cues)} cues, ~{total / 60:.1f} min")


if __name__ == "__main__":
    main(sys.argv[1:])
