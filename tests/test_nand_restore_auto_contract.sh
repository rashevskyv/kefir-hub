#!/bin/sh
set -e

SCRIPT="$(pwd)/assets/romfs/tegra/nand_transfer_restore_auto.te"
if [ ! -f "$SCRIPT" ]; then
    echo "ERROR: script not found: $SCRIPT"
    exit 1
fi

check_contract() {
    f="$1"

    # 1. Exact direct-child nand_pack.txt path validation
    grep -q 'nand_pack\.txt' "$f" || return 1
    grep -q '!fsexists(packf)' "$f" || return 1
    grep -q 'parts = rel\.split("/")' "$f" || return 1
    grep -q 'parts\.len() != 5' "$f" || return 1
    grep -q 'parts\[0\] != "" || parts\[1\] != "config" || parts\[2\] != "kefir" || parts\[3\] != "nand_transfer"' "$f" || return 1
    grep -q 'pack = "sd:" + rel' "$f" || return 1

    # 2. Explicit rejection of . / .. / backslash
    grep -q 'rel\.split("\\\\")' "$f" || return 2
    grep -q 'packNameShort == "\."' "$f" || return 2
    grep -q 'packNameShort == "\.\."' "$f" || return 2

    # 3. No restore_backup directory scan and no runBackup
    if grep -q 'restore_backup' "$f"; then return 3; fi
    if grep -q 'runBackup' "$f"; then return 3; fi
    if grep -q 'backupFilesInDir' "$f"; then return 3; fi

    # 4. No menu-based/folder-scan fallback
    if grep -q 'menu(' "$f"; then return 4; fi
    if grep -q 'packNames' "$f"; then return 4; fi
    if grep -q 'packPaths' "$f"; then return 4; fi
    if grep -q 'readdir(packRoot)' "$f"; then return 4; fi

    # 5. Stale nand_restored.ok deleted before restore
    del_ok_line=$(grep -n 'delfile(pending + "/nand_restored\.ok")' "$f" | cut -d: -f1 | head -n1 || true)
    restore_call_line=$(grep -n 'restoreSave0010()' "$f" | tail -n1 | cut -d: -f1 || true)
    if [ -z "$del_ok_line" ] || [ -z "$restore_call_line" ]; then return 5; fi
    if [ "$del_ok_line" -ge "$restore_call_line" ]; then return 5; fi

    # 6. Exactly three commitRc = saveObj.commit() sites
    commit_count=$(grep -c 'commitRc = saveObj\.commit()' "$f" || true)
    if [ "$commit_count" -ne 3 ]; then return 6; fi

    # 7. Each save-specific commit site has a corresponding nonzero failure branch
    commit_fails=$(grep -c 'if (commitRc)' "$f" || true)
    if [ "$commit_fails" -ne 3 ]; then return 7; fi

    # 8. Success marker written ONLY under exact complete condition
    if ! grep -q 'if (errors == 0 && restoredSaves > 0 && restoredSaves == expectedSaves)' "$f"; then
        return 8
    fi
    write_ok_count=$(grep -c 'writefile(pending + "/nand_restored\.ok"' "$f" || true)
    if [ "$write_ok_count" -ne 1 ]; then return 8; fi

    # 9. readdir failure increments both errors and saveErrors
    if ! awk '/if \(dList\.result\)/,/\.else/' "$f" | grep -q 'errors = errors + 1'; then
        return 9
    fi
    if ! awk '/if \(dList\.result\)/,/\.else/' "$f" | grep -q 'saveErrors = saveErrors + 1'; then
        return 9
    fi

    # 10. restore_00F0 remains honored
    grep -q 'flagf = pending + "/restore_00F0"' "$f" || return 10
    grep -q 'flagText == "0"' "$f" || return 10
    grep -q 'if (!do00F0)' "$f" || return 10

    # 11. Undo status is based only on saves requested by the selected pack
    grep -q 'needsSnap0010 = fsexists(pack + "/8000000000000010")' "$f" || return 11
    grep -q 'needsSnap00F0 = do00F0 && fsexists(pack + "/80000000000000F0")' "$f" || return 11
    grep -q 'hasRelevantSnap = (needsSnap0010 && hasSnap0010) || (needsSnap00F0 && hasSnap00F0)' "$f" || return 11

    # 12. Final error text must not claim earlier successful commits were rolled back
    grep -q 'Failed saves were not committed' "$f" || return 12
    if grep -q 'No save committed if errors were encountered' "$f"; then return 12; fi

    return 0
}

# Pass 1: Verify actual script passes contract
if ! check_contract "$SCRIPT"; then
    echo "ERROR: nand_transfer_restore_auto.te failed contract check!"
    exit 1
fi

# Pass 2: Negative structural mutation tests
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

# Mutation 1: weaken path validation
sed 's/parts\.len() != 5/parts\.len() < 3/' "$SCRIPT" > "$TMPDIR/m1.te"
if check_contract "$TMPDIR/m1.te"; then
    echo "ERROR: failed to reject weakened parts.len check"
    exit 1
fi

# Mutation 2: remove . / .. rejection
sed 's/packNameShort == "\."/0/' "$SCRIPT" > "$TMPDIR/m2.te"
if check_contract "$TMPDIR/m2.te"; then
    echo "ERROR: failed to reject missing dot check"
    exit 1
fi

# Mutation 3: re-introduce runBackup / restore_backup
cat "$SCRIPT" > "$TMPDIR/m3.te"
echo 'runBackup = {}' >> "$TMPDIR/m3.te"
if check_contract "$TMPDIR/m3.te"; then
    echo "ERROR: failed to detect runBackup re-introduction"
    exit 1
fi

# Mutation 4: re-introduce menu fallback
cat "$SCRIPT" > "$TMPDIR/m4.te"
echo 'act = menu(actions, 1)' >> "$TMPDIR/m4.te"
if check_contract "$TMPDIR/m4.te"; then
    echo "ERROR: failed to detect menu re-introduction"
    exit 1
fi

# Mutation 5: remove stale nand_restored.ok deletion
sed '/delfile(pending + "\/nand_restored\.ok")/d' "$SCRIPT" > "$TMPDIR/m5.te"
if check_contract "$TMPDIR/m5.te"; then
    echo "ERROR: failed to detect missing stale ok marker deletion"
    exit 1
fi

# Mutation 6: remove one commit site
sed '0,/commitRc = saveObj\.commit()/{//d;}' "$SCRIPT" > "$TMPDIR/m6.te"
if check_contract "$TMPDIR/m6.te"; then
    echo "ERROR: failed to detect missing commit site"
    exit 1
fi

# Mutation 7: remove commit failure branch
sed '0,/if (commitRc)/{//d;}' "$SCRIPT" > "$TMPDIR/m7.te"
if check_contract "$TMPDIR/m7.te"; then
    echo "ERROR: failed to detect missing commitRc check"
    exit 1
fi

# Mutation 8: weaken success gate condition
sed 's/errors == 0 && restoredSaves > 0 && restoredSaves == expectedSaves/errors == 0/' "$SCRIPT" > "$TMPDIR/m8.te"
if check_contract "$TMPDIR/m8.te"; then
    echo "ERROR: failed to detect weakened success gate"
    exit 1
fi

# Mutation 9: remove saveErrors increment on readdir failure
awk '
    /if \(dList\.result\)/ { in_block=1 }
    in_block && /saveErrors = saveErrors \+ 1/ && !removed { removed=1; next }
    /\.else/ { in_block=0 }
    { print }
' "$SCRIPT" > "$TMPDIR/m9.te"
if check_contract "$TMPDIR/m9.te"; then
    echo "ERROR: failed to detect missing saveErrors on readdir failure"
    exit 1
fi

# Mutation 10: remove restore_00F0 handling
sed 's/flagText == "0"/0/' "$SCRIPT" > "$TMPDIR/m10.te"
if check_contract "$TMPDIR/m10.te"; then
    echo "ERROR: failed to detect broken restore_00F0 check"
    exit 1
fi

# Mutation 11: break selected-pack snapshot requirement
sed 's/needsSnap0010 = fsexists(pack + "\/8000000000000010")/needsSnap0010 = hasSnap0010/' "$SCRIPT" > "$TMPDIR/m11.te"
if check_contract "$TMPDIR/m11.te"; then
    echo "ERROR: failed to detect detached 0010 snapshot requirement"
    exit 1
fi

# Mutation 12: restore misleading all-or-nothing commit message
sed 's/Failed saves were not committed/No save committed if errors were encountered/' "$SCRIPT" > "$TMPDIR/m12.te"
if check_contract "$TMPDIR/m12.te"; then
    echo "ERROR: failed to detect misleading commit message"
    exit 1
fi

echo "ok  nand_restore_auto_contract: all checks passed"
