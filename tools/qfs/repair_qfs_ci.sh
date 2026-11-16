#!/usr/bin/env bash
set -Eeuo pipefail

cd "$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)"

TARGET="tests/filesystem/qfs_crash_test.c"
TEST_SCRIPT="tools/qfs/run_crash_tests.sh"
QBUILD="tools/qbuild/qbuild"

echo
echo "================================================"
echo " qmjyOS QFS CI repair"
echo "================================================"

for f in "$TARGET" "$TEST_SCRIPT" "$QBUILD"; do
    if [[ ! -f "$f" ]]; then
        echo "[FAIL] Missing required file: $f"
        exit 1
    fi
done

# ==================================================
# PHASE 1: REVIEW
# ==================================================

echo
echo "========== PHASE 1/3: REVIEW =========="

echo "[REVIEW] Inspecting format failpoint implementation..."

grep -n -B 8 -A 14 \
    'if (op == OP_FORMAT)' "$TARGET"

OLD_COUNT=$(grep -Fxc \
    '            (void)qfs_format(&partition);' "$TARGET" || true)

NEW_COUNT=$(grep -Fxc \
    '            status = qfs_format(&partition);' "$TARGET" || true)

if [[ "$OLD_COUNT" -eq 1 && "$NEW_COUNT" -eq 0 ]]; then
    echo "[REVIEW PASS] Confirmed: qfs_format() return value is discarded."
elif [[ "$OLD_COUNT" -eq 0 && "$NEW_COUNT" -eq 1 ]]; then
    echo "[REVIEW PASS] The repair is already present."
else
    echo "[REVIEW FAIL] Unexpected source state."
    echo "Old statement count: $OLD_COUNT"
    echo "Fixed statement count: $NEW_COUNT"
    echo "No source files changed."
    exit 1
fi

# ==================================================
# PHASE 2: REPAIR
# ==================================================

echo
echo "========== PHASE 2/3: REPAIR =========="

if [[ "$OLD_COUNT" -eq 1 ]]; then
    STAMP="$(date +%Y%m%d-%H%M%S)-$$"
    BACKUP_DIR="build/repair-backups/qfs-format-failpoint/$STAMP"

    mkdir -p "$BACKUP_DIR"
    cp -p "$TARGET" "$BACKUP_DIR/qfs_crash_test.c"

    echo "[BACKUP] $BACKUP_DIR/qfs_crash_test.c"

    python3 - "$TARGET" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
old = "            (void)qfs_format(&partition);\n"
new = "            status = qfs_format(&partition);\n"

source = path.read_text(encoding="utf-8")

if source.count(old) != 1:
    raise SystemExit("[FAIL] Expected exactly one original statement.")

if new in source:
    raise SystemExit("[FAIL] Repaired statement already exists.")

source = source.replace(old, new, 1)
path.write_text(source, encoding="utf-8")

check = path.read_text(encoding="utf-8")

if check.count(new) != 1 or old in check:
    raise SystemExit("[FAIL] Repair verification failed.")

print("[REPAIR PASS] qfs_format() return value is now checked.")
PY
else
    echo "[REPAIR PASS] No source modification necessary."
fi

echo
echo "[REVIEW] Verify changed lines..."
git diff --check -- "$TARGET"
git diff -- "$TARGET"

# ==================================================
# PHASE 3: SIMULATION
# ==================================================

echo
echo "========== PHASE 3/3: SIMULATION =========="

echo
echo "----- 3.1 Shell and Python syntax -----"

sh -n "$QBUILD"
bash -n tools/verify_boot.sh
bash -n tools/qemu_boot_test.sh
bash -n "$TEST_SCRIPT"
bash -n tools/qboot/test_qfs_ide.sh

python3 -m py_compile tools/qbuild/module_plan.py

echo "[PASS] Syntax checks"

echo
echo "----- 3.2 QBuild configuration -----"

sh "$QBUILD" config \
    --arch x86_64 \
    --boot bios \
    --jobs 4

echo
echo "----- 3.3 QBuild modules and dependencies -----"

sh "$QBUILD" modules \
    --arch x86_64 \
    --boot bios

sh "$QBUILD" deps \
    --arch x86_64 \
    --boot bios

echo
echo "----- 3.4 QBuild dependency plan -----"

sh "$QBUILD" plan \
    --arch x86_64 \
    --boot bios \
    --jobs 4

echo
echo "----- 3.5 Complete kernel build -----"

sh "$QBUILD" build \
    --arch x86_64 \
    --boot bios \
    --jobs 4

echo
echo "----- 3.6 Complete QBuild test suite -----"

sh "$QBUILD" test \
    --arch x86_64 \
    --boot bios \
    --jobs 4

echo
echo "----- 3.7 QFS persistence and crash tests -----"

QFS_TEST_RUN_ID="repair-$(date +%Y%m%d-%H%M%S)-$$" \
    bash "$TEST_SCRIPT"

echo
echo "----- 3.8 QFS IDE persistence and restart test -----"

bash tools/qboot/test_qfs_ide.sh

echo
echo "----- 3.9 Standalone QEMU BIOS boot test -----"

bash tools/qemu_boot_test.sh

# ==================================================
# FINAL REVIEW
# ==================================================

echo
echo "========== FINAL REVIEW =========="

git diff --check

echo
echo "[PASS] git diff --check"

echo
echo "Changed files:"
git status --short

echo
echo "Repair diff:"
git diff -- "$TARGET"

echo
echo "================================================"
echo "[SUCCESS] REVIEW + REPAIR + SIMULATION PASSED"
echo "================================================"
echo
echo "No commit or push was performed."
echo "Please review and upload locally when ready."
