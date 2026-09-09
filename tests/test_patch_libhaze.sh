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
    grep -q 'Kefir Hub/%s (HOS/%s)' "source/ptp_responder_ptp_operations.cpp"
    grep -q 'R_TRY(db.AddString(device_version));' "source/ptp_responder_ptp_operations.cpp"
    grep -q 'buf.resize(' "source/threaded_file_transfer.cpp"
    awk '/buf\.resize\(.*bytes_read\)/{r=NR} /if \(!bytes_read\)/{b=NR} END{exit (r>0 && b>0 && r<b) ? 0 : 1}' "source/threaded_file_transfer.cpp"

    # Pass 2: Verify idempotency
    cmake -P "$PATCH_SCRIPT" > "$TMPDIR/out2.log" 2>&1
    grep -q "ptp_responder_ptp_operations.cpp already patched" "$TMPDIR/out2.log"
    grep -q "ptp_responder_ptp_operations.cpp device_version already patched" "$TMPDIR/out2.log"
    grep -q "threaded_file_transfer.cpp EOF resize already patched" "$TMPDIR/out2.log"

    # Pass 3: Verify rejection of corrupted files
    cp "source/ptp_responder_ptp_operations.cpp" "$TMPDIR/ptp_ops.bak"
    cp "source/threaded_file_transfer.cpp" "$TMPDIR/tft.bak"

    echo "corrupted" > "source/ptp_responder_ptp_operations.cpp"
    if cmake -P "$PATCH_SCRIPT" > /dev/null 2>&1; then
        echo "ERROR: patch script did not fail on corrupted ptp_responder_ptp_operations.cpp!"
        exit 1
    fi
    cp "$TMPDIR/ptp_ops.bak" "source/ptp_responder_ptp_operations.cpp"

    echo "corrupted" > "source/threaded_file_transfer.cpp"
    if cmake -P "$PATCH_SCRIPT" > /dev/null 2>&1; then
        echo "ERROR: patch script did not fail on corrupted threaded_file_transfer.cpp!"
        exit 1
    fi
    cp "$TMPDIR/tft.bak" "source/threaded_file_transfer.cpp"
fi

echo "ok  libhaze_patch_check: all checks passed"
