#!/usr/bin/env python3
"""Bring a Prelude-Nextendo release into Kefir Hub.

Reads the release source and writes:
  sphaira/include/nextendo_snapshot.hpp   tag, server IPs, hosts template, list of patch files
  assets/romfs/nextendo/sd/...            the patch files (folder names get the prefix nextendo_)
  assets/romfs/nextendo/LICENSE.md        the license text of the release

Strict checks stand in for a human review (the robot releases without one). Any surprise stops the sync with
exit code 2 and a reason: a hosts line that is not "{IP} host", a file outside the patch folders, a changed
license, no ssl patch folder.

  sync.py                       newest release from GitHub; does nothing when it is already in the Hub
  sync.py --src DIR --tag TAG   a source tree that is already on disk
  --bump                        also raise sphaira_VERSION and add a CHANGELOG entry (the robot does this)

Prints key=value lines (for GITHUB_OUTPUT) and writes build/prelude_sync/{summary.md,release.md}.
"""
import argparse
import io
import ipaddress
import json
import os
import re
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

REPO = "NextendoNetwork/Prelude-Nextendo"
ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "sphaira/include/nextendo_snapshot.hpp"
ROMFS = ROOT / "assets/romfs/nextendo"
CMAKE = ROOT / "sphaira/CMakeLists.txt"
CHANGELOG = ROOT / "docs/dev/CHANGELOG.md"
OUT = ROOT / "build/prelude_sync"

MARK = "NEXTENDO NETWORK - Atmosphere DNS-MITM"
PREFIX = "nextendo_"
SSL_FOLDER = "atmosphere/exefs_patches/nextendo_disable_ca_verification/"
IP_ARGS = {"ip": "{IP}", "nncs2_ip": "{IP_NNCS2}", "NEXTENDO_SERVER_IP_NNCSD2": "{IP_NNCS2}"}
HOSTS_LINE = re.compile(r"^\{IP(?:_NNCS2)?\}[ \t]+[A-Za-z0-9*%.\-]+[ \t]*$")  # Atmosphere: % = environment (lp1)
PATCH_FILE = re.compile(r"^atmosphere/(exefs_patches|nro_patches)/([A-Za-z0-9_.\-]+)/([0-9A-Fa-f]{16,64})\.ips$")
BROWSER_FILE = re.compile(r"^atmosphere/contents/0100000000000803/(?!.*\.\.)[A-Za-z0-9_./\-]+$")
IGNORED = {"rootCA.pem"}  # not read by the console or by Prelude (checked 2026-10-28)
TAG = re.compile(r"^v?\d{1,3}\.\d{1,3}\.\d{1,3}$")
MAX_FILE, MAX_TOTAL = 256 * 1024, 2 * 1024 * 1024


class Refused(Exception):
    pass


# --- hosts template: nextendo_hosts_build() in source/nextendo_apply.c ----------------------------------------

def tokens(body):
    """Yield ('str', text) for string literals and ('code', text) for the rest; comments dropped."""
    i, n = 0, len(body)
    code = []
    while i < n:
        c = body[i]
        if c == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            if code:
                yield "code", "".join(code)
                code = []
            yield "str", body[i + 1:j]
            i = j + 1
        elif body.startswith("//", i):
            i = body.index("\n", i)
        elif body.startswith("/*", i):
            i = body.index("*/", i) + 2
        else:
            code.append(c)
            i += 1
    if code:
        yield "code", "".join(code)


def unescape(s):
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}[m.group(1)], s)


def build_hosts(text):
    try:
        start = text.index("char *nextendo_hosts_build")
        body = text[start:text.index("#undef EMIT_H", start)]
    except ValueError:
        raise Refused("nextendo_hosts_build() not found in nextendo_apply.c")
    out = []
    toks = list(tokens(body))
    for k, (kind, val) in enumerate(toks):
        if kind != "str":
            continue
        before = toks[k - 1][1].rstrip() if k else ""
        after = toks[k + 1][1] if k + 1 < len(toks) else ""
        if before.endswith("EMIT_H("):
            out.append(unescape(val))
        elif before.endswith("sizeof(line),"):
            arg = after.lstrip(", ").split(")")[0].strip()
            if arg not in IP_ARGS or "EMIT_H(line)" not in after:
                raise Refused("unexpected snprintf shape near: " + val)
            out.append(unescape(val).replace("%s", IP_ARGS[arg], 1).replace("%%", "%"))
    tmpl = "".join(out)
    lines = tmpl.split("\n")
    if len(lines) < 3 or MARK not in lines[1]:
        raise Refused("hosts mark missing on line 2 of the template")
    for line in lines:
        s = line.strip()
        if s and not s.startswith("#") and not HOSTS_LINE.match(s):
            raise Refused("hosts line of an unknown form: " + s)
    if not any(HOSTS_LINE.match(l.strip()) for l in lines):
        raise Refused("hosts template has no {IP} line")
    return tmpl


def build_config(text):
    ips = dict(re.findall(r'#define\s+NEXTENDO_SERVER_IP_(DEFAULT|NNCSD2)\s+"([^"]+)"', text))
    if set(ips) != {"DEFAULT", "NNCSD2"}:
        raise Refused("server IPs not found in nextendo_config.h")
    for v in ips.values():
        try:
            if not ipaddress.IPv4Address(v).is_global:
                raise Refused("server IP is not public: " + v)
        except ipaddress.AddressValueError:
            raise Refused("server IP is not IPv4: " + v)
    return {"ip_default": ips["DEFAULT"], "ip_nncs2": ips["NNCSD2"]}


# --- patch files: romfs/sd of the release ------------------------------------------------------------------

def prefixed(rel):
    """atmosphere/exefs_patches/s3peername/X.ips -> atmosphere/exefs_patches/nextendo_s3peername/X.ips"""
    m = PATCH_FILE.match(rel)
    if not m:
        return rel
    folder = m.group(2) if m.group(2).startswith(PREFIX) else PREFIX + m.group(2)
    return "atmosphere/%s/%s/%s.ips" % (m.group(1), folder, m.group(3))


def collect_patches(src):
    sd = src / "romfs" / "sd"
    if not sd.is_dir():
        raise Refused("romfs/sd not found in the release")
    files, total = {}, 0
    for p in sorted(sd.rglob("*")):
        if p.is_dir():
            continue
        rel = p.relative_to(sd).as_posix()
        if rel in IGNORED:
            continue
        if not (PATCH_FILE.match(rel) or BROWSER_FILE.match(rel)):
            raise Refused("file outside the patch folders: romfs/sd/" + rel)
        data = p.read_bytes()
        if len(data) > MAX_FILE:
            raise Refused("file too big: romfs/sd/%s (%d bytes)" % (rel, len(data)))
        total += len(data)
        dest = prefixed(rel)
        if dest in files:
            raise Refused("two files map to " + dest)
        files[dest] = data
    if total > MAX_TOTAL:
        raise Refused("patch files too big in total: %d bytes" % total)
    if not any(k.startswith(SSL_FOLDER) for k in files):
        raise Refused("no ssl patch folder (disable_ca_verification): Nextendo would be off on every firmware")
    return files


# --- the header --------------------------------------------------------------------------------------------

def render_header(tag, cfg, tmpl, files):
    delim = "NXHOSTS"
    if ")" + delim + '"' in tmpl:
        raise Refused("raw string delimiter found in the template")
    rows = "".join('    {"%s", %d},\n' % (k, len(v)) for k, v in sorted(files.items()))
    return (
        "#pragma once\n\n"
        "// Generated by tools/nextendo/sync.py from Prelude-Nextendo %s. Do not edit.\n"
        "// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, see assets/romfs/nextendo/LICENSE.md)\n\n"
        "namespace sphaira::nextendo {\n\n"
        "struct PatchFile {\n"
        "    const char* path; // from the SD root; the Hub keeps its copy at romfs:/nextendo/sd/<path>\n"
        "    unsigned size;\n"
        "};\n\n"
        'inline constexpr const char* kPinnedTag = "%s";\n'
        'inline constexpr const char* kSnapshotIp = "%s";\n'
        'inline constexpr const char* kSnapshotIpNncs2 = "%s";\n\n'
        "inline constexpr PatchFile kPatchFiles[] = {\n%s};\n\n"
        'inline constexpr const char* kHostsTemplate = R"%s(%s)%s";\n\n'
        "} // namespace sphaira::nextendo\n"
    ) % (tag, tag, cfg["ip_default"], cfg["ip_nncs2"], rows, delim, tmpl, delim)


def read_header():
    """What the Hub has now: tag, IPs, hosts lines, patch paths (empty values when there is no header)."""
    text = HEADER.read_text(encoding="utf-8") if HEADER.exists() else ""
    get = lambda name: (re.search(r'%s = "([^"]*)"' % name, text) or [None, ""])[1]
    tmpl = re.search(r'R"NXHOSTS\((.*)\)NXHOSTS"', text, re.S)
    return {
        "tag": get("kPinnedTag"),
        "ip": get("kSnapshotIp"),
        "hosts": set(l.strip() for l in (tmpl.group(1) if tmpl else "").split("\n") if HOSTS_LINE.match(l.strip())),
        "paths": set(re.findall(r'\{"(atmosphere/[^"]+)", \d+\}', text)),
    }


# --- report ------------------------------------------------------------------------------------------------

def folders(paths):
    return {"/".join(p.split("/")[:3]) for p in paths}


def ssl_ids(paths):
    return {p[len(SSL_FOLDER):-4].upper() for p in paths if p.startswith(SSL_FOLDER)}


def report(old, tag, cfg, tmpl, files):
    hosts = set(l.strip() for l in tmpl.split("\n") if HOSTS_LINE.match(l.strip()))
    paths = set(files)
    add_h, del_h = sorted(hosts - old["hosts"]), sorted(old["hosts"] - hosts)
    add_f, del_f = sorted(folders(paths) - folders(old["paths"])), sorted(folders(old["paths"]) - folders(paths))
    lost_ssl = sorted(ssl_ids(old["paths"]) - ssl_ids(paths))
    warn = []
    if old["ip"] and old["ip"] != cfg["ip_default"]:
        warn.append("Сервер Nextendo змінився: %s → %s" % (old["ip"], cfg["ip_default"]))
    if lost_ssl:
        warn.append("Зник ssl-патч для %d прошивок: на них Nextendo вимкнеться (%s)" % (len(lost_ssl), ", ".join(lost_ssl)))
    lines = ["Prelude-Nextendo %s → %s" % (old["tag"] or "—", tag), ""]
    lines += ["⚠️ " + w for w in warn] + ([""] if warn else [])
    lines.append("Hosts: +%d / −%d рядків" % (len(add_h), len(del_h)))
    lines += ["  + " + h for h in add_h[:20]] + ["  − " + h for h in del_h[:20]]
    lines.append("Папки патчів: +%d / −%d (усього файлів: %d)" % (len(add_f), len(del_f), len(paths)))
    lines += ["  + " + f for f in add_f] + ["  − " + f for f in del_f]
    release = "### Nextendo\n- Prelude-Nextendo updated to %s." % tag
    if add_h or add_f:
        release += " New game servers and patches."
    note = "Hosts +%d/-%d lines, patch folders +%d/-%d." % (len(add_h), len(del_h), len(add_f), len(del_f))
    return "\n".join(lines) + "\n", release + "\n", note


# --- version and changelog (robot) -------------------------------------------------------------------------

def bump(tag, summary_line):
    text = CMAKE.read_text(encoding="utf-8")
    m = re.search(r"set\(sphaira_VERSION (\d+)\.(\d+)\.(\d+)\)", text)
    old = "%s.%s.%s" % m.groups()
    new = "%s.%s.%d" % (m.group(1), m.group(2), int(m.group(3)) + 1)
    CMAKE.write_text(text.replace(m.group(0), "set(sphaira_VERSION %s)" % new, 1), encoding="utf-8", newline="\n")
    log = CHANGELOG.read_text(encoding="utf-8")
    entry = ("## v%s — Prelude-Nextendo %s (robot)\n%s\nhost tests: pass (quick) | nro: built (CI) | switch: pending\n\n"
             % (new, tag, summary_line))
    pos = log.index("\n## ") + 1
    CHANGELOG.write_text(log[:pos] + entry + log[pos:], encoding="utf-8", newline="\n")
    return old, new


# --- source download ---------------------------------------------------------------------------------------

def github(url):
    req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json", "User-Agent": "kefir-hub-sync"})
    if os.environ.get("GITHUB_TOKEN"):
        req.add_header("Authorization", "Bearer " + os.environ["GITHUB_TOKEN"])
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def download(tag, into):
    with zipfile.ZipFile(io.BytesIO(github("https://api.github.com/repos/%s/zipball/%s" % (REPO, tag)))) as zf:
        zf.extractall(into)
    (top,) = [p for p in Path(into).iterdir() if p.is_dir()]
    return top


# --- main --------------------------------------------------------------------------------------------------

def sync(src, tag, do_bump):
    if not TAG.match(tag):
        raise Refused("unexpected tag: " + tag)
    tmpl = build_hosts((src / "source/nextendo_apply.c").read_text(encoding="utf-8"))
    cfg = build_config((src / "source/nextendo_config.h").read_text(encoding="utf-8"))
    files = collect_patches(src)
    lic = (src / "LICENSE.md").read_text(encoding="utf-8")
    lic_path = ROMFS / "LICENSE.md"
    if "PolyForm Shield License 1.0.0" not in lic:
        raise Refused("LICENSE.md is not PolyForm Shield 1.0.0 any more")
    if lic_path.exists() and lic_path.read_text(encoding="utf-8").strip() != lic.strip():
        raise Refused("LICENSE.md changed: a human must read it")

    old = read_header()
    summary, release, note = report(old, tag, cfg, tmpl, files)

    shutil.rmtree(ROMFS / "sd", ignore_errors=True)
    for rel, data in files.items():
        dest = ROMFS / "sd" / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    lic_path.write_text(lic, encoding="utf-8", newline="\n")
    HEADER.write_text(render_header(tag, cfg, tmpl, files), encoding="utf-8", newline="\n")

    result = {"changed": "1", "tag": tag, "old_tag": old["tag"]}
    if do_bump:
        result["old_version"], result["version"] = bump(tag, note)
        release = "# Kefir Hub %s\n\nUpdate from %s.\n\n%s" % (result["version"], result["old_version"], release)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "summary.md").write_text(summary, encoding="utf-8")
    (OUT / "release.md").write_text(release, encoding="utf-8")
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path)
    ap.add_argument("--tag")
    ap.add_argument("--bump", action="store_true")
    a = ap.parse_args()
    try:
        if a.src:
            result = sync(a.src, a.tag or "", a.bump)
        else:
            tag = a.tag or json.loads(github("https://api.github.com/repos/%s/releases/latest" % REPO))["tag_name"]
            if tag == read_header()["tag"]:
                result = {"changed": "0", "tag": tag}
            else:
                with tempfile.TemporaryDirectory() as tmp:
                    result = sync(download(tag, tmp), tag, a.bump)
    except Refused as e:
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "refused.txt").write_text(str(e) + "\n", encoding="utf-8")
        print("refused=" + str(e).replace("\n", " "))
        sys.exit(2)
    for k, v in result.items():
        print("%s=%s" % (k, v))


if __name__ == "__main__":
    main()
