#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

find_root() {
    local here="$1"
    while :; do
        if [[ -f "$here/Makefile" && \
              -f "$here/tools/qbuild/module_plan.py" && \
              -f "$here/config/modules.status" && \
              -f "$here/include/qmjy/types.h" ]]; then
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
    echo "[FAIL] Cannot locate qmjyOS repository root." >&2
    exit 1
fi
cd "$ROOT"

printf '[PHASE 2 MM_HEAP] Project root: %s\n' "$ROOT"

python3 - "$SCRIPT_DIR" <<'PY'
from pathlib import Path
from datetime import datetime
import os
import re
import shutil
import stat
import tempfile
import sys

ROOT = Path.cwd()
PAYLOAD = Path(sys.argv[1])
NEW_FILES = [
    "include/qmjy/heap.h",
    "mm/heap/heap.c",
    "tests/memory/heap_host_test.c",
    "docs/architecture/MM_HEAP_DESIGN.md",
]
EXISTING_FILES = [
    "Makefile",
    "tools/qbuild/module_plan.py",
    "tools/qbuild/qbuild",
    "config/modules.status",
    "tests/interfaces/interface_header_test.c",
    "kernel/core/main.c",
]


def fail(message):
    raise SystemExit("[FAIL] " + message + "; no project files were changed.")


for name in EXISTING_FILES:
    if not (ROOT / name).is_file():
        fail(f"missing required project file: {name}")

payload = {}
for name in NEW_FILES:
    source = PAYLOAD / name
    if not source.is_file():
        fail(f"package payload missing: {name}")
    data = source.read_bytes()
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        fail(f"package file is not UTF-8: {name}")
    payload[name] = data

original = {name: (ROOT / name).read_bytes() for name in EXISTING_FILES}
updated = {name: data.decode("utf-8") for name, data in original.items()}

# Refuse an unrelated implementation of the same exported symbols.
source_pattern = re.compile(r"(?m)^[ \t]*(?:static[ \t]+)?(?:[A-Za-z_]\w*[ \t*]+)+(?:qm_heap_init|kmalloc|kcalloc|kfree|qm_heap_get_stats)\s*\([^;{}]*\)\s*\{")
for source in ROOT.rglob("*.c"):
    if "build" in source.parts or ".git" in source.parts:
        continue
    rel = source.relative_to(ROOT).as_posix()
    if rel == "mm/heap/heap.c":
        continue
    try:
        if source.resolve().is_relative_to(PAYLOAD.resolve()):
            continue
    except (OSError, AttributeError):
        pass
    try:
        if source_pattern.search(source.read_text(encoding="utf-8")):
            fail(f"heap API already has a C implementation: {rel}")
    except (OSError, UnicodeDecodeError):
        continue

# QBuild source mapping. MM_HEAP is already enabled in modules.conf and
# depends on KERNEL_CORE + MM_PAGING; this adds its first source mapping.
planner = updated["tools/qbuild/module_plan.py"]
old_map = '''    "MM_PAGING": [
        ("mm/paging/paging.c", "mm/paging/paging.o"),
    ],
'''
new_map = old_map + '''    "MM_HEAP": [
        ("mm/heap/heap.c", "mm/heap/heap.o"),
    ],
'''
if new_map not in planner:
    if planner.count(old_map) != 1:
        fail("module_plan.py MM_PAGING source-map anchor mismatch")
    planner = planner.replace(old_map, new_map, 1)
updated["tools/qbuild/module_plan.py"] = planner

# The heap is source-mapped, built and host-tested, but its fixed arena and
# lack of synchronization/PMM growth keep the module experimental.
status = updated["config/modules.status"]
if "MM_HEAP=experimental" not in status:
    if status.count("MM_HEAP=planned") != 1:
        fail("config/modules.status MM_HEAP state anchor mismatch")
    status = status.replace("MM_HEAP=planned", "MM_HEAP=experimental", 1)
updated["config/modules.status"] = status

# Public interface compile check.
headers = updated["tests/interfaces/interface_header_test.c"]
include_anchor = '#include "qmjy/memory.h"\n'
heap_include = '#include "qmjy/heap.h"\n'
if heap_include not in headers:
    if headers.count(include_anchor) != 1:
        fail("interface_header_test.c memory header anchor mismatch")
    headers = headers.replace(include_anchor, include_anchor + heap_include, 1)
updated["tests/interfaces/interface_header_test.c"] = headers

# Initialize the bounded heap immediately after serial setup, before VFS,
# block storage or any potential heap consumer. The failure is diagnostic
# rather than an unconditional boot stop because current consumers do not
# yet depend on dynamic allocation.
main_source = updated["kernel/core/main.c"]
main_include_anchor = '#include "memory.h"\n'
main_heap_include = '#include "qmjy/heap.h"\n'
if main_heap_include not in main_source:
    if main_source.count(main_include_anchor) != 1:
        fail("kernel/core/main.c memory header anchor mismatch")
    main_source = main_source.replace(main_include_anchor, main_include_anchor + main_heap_include, 1)
heap_init_marker = '[MM] Kernel heap initialized (bounded early arena).\\n'
heap_init_code = '''    if (qm_heap_init() == QM_OK) {
        serial_puts("[MM] Kernel heap initialized (bounded early arena).\\n");
    } else {
        serial_puts("[MM] WARNING: kernel heap initialization failed; heap-dependent services remain disabled.\\n");
    }
'''
if heap_init_marker not in main_source:
    serial_anchor = '    serial_init();\n'
    if main_source.count(serial_anchor) != 1:
        fail("kernel/core/main.c serial_init call anchor mismatch")
    main_source = main_source.replace(serial_anchor, serial_anchor + heap_init_code, 1)
if main_heap_include not in main_source or heap_init_marker not in main_source:
    fail("kernel heap initialization integration invariant failed")
updated["kernel/core/main.c"] = main_source

# Add a separate build/tests target; do not alter existing clean rules or
# delete any diagnostic artifacts outside build/tests/heap.
makefile = updated["Makefile"]
if "# qmjyOS MM_HEAP host test" not in makefile:
    makefile += '''

# qmjyOS MM_HEAP host test
.PHONY: test-heap clean-heap-test
HOST_HEAP_TEST := build/tests/heap/heap_host_test

test-heap: $(HOST_HEAP_TEST)
>./$(HOST_HEAP_TEST)

$(HOST_HEAP_TEST): tests/memory/heap_host_test.c mm/heap/heap.c include/qmjy/heap.h | build/tests/heap
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ tests/memory/heap_host_test.c mm/heap/heap.c

build/tests/heap:
>mkdir -p $@

clean: clean-heap-test

clean-heap-test:
>rm -rf build/tests/heap
'''
updated["Makefile"] = makefile

# Make the heap test part of QBuild's single test command.
qbuild = updated["tools/qbuild/qbuild"]
heap_test_line = "        test-heap " + chr(92) + "\n"
if heap_test_line not in qbuild:
    anchor = "        test-paging " + chr(92) + "\n"
    if qbuild.count(anchor) != 1:
        fail("QBuild run_test test-paging anchor mismatch")
    qbuild = qbuild.replace(anchor, anchor + heap_test_line, 1)
updated["tools/qbuild/qbuild"] = qbuild

# Cross-file invariants before any backup/write.
checks = {
    "tools/qbuild/module_plan.py": [
        '("mm/heap/heap.c", "mm/heap/heap.o")',
    ],
    "config/modules.status": ["MM_HEAP=experimental"],
    "tests/interfaces/interface_header_test.c": ['#include "qmjy/heap.h"'],
    "kernel/core/main.c": ['#include "qmjy/heap.h"', '[MM] Kernel heap initialized (bounded early arena).\\n'],
    "Makefile": ["test-heap", "tests/memory/heap_host_test.c", "build/tests/heap"],
    "tools/qbuild/qbuild": ["test-heap"],
}
for name, needles in checks.items():
    for needle in needles:
        if needle not in updated[name]:
            fail(f"cross-file invariant missing in {name}: {needle}")

for name, data in payload.items():
    target = ROOT / name
    if target.exists() and target.read_bytes() != data:
        fail(f"destination already exists with different content: {name}")

for name in EXISTING_FILES:
    if (ROOT / name).read_bytes() != original[name]:
        fail(f"project file changed during preflight: {name}")

changed = [name for name in EXISTING_FILES if updated[name].encode() != original[name]]
created = [name for name in NEW_FILES if not (ROOT / name).exists()]
if not changed and not created:
    print("[ALREADY] MM_HEAP implementation and integration are already present.")
    raise SystemExit(0)

stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
backup_dir = ROOT / "build/repair-backups/phase2-mm-heap" / stamp
backup_dir.mkdir(parents=True, exist_ok=False)
for name in changed:
    backup = backup_dir / (name + ".before-phase2-mm-heap")
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / name, backup)
    if backup.read_bytes() != original[name]:
        fail(f"backup verification failed: {name}")
    print(f"[BACKUP] {backup.relative_to(ROOT)}")


def stage(target, data, mode):
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix="." + target.name + ".heap.", dir=str(target.parent))
    temp = Path(temp_name)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temp, mode)
        return temp
    except Exception:
        try:
            os.close(fd)
        except OSError:
            pass
        temp.unlink(missing_ok=True)
        raise


staged = {}
committed = []
try:
    for name in changed:
        target = ROOT / name
        staged[name] = stage(target, updated[name].encode(), stat.S_IMODE(target.stat().st_mode))
    for name in created:
        staged[name] = stage(ROOT / name, payload[name], 0o644)

    for name in changed:
        os.replace(staged[name], ROOT / name)
        committed.append(name)
    for name in created:
        os.replace(staged[name], ROOT / name)
        committed.append(name)
except Exception as exc:
    for name in reversed(committed):
        target = ROOT / name
        if name in original:
            restore = backup_dir / (name + ".before-phase2-mm-heap")
            shutil.copy2(restore, target)
        else:
            target.unlink(missing_ok=True)
    for temp in staged.values():
        temp.unlink(missing_ok=True)
    fail(f"atomic commit failed and committed paths were restored: {exc}")

print("[PASS] MM_HEAP source and build integration installed.")
print(f"[BACKUP DIR] {backup_dir.relative_to(ROOT)}")
PY

printf '\n[PHASE 2 MM_HEAP] Running source-level validation.\n'
bash -n "$SCRIPT_DIR/apply_phase2_heap.sh"
python3 - <<'PY_CHECK'
from pathlib import Path
compile(Path("tools/qbuild/module_plan.py").read_text(encoding="utf-8"),
        "tools/qbuild/module_plan.py", "exec")
print("QBuild module planner syntax: PASS")
PY_CHECK
python3 tools/qbuild/module_plan.py --check
make test-heap
make test-interfaces

printf '\n[PHASE 2 MM_HEAP] Compiling freestanding x86_64 object.\n'
clang --target=x86_64-unknown-none -m64 -mno-red-zone -mno-sse -mno-mmx \
    -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib \
    -Wall -Wextra -Werror -O2 -Iinclude \
    -c mm/heap/heap.c -o build/tests/heap/heap.freestanding.o

printf '\n[PHASE 2 MM_HEAP] Full kernel build.\n'
make -j2 all

printf '\n[PHASE 2 MM_HEAP] Complete. Module status remains experimental until PMM/VMM-backed growth and runtime use are verified.\n'
