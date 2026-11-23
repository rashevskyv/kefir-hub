#!/usr/bin/env python3
"""Bring the robot's Prelude-Nextendo releases from origin/master into the work branch (dev).

The robot (.github/workflows/prelude-sync.yml) commits to master and bumps sphaira_VERSION there, so every
merge conflicts on two places: the version line and the top of docs/dev/CHANGELOG.md. This script resolves
exactly those two (the higher version wins, both CHANGELOG entries are kept) and stops on any other conflict.

  python tools/dev/merge_master.py           # fetch origin, merge origin/master, commit
  python tools/dev/merge_master.py --check   # only say whether master has commits dev lacks
"""
import re
import subprocess
import sys
from pathlib import Path

# the repository the script runs in, not the one it lives in: the robot runs a copy from /tmp
ROOT = Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True).stdout.strip())
CMAKE = "sphaira/CMakeLists.txt"
CHANGELOG = "docs/dev/CHANGELOG.md"
VERSION = re.compile(r"set\(sphaira_VERSION (\d+(?:\.\d+){2,3})\)")  # robot releases add a 4th number


def git(*args, check=True):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    if check and r.returncode:
        sys.exit("git %s failed:\n%s%s" % (" ".join(args), r.stdout, r.stderr))
    return r


def show(stage, path):
    return git("show", ":%d:%s" % (stage, path)).stdout


def higher_version(ours, theirs):
    def key(text):
        parts = [int(p) for p in VERSION.search(text).group(1).split(".")]
        return tuple(parts + [0] * (4 - len(parts)))

    a, b = key(ours), key(theirs)
    return ours if a >= b else theirs


def changelog_union(base, ours, theirs):
    """Our file, plus every '## ' entry the other side added that ours lacks, put above our newest entry."""
    def entries(text):
        parts = re.split(r"(?m)^(?=## )", text)
        return parts[0], parts[1:]

    _, base_e = entries(base)
    head, our_e = entries(ours)
    _, their_e = entries(theirs)
    have = set(base_e) | set(our_e)
    new = [e if e.endswith("\n\n") else e.rstrip("\n") + "\n\n" for e in their_e if e not in have]
    return head + "".join(new + our_e)


def main():
    git("fetch", "-q", "origin")
    behind = git("rev-list", "--count", "HEAD..origin/master").stdout.strip()
    if sys.argv[1:] == ["--check"]:
        print("origin/master has %s commit(s) this branch lacks" % behind)
        return
    if behind == "0":
        print("up to date with origin/master")
        return
    r = git("merge", "--no-ff", "--no-commit", "origin/master", check=False)
    conflicts = git("diff", "--name-only", "--diff-filter=U").stdout.split()
    if r.returncode and not conflicts:
        sys.exit("merge refused (uncommitted changes in the same files?):\n" + r.stdout + r.stderr)
    other = [c for c in conflicts if c not in (CMAKE, CHANGELOG)]
    if other:
        git("merge", "--abort")
        sys.exit("conflicts outside the version and CHANGELOG, merge by hand: " + ", ".join(other))
    if CMAKE in conflicts:
        text = (ROOT / CMAKE).read_text(encoding="utf-8")
        hunk = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
        # only the version line may conflict; git merged the rest of the file already
        for m in hunk.finditer(text):
            if not (re.fullmatch(VERSION.pattern + r"\s*", m.group(1)) and re.fullmatch(VERSION.pattern + r"\s*", m.group(2))):
                git("merge", "--abort")
                sys.exit(CMAKE + " conflicts beyond the version line, merge by hand")
        text = hunk.sub(lambda m: higher_version(m.group(1), m.group(2)), text)
        (ROOT / CMAKE).write_text(text, encoding="utf-8", newline="\n")
    if CHANGELOG in conflicts:
        (ROOT / CHANGELOG).write_text(changelog_union(show(1, CHANGELOG), show(2, CHANGELOG), show(3, CHANGELOG)),
                                      encoding="utf-8", newline="\n")
    git("add", CMAKE, CHANGELOG)
    git("commit", "-q", "--no-edit")
    print("merged origin/master (%s commit(s)); resolved: %s" % (behind, ", ".join(conflicts) or "nothing"))


if __name__ == "__main__":
    main()
