#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

find_root() {
    local here="$1"
    while :; do
        if [[ -f "$here/Makefile" && -f "$here/boot/bios/boot.S" && -f "$here/tools/qbuild/qbuild" ]]; then
            printf '%s\n' "$here"
            return 0
        fi
        [[ "$here" == "/" ]] && break
        here="$(dirname -- "$here")"
    done
    return 1
}
ROOT="$(find_root "$SCRIPT_DIR" || find_root "$(dirname -- "$SCRIPT_DIR")" || true)"
if [[ -z "$ROOT" ]]; then
    echo "[FAIL] Cannot locate qmjyOS root. Extract this package under the repository root." >&2
    exit 1
fi
cd "$ROOT"
printf '[PHASE 2 BOOT] Project root: %s\n' "$ROOT"

python3 - "$SCRIPT_DIR" <<'PY'
from pathlib import Path
from datetime import datetime
import os
import re
import shutil
import tempfile
import sys

ROOT = Path.cwd()
PAYLOAD = Path(sys.argv[1])
BOOT = "boot/bios/boot.S"
MAKE = "Makefile"
TEST = "tests/boot/test_bios_nxe.py"
DOC = "docs/architecture/BOOT_CPU_FEATURES.md"
OLD = '''    # Enable EFER.NXE only when CPUID reports NX support.
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb 2f
    mov $0x80000001, %eax
    cpuid
    bt $20, %edx
    jnc 2f
    mov $0xC0000080, %ecx
    rdmsr
    bts $11, %eax
    wrmsr
2:
'''
MARKER = '''    /* RING3_NXE_CPU_CHECK: detect extended CPUID NX support */
'''
RECIPE_ANCHOR = "\tbash tools/verify_boot.sh\n"
RECIPE_NEW = "\tpython3 tests/boot/test_bios_nxe.py\n" + RECIPE_ANCHOR


def fail(message):
    raise SystemExit("[FAIL] " + message + "; no project files were changed.")

for name in (BOOT, MAKE):
    if not (ROOT / name).is_file():
        fail(f"missing required source: {name}")
for name in (TEST, DOC):
    if not (PAYLOAD / name).is_file():
        fail(f"package payload missing: {name}")

boot_bytes = (ROOT / BOOT).read_bytes()
make_bytes = (ROOT / MAKE).read_bytes()
try:
    boot = boot_bytes.decode("utf-8")
    make = make_bytes.decode("utf-8")
except UnicodeDecodeError:
    fail("boot source or Makefile is not UTF-8")

old_count = boot.count(OLD)
marker_count = boot.count(MARKER)
if old_count == 1 and marker_count == 1:
    updated_boot = boot.replace(OLD, "", 1)
elif old_count == 0 and marker_count == 1:
    # Already normalized: retain the single robust feature-detection block.
    updated_boot = boot
else:
    fail(f"NXE duplicate anchor mismatch (duplicate_block={old_count}, robust_block={marker_count})")

if make.count(RECIPE_NEW) == 1:
    updated_make = make
elif make.count(RECIPE_ANCHOR) == 1:
    updated_make = make.replace(RECIPE_ANCHOR, RECIPE_NEW, 1)
else:
    fail("Makefile test-boot verification anchor mismatch")

payload = {}
for name in (TEST, DOC):
    data = (PAYLOAD / name).read_bytes()
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        fail(f"payload is not UTF-8: {name}")
    payload[name] = data

# The resulting assembly must retain one NX detection, one NXE-on path,
# one NXE-off path, and the original CR0.WP/PG transition.
feature_queries = list(re.finditer(r"(?m)^\s*movl? \$0x80000000, %eax$", updated_boot))
if len(feature_queries) != 1:
    fail("expected exactly one extended CPUID maximum-leaf query")
if updated_boot.count("btl $20, %edx") != 1:
    fail("expected exactly one NX feature-bit test")
if updated_boot.count("orl $((1 << 8) | (1 << 11)), %eax") != 1:
    fail("expected exactly one LME+NXE enable path")
if updated_boot.count("orl $(1 << 8), %eax") != 1:
    fail("expected exactly one LME-only fallback path")
if "or $0x80010000, %eax" not in updated_boot:
    fail("CR0.PG and CR0.WP enable operation missing")
feature_position = feature_queries[0].start()
if updated_boot.index("mov %eax, boot_mb_magic") > feature_position:
    fail("Multiboot EAX must be saved before CPUID clobbers it")
if updated_boot.index("mov %ebx, boot_mb_info") > feature_position:
    fail("Multiboot EBX must be saved before CPUID clobbers it")

changed = []
if updated_boot.encode() != boot_bytes:
    changed.append(BOOT)
if updated_make.encode() != make_bytes:
    changed.append(MAKE)
created = [name for name in (TEST, DOC) if not (ROOT / name).exists()]
for name, data in payload.items():
    target = ROOT / name
    if target.exists() and target.read_bytes() != data:
        fail(f"destination already exists with different content: {name}")

if not changed and not created:
    print("[ALREADY] duplicate NXE check removed; static regression test and documentation present")
    raise SystemExit(0)

stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
backup_dir = ROOT / "build/repair-backups/phase2-boot-nxe" / stamp
backup_dir.mkdir(parents=True, exist_ok=False)
for name in changed:
    src = ROOT / name
    dst = backup_dir / (name + ".before-phase2-boot-nxe")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    if dst.read_bytes() != src.read_bytes():
        fail(f"backup verification failed: {name}")
    print(f"[BACKUP] {dst.relative_to(ROOT)}")

updates = {}
if BOOT in changed:
    updates[ROOT / BOOT] = (updated_boot.encode(), (ROOT / BOOT).stat().st_mode & 0o777)
if MAKE in changed:
    updates[ROOT / MAKE] = (updated_make.encode(), (ROOT / MAKE).stat().st_mode & 0o777)
for name in created:
    mode = 0o755 if name == TEST else 0o644
    updates[ROOT / name] = (payload[name], mode)

staged = {}
committed = []
changed_set = set(changed)
try:
    for target, (data, mode) in updates.items():
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp_name = tempfile.mkstemp(prefix="." + target.name + ".phase2-boot.", dir=str(target.parent))
        tmp = Path(tmp_name)
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(tmp, mode)
        staged[target] = tmp
    for target, tmp in staged.items():
        os.replace(tmp, target)
        committed.append(target)
        print(f"[WRITE] {target.relative_to(ROOT)}")
except Exception:
    # Restore replaced project files and remove newly-created payload files.
    for target in reversed(committed):
        relative = target.relative_to(ROOT).as_posix()
        if relative in changed_set:
            backup = backup_dir / (relative + ".before-phase2-boot-nxe")
            if backup.is_file():
                shutil.copy2(backup, target)
        else:
            target.unlink(missing_ok=True)
    raise
finally:
    for tmp in staged.values():
        tmp.unlink(missing_ok=True)

print("[PASS] NXE duplicate-check cleanup applied; existing feature fallback preserved")
print(f"[BACKUP_DIR] {backup_dir.relative_to(ROOT)}")
PY

python3 tests/boot/test_bios_nxe.py
mkdir -p build/tests/boot
clang --target=i386-unknown-none -m32 -ffreestanding -fno-pic -fno-pie -nostdlib -Wall -O2 -g -c boot/bios/boot.S -o build/tests/boot/boot-nxe.o
file build/tests/boot/boot-nxe.o
printf '[PASS] BIOS bootstrap assembly compiled.\n'
