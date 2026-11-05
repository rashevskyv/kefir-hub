#!/bin/sh
# Rebuilds the RAR fixtures in this folder (run in WSL / Linux).
#   RAR_BIN=/path/to/rar tests/contract_fixtures/rar/make.sh
# rar 6.24 (rarlab.com rarlinux-x64-624.tar.gz) made them: rar 7 can no longer write RAR4 (-ma4).
# The content is deterministic; tests/rar/rar_reader_host.cpp builds the same bytes to compare.
set -e
RAR_BIN="${RAR_BIN:-rar}"; unset RAR # rar reads $RAR as default switches
out="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"

mkdir -p src/dir/sub
python3 - <<'EOF'
lines = "".join("Kefir Hub RAR fixture line %04d\n" % i for i in range(400))
open("src/readme.txt", "w").write(lines)
x, data = 1, bytearray()
for _ in range(12000):
    x = (x * 1103515245 + 12345) & 0xFFFFFFFF
    data.append((x >> 16) & 0xFF)
open("src/dir/data.bin", "wb").write(data)
open("src/dir/sub/empty.txt", "wb").close()
open("src/dir/тест.txt", "w", encoding="utf-8").write("привіт\n")
EOF

rm -f "$out"/*.rar "$out"/*.r[0-9][0-9]
cd src
# -m5 best, -s solid, -ma4 / -ma5 format.
"$RAR_BIN" a -idq -m5 -s -ma4 "$out/solid4.rar" readme.txt dir
"$RAR_BIN" a -idq -m5 -s -ma5 "$out/solid5.rar" readme.txt dir
"$RAR_BIN" a -idq -m5 -ma5 "$out/plain5.rar" readme.txt dir
# split sets: data.bin does not compress, so it spans the volumes.
"$RAR_BIN" a -idq -m5 -ma5 -v5k "$out/multi5.rar" readme.txt dir
"$RAR_BIN" a -idq -m5 -ma4 -vn -v5k "$out/multi4.rar" readme.txt dir
# encrypted: file data only (-p), and headers too (-hp).
"$RAR_BIN" a -idq -ma5 -psecret "$out/enc_data.rar" dir/sub/empty.txt readme.txt
"$RAR_BIN" a -idq -ma5 -hpsecret "$out/enc_head.rar" dir/sub/empty.txt
ls -l "$out"
