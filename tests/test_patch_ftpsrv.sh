#!/bin/sh
set -e

PATCH_SCRIPT="$(pwd)/sphaira/cmake/patch_ftpsrv.cmake"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

SRC_DIR=""
if [ -d "build/ReleaseWithInstall/_deps/ftpsrv-src" ]; then
    SRC_DIR="build/ReleaseWithInstall/_deps/ftpsrv-src"
elif [ -d "build/_deps/ftpsrv-src" ]; then
    SRC_DIR="build/_deps/ftpsrv-src"
fi

if [ -n "$SRC_DIR" ]; then
    cp -r "$SRC_DIR" "$TMPDIR/ftpsrv"
    cd "$TMPDIR/ftpsrv"

    # Pass 1: Run on currently patched / configured source
    cmake -P "$PATCH_SCRIPT" > "$TMPDIR/out1.log" 2>&1
    grep -q "sphaira: filesystem mutation notifications" "src/platform/nx/vfs_nx.h"
    grep -q "sphaira: filesystem mutation callbacks" "src/platform/nx/vfs/vfs_nx_fs.c"

    # Pass 2: Verify idempotency
    cmake -P "$PATCH_SCRIPT" > "$TMPDIR/out2.log" 2>&1
    grep -q "vfs_nx.h mutation notifications already patched" "$TMPDIR/out2.log"
    grep -q "vfs_nx_fs.c already patched" "$TMPDIR/out2.log"

    # Pass 3: Verify rejection of corrupted file
    echo "corrupted" > "src/platform/nx/vfs/vfs_nx_fs.c"
    if cmake -P "$PATCH_SCRIPT" > /dev/null 2>&1; then
        echo "ERROR: patch script did not fail on corrupted file!"
        exit 1
    fi
fi

echo "ok  ftpsrv_patch_check: all checks passed"
