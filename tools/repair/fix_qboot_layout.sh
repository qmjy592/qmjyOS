#!/usr/bin/env bash
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT"

TARGET="boot/bios/qboot_stage2.S"
BUILDER="tools/qboot/build_bios_image.sh"

fail() {
    echo "[QBOOT REPAIR FAIL] $*" >&2
    exit 1
}

[ -f "$TARGET" ] || fail "Stage2 source not found"
[ -f "$BUILDER" ] || fail "QBoot builder not found"

echo "[QBOOT REPAIR] Checking original source anchors."

python3 - <<'PY'
from pathlib import Path

stage2 = Path("boot/bios/qboot_stage2.S")
builder = Path("tools/qboot/build_bios_image.sh")

source = stage2.read_text(encoding="utf-8")
build = builder.read_text(encoding="utf-8")

old = '.include "build/qboot/kernel_layout.inc"'
new = '.include "kernel_layout.inc"'

if source.count(old) != 1:
    raise SystemExit(
        "[FAIL] Stage2 include anchor is not unique; no files changed."
    )

if '-I "$BUILD_DIR"' not in build:
    raise SystemExit(
        "[FAIL] Builder does not provide BUILD_DIR include path; "
        "no files changed."
    )

print("[PASS] Stage2 include anchor verified.")
print("[PASS] Builder include directory verified.")
PY

BACKUP_DIR="build/repair-backups/qboot-layout"
mkdir -p "$BACKUP_DIR"

for file in "$TARGET"; do
    backup="$BACKUP_DIR/$(basename "$file").before-layout-fix"

    if [ -e "$backup" ]; then
        echo "[QBOOT REPAIR] Existing backup preserved: $backup"
    else
        cp -p "$file" "$backup"
        echo "[QBOOT REPAIR] Backup created: $backup"
    fi
done

python3 - <<'PY'
from pathlib import Path

path = Path("boot/bios/qboot_stage2.S")
source = path.read_text(encoding="utf-8")

old = '.include "build/qboot/kernel_layout.inc"'
new = '.include "kernel_layout.inc"'

if source.count(old) != 1:
    raise SystemExit("[FAIL] Source changed unexpectedly.")

updated = source.replace(old, new, 1)
path.write_text(updated, encoding="utf-8")

check = path.read_text(encoding="utf-8")
if new not in check or old in check:
    raise SystemExit("[FAIL] Post-write verification failed.")

print("[PASS] Stage2 now includes the layout file from BUILD_DIR.")
PY

echo
echo "[QBOOT REPAIR] Checking assembler include resolution."

grep -nF '.include "kernel_layout.inc"' "$TARGET"
grep -nF -- '-I "$BUILD_DIR"' "$BUILDER"

echo
echo "[QBOOT REPAIR PASS]"
echo "No scheduler, Ring 3, QFS driver, or IDE driver files were changed."
