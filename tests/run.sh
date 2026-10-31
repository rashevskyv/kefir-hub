#!/bin/sh
# Everything here runs on the host -- no Switch, no devkitPro, no build dir.
# The on-device build is its own check; these cover the parts that can be run.
#
#     tests/run.sh            everything below
#     tests/run.sh --quick    host C++ tests + dead-symbol guard only
set -e
cd "$(dirname "$0")/.."

quick=0
[ "${1:-}" = "--quick" ] && quick=1

fail=0

echo "== host unit tests =="
pids=""
tmpdir="$(mktemp -d)"
# A test may list extra translation units, one per line, at the top of the file:
#     // LINK: sphaira/source/foo_logic.cpp
# Only libnx-free sources may be listed (they are compiled with the host g++).
for src in tests/test_*.cpp; do
    (
        out="$tmpdir/$(basename "$src" .cpp)"
        links=$(sed -n 's#^// LINK: *\([^[:space:]]*\).*#\1#p' "$src" | tr -d '\r')
        # shellcheck disable=SC2086
        g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include "$src" $links -o "$out"
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

if [ "$quick" -eq 1 ]; then
    echo
    if [ "$fail" -ne 0 ]; then
        echo "FAILED (quick)"
        exit 1
    fi
    echo "quick: green"
    exit 0
fi

echo
echo "== libhaze patch shape check =="
./tests/test_patch_libhaze.sh || fail=1

echo
echo "== ftpsrv patch shape check =="
./tests/test_patch_ftpsrv.sh || fail=1

echo
echo "== nand restore auto script contract check =="
./tests/test_nand_restore_auto_contract.sh || fail=1

echo
echo "== python contract tests =="
py_pids=""
for py in tests/test_*_contract.py; do
    (python3 "$py") &
    py_pids="$py_pids $!"
done
for pid in $py_pids; do
    wait "$pid" || fail=1
done

echo
if [ "$fail" -ne 0 ]; then
    echo "FAILED"
    exit 1
fi
echo "all green"
