#!/bin/sh
set -e

PATCH_SCRIPT="$(pwd)/sphaira/cmake/patch_libhaze.cmake"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

SRC_DIR=""
if [ -d "build/ReleaseWithInstall/_deps/libhaze-src" ]; then
    SRC_DIR="build/ReleaseWithInstall/_deps/libhaze-src"
elif [ -d "build/_deps/libhaze-src" ]; then
    SRC_DIR="build/_deps/libhaze-src"
fi

if [ -n "$SRC_DIR" ]; then
    cp -r "$SRC_DIR" "$TMPDIR/libhaze"
    cd "$TMPDIR/libhaze"

    # Pass 1: Run on currently patched / configured source
    cmake -P "$PATCH_SCRIPT" > "$TMPDIR/out1.log" 2>&1
    grep -q "data_header.length >= sizeof(PtpUsbBulkContainer)" "source/ptp_responder_ptp_operations.cpp"

    # Pass 2: Verify idempotency
    cmake -P "$PATCH_SCRIPT" > "$TMPDIR/out2.log" 2>&1
    grep -q "ptp_responder_ptp_operations.cpp already patched" "$TMPDIR/out2.log"

    # Pass 3: Verify rejection of corrupted file
    echo "corrupted" > "source/ptp_responder_ptp_operations.cpp"
    if cmake -P "$PATCH_SCRIPT" > /dev/null 2>&1; then
        echo "ERROR: patch script did not fail on corrupted file!"
        exit 1
    fi
fi

echo "ok  libhaze_patch_check: all checks passed"
