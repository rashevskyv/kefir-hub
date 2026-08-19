#!/bin/sh
# Everything here runs on the host -- no Switch, no devkitPro, no build dir.
# The on-device build is its own check; these cover the parts that can be run.
#
#     tests/run.sh
set -e
cd "$(dirname "$0")/.."

fail=0

echo "== host unit tests =="
pids=""
tmpdir="$(mktemp -d)"
for src in tests/test_*.cpp; do
    (
        out="$tmpdir/$(basename "$src" .cpp)"
        g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include "$src" -o "$out"
        "$out"
    ) &
    pids="$pids $!"
done

for pid in $pids; do
    wait "$pid" || fail=1
done
rm -rf "$tmpdir"

echo
echo "== dead symbol guard =="
python3 tests/check_dead_symbols.py || fail=1

echo
if [ "$fail" -ne 0 ]; then
    echo "FAILED"
    exit 1
fi
echo "all green"
