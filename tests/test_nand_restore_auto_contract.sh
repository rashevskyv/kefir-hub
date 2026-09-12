#!/bin/sh
set -e

SCRIPT="$(pwd)/assets/romfs/tegra/nand_transfer_restore_auto.te"
if [ ! -f "$SCRIPT" ]; then
    echo "ERROR: script not found: $SCRIPT"
    exit 1
fi

check_contract() {
    f="$1"

    # 1. Exact direct-child nand_pack.txt path validation with parser-compatible scalar assignments
    grep -q 'nand_pack\.txt' "$f" || return 1
    grep -q '!fsexists(packf)' "$f" || return 1
    grep -q 'parts = rel\.split("/")' "$f" || return 1
    grep -q 'pLen = parts\.len()' "$f" || return 1
    grep -q 'pLen != 5' "$f" || return 1
    grep -q 'p0 = parts\[0\]' "$f" || return 1
    grep -q 'p1 = parts\[1\]' "$f" || return 1
    grep -q 'p2 = parts\[2\]' "$f" || return 1
    grep -q 'p3 = parts\[3\]' "$f" || return 1
    grep -q 'p4 = parts\[4\]' "$f" || return 1
    grep -q 'if (badPackPath)' "$f" || return 1
    if grep -q '||\|&&' "$f"; then return 1; fi
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

    # 8. Success marker written ONLY under the fully computed restoreOk gate
    grep -q 'restoreOk = 0' "$f" || return 8
    grep -q 'if (errors == 0)' "$f" || return 8
    grep -q 'if (restoredSaves > 0)' "$f" || return 8
    grep -q 'if (restoredSaves == expectedSaves)' "$f" || return 8
    grep -q 'if (restoreOk)' "$f" || return 8
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
    grep -q 'needsSnap00F0 = 0' "$f" || return 11
    grep -q 'hasRelevantSnap = 0' "$f" || return 11

    # 12. Final error text must not claim earlier successful commits were rolled back
    grep -q 'Failed saves were not committed' "$f" || return 12
    if grep -q 'No save committed if errors were encountered' "$f"; then return 12; fi

    # 13. Mandatory full safety backup before first write in /config/kefir/safety_backup
    grep -q 'safetyDir = "sd:/config/kefir/safety_backup"' "$f" || return 13
    grep -q 'snapFail' "$f" || return 13
    grep -q 'Safety snapshot failed! Restore aborted to protect NAND' "$f" || return 13

    # 14. Reuse only snapshot for THIS EXACT OPERATION and NAND
    grep -q 'if (sameOperation)' "$f" || return 14
    grep -q 'snapshot\.ok' "$f" || return 14
    if grep -q 'sParts\[[0-9]\].*&&\|&&.*sParts\[[0-9]\]' "$f"; then return 14; fi

    # 15. Streaming writeFromFile used and legacy readfile buffer write eliminated
    grep -q 'saveObj\.writeFromFile(innerPath, sdPath)' "$f" || return 15
    if grep -q 'saveObj\.write(fdst, bytes)' "$f"; then return 15; fi

    # 16. Post-commit reopen and streaming readback verification with compareToFile
    grep -q 'verifyObj = readsave(bis)' "$f" || return 16
    grep -q 'verifyObj\.compareToFile' "$f" || return 16
    grep -q 'readbackFail' "$f" || return 16
    grep -q 'Readback mismatch' "$f" || return 16
    if grep -q 'verifyObj\.read(' "$f"; then return 16; fi
    if grep -q 'readfile(chk' "$f"; then return 16; fi

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
sed 's/pLen != 5/pLen < 3/' "$SCRIPT" > "$TMPDIR/m1.te"
if check_contract "$TMPDIR/m1.te"; then
    echo "ERROR: failed to reject weakened pLen check"
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

# Mutation 8: bypass success gate
sed 's/if (restoreOk)/if (1)/' "$SCRIPT" > "$TMPDIR/m8.te"
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

# Mutation 13: weaken safety backup path
sed 's/safetyDir = "sd:\/config\/kefir\/safety_backup"/safetyDir = "sd:\/tmp"/' "$SCRIPT" > "$TMPDIR/m13.te"
if check_contract "$TMPDIR/m13.te"; then
    echo "ERROR: failed to detect weakened safetyDir path"
    exit 1
fi

# Mutation 14: bypass exact snapshot matching
sed 's/if (sameOperation)/if (1)/' "$SCRIPT" > "$TMPDIR/m14.te"
if check_contract "$TMPDIR/m14.te"; then
    echo "ERROR: failed to detect bypassed snapshot match"
    exit 1
fi

# Mutation 15: revert streaming writeFromFile to byte array write
sed 's/saveObj\.writeFromFile(innerPath, sdPath)/saveObj.write(fdst, bytes)/' "$SCRIPT" > "$TMPDIR/m15.te"
if check_contract "$TMPDIR/m15.te"; then
    echo "ERROR: failed to detect writeFromFile reversion"
    exit 1
fi

# Mutation 16: remove readback verification
sed '/verifyObj = readsave(bis)/d' "$SCRIPT" > "$TMPDIR/m16.te"
if check_contract "$TMPDIR/m16.te"; then
    echo "ERROR: failed to detect missing readback verification"
    exit 1
fi

# Mutation 17: re-introduce whole-file readback
sed 's/cmpRc = verifyObj\.compareToFile(chkInner, chkSd)/chkRb = verifyObj.read(chkInner)/' "$SCRIPT" > "$TMPDIR/m17.te"
if check_contract "$TMPDIR/m17.te"; then
    echo "ERROR: failed to detect whole-file readback re-introduction"
    exit 1
fi

echo "ok  nand_restore_auto_contract: all checks passed"
