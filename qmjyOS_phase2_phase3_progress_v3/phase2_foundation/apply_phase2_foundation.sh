#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

find_root() {
    local here="$1"
    while :; do
        if [[ -f "$here/Makefile" && \
              -f "$here/tools/qbuild/module_plan.py" && \
              -f "$here/include/qmjy/boot.h" && \
              -f "$here/boot/bios/qboot_stage2.S" ]]; then
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
    echo "[FAIL] Cannot locate qmjyOS root. Extract this folder under the repository root." >&2
    exit 1
fi
cd "$ROOT"

printf '[PHASE 2 FOUNDATION] Project root: %s\n' "$ROOT"
printf '[PHASE 2 FOUNDATION] Preflight and source update.\n'

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
    "kernel/core/boot_info.c",
    "kernel/core/init_registry.c",
    "kernel/syscall/syscall.c",
    "tests/kernel/kernel_foundation_test.c",
    "tests/kernel/init_failure_test.c",
    "tests/syscall/syscall_test.c",
]
EXISTING_FILES = [
    "Makefile",
    "tools/qbuild/module_plan.py",
    "tools/qbuild/qbuild",
    "config/modules.status",
]


def fail(message):
    raise SystemExit("[FAIL] " + message + "; no project files were changed.")


for name in EXISTING_FILES:
    if not (ROOT / name).is_file():
        fail(f"missing required project file: {name}")

payload_bytes = {}
for name in NEW_FILES:
    source = PAYLOAD / name
    if not source.is_file():
        fail(f"package payload missing: {name}")
    payload_bytes[name] = source.read_bytes()
    try:
        payload_bytes[name].decode("utf-8")
    except UnicodeDecodeError:
        fail(f"package payload is not UTF-8: {name}")

original = {name: (ROOT / name).read_bytes() for name in EXISTING_FILES}
updated = {name: data.decode("utf-8") for name, data in original.items()}

# Verify that the new public APIs have declarations but no competing implementations.
for symbol in (
    "qm_boot_validate_info",
    "qm_kernel_init_register",
    "qm_kernel_init_run_level",
    "qm_kernel_init",
    "qm_kernel_shutdown",
    "qm_syscall_register",
    "qm_syscall_dispatch",
):
    definitions = 0
    pattern = re.compile(r"\b" + re.escape(symbol) + r"\s*\([^;]*?\)\s*\{", re.S)
    for source in ROOT.rglob("*.c"):
        if "build" in source.parts or ".git" in source.parts:
            continue
        try:
            if source.resolve().is_relative_to(PAYLOAD.resolve()):
                continue
        except (OSError, AttributeError):
            pass
        try:
            relative_name = source.relative_to(ROOT).as_posix()
            if relative_name in payload_bytes and source.read_bytes() == payload_bytes[relative_name]:
                continue
            if pattern.search(source.read_text(encoding="utf-8")):
                definitions += 1
        except (OSError, UnicodeDecodeError):
            continue
    if definitions:
        fail(f"{symbol} already has {definitions} C definition(s); refusing duplicate implementation")

# The uploaded/local QFS test script retains evidence; this phase does not touch it.

# QBuild source map: add boot-info validation and init registry to KERNEL_CORE.
old_kernel_map = '''    "KERNEL_CORE": [
        ("kernel/core/main.c", "kernel/core/main.o"),
    ],'''
new_kernel_map = '''    "KERNEL_CORE": [
        ("kernel/core/main.c", "kernel/core/main.o"),
        ("kernel/core/boot_info.c", "kernel/core/boot_info.o"),
        ("kernel/core/init_registry.c", "kernel/core/init_registry.o"),
    ],'''
if updated["tools/qbuild/module_plan.py"].count(new_kernel_map) == 1:
    pass
else:
    if updated["tools/qbuild/module_plan.py"].count(old_kernel_map) != 1:
        fail("module_plan.py KERNEL_CORE source-map anchor mismatch")
    updated["tools/qbuild/module_plan.py"] = updated["tools/qbuild/module_plan.py"].replace(old_kernel_map, new_kernel_map, 1)

old_sched_map = '''    "SCHEDULER": [
        ("sched/core/task.c", "sched/core/task.o"),
    ],'''
new_sched_map = '''    "SCHEDULER": [
        ("sched/core/task.c", "sched/core/task.o"),
    ],
    "SYSCALL": [
        ("kernel/syscall/syscall.c", "kernel/syscall/syscall.o"),
    ],'''
if updated["tools/qbuild/module_plan.py"].count(new_sched_map) == 1:
    pass
else:
    if updated["tools/qbuild/module_plan.py"].count(old_sched_map) != 1:
        fail("module_plan.py SCHEDULER source-map anchor mismatch")
    updated["tools/qbuild/module_plan.py"] = updated["tools/qbuild/module_plan.py"].replace(old_sched_map, new_sched_map, 1)

# The syscall registry is implemented as an early, fixed-size dispatch table;
# it is experimental until connected to the x86_64 entry/return path.
status_old = "SYSCALL=planned"
status_new = "SYSCALL=experimental"
if updated["config/modules.status"].count(status_new) == 1:
    pass
else:
    if updated["config/modules.status"].count(status_old) != 1:
        fail("config/modules.status SYSCALL state anchor mismatch")
    updated["config/modules.status"] = updated["config/modules.status"].replace(status_old, status_new, 1)

# Add the host-test entry point without changing existing test targets.
makefile = updated["Makefile"]
old_phony = ".PHONY: all run clean \\\n        test-memory test-paging test-vfs test-ramfs test-qfs test-boot \\\n        clean-memory-test module-plan"
new_phony = ".PHONY: all run clean \\\n        test-memory test-paging test-vfs test-ramfs test-qfs test-boot \\\n        test-kernel-foundation clean-memory-test module-plan"
if makefile.count(new_phony) != 1:
    if makefile.count(old_phony) != 1:
        fail("Makefile .PHONY anchor mismatch")
    makefile = makefile.replace(old_phony, new_phony, 1)

clean_anchor = "\trm -rf build/qboot\n"
clean_new = clean_anchor + "\trm -rf build/tests/kernel-foundation\n"
if makefile.count(clean_new) != 1:
    if makefile.count(clean_anchor) != 1:
        fail("Makefile clean target anchor mismatch")
    makefile = makefile.replace(clean_anchor, clean_new, 1)

if "# Phase 2 kernel foundation host tests" not in makefile:
    if ".RECIPEPREFIX := >" not in makefile:
        fail("Makefile recipe-prefix declaration missing")
    makefile += '''

# Phase 2 kernel foundation host tests
HOST_KERNEL_FOUNDATION_DIR := build/tests/kernel-foundation
HOST_KERNEL_FOUNDATION_TESTS := \\
$(HOST_KERNEL_FOUNDATION_DIR)/kernel-foundation \\
$(HOST_KERNEL_FOUNDATION_DIR)/init-failure \\
$(HOST_KERNEL_FOUNDATION_DIR)/syscall

.PHONY: test-kernel-foundation
test-kernel-foundation: $(HOST_KERNEL_FOUNDATION_TESTS)
>@set -e; for test in $(HOST_KERNEL_FOUNDATION_TESTS); do echo "===== $$test ====="; ./$$test; done

$(HOST_KERNEL_FOUNDATION_DIR):
>mkdir -p $@

$(HOST_KERNEL_FOUNDATION_DIR)/kernel-foundation: tests/kernel/kernel_foundation_test.c kernel/core/boot_info.c kernel/core/init_registry.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^

$(HOST_KERNEL_FOUNDATION_DIR)/init-failure: tests/kernel/init_failure_test.c kernel/core/boot_info.c kernel/core/init_registry.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^

$(HOST_KERNEL_FOUNDATION_DIR)/syscall: tests/syscall/syscall_test.c kernel/syscall/syscall.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^
'''
updated["Makefile"] = makefile

# Include the foundation tests in the existing unified QBuild test command.
qbuild = updated["tools/qbuild/qbuild"]
old_test_list = '''        test-qfs \\
        test-interfaces \\
        test-services \\
        test-boot'''
new_test_list = '''        test-qfs \\
        test-interfaces \\
        test-kernel-foundation \\
        test-services \\
        test-boot'''
if qbuild.count(new_test_list) != 1:
    if qbuild.count(old_test_list) != 1:
        fail("QBuild run_test target-list anchor mismatch")
    qbuild = qbuild.replace(old_test_list, new_test_list, 1)
updated["tools/qbuild/qbuild"] = qbuild

# Cross-file consistency checks before the first backup/write.
for name, needles in {
    "tools/qbuild/module_plan.py": [
        '("kernel/core/boot_info.c", "kernel/core/boot_info.o")',
        '("kernel/core/init_registry.c", "kernel/core/init_registry.o")',
        '("kernel/syscall/syscall.c", "kernel/syscall/syscall.o")',
    ],
    "config/modules.status": ["SYSCALL=experimental"],
    "Makefile": ["test-kernel-foundation", "kernel_foundation_test.c", "init_failure_test.c", "syscall_test.c"],
    "tools/qbuild/qbuild": ["test-kernel-foundation"],
}.items():
    for needle in needles:
        if needle not in updated[name]:
            fail(f"cross-file invariant missing in {name}: {needle}")

for name, data in payload_bytes.items():
    destination = ROOT / name
    if destination.exists() and destination.read_bytes() != data:
        fail(f"new destination already exists with different content: {name}")

for name in EXISTING_FILES:
    if (ROOT / name).read_bytes() != original[name]:
        fail(f"project file changed during preflight: {name}")

changed_existing = [name for name in EXISTING_FILES if updated[name].encode("utf-8") != original[name]]
new_to_create = [name for name, data in payload_bytes.items()
                 if not (ROOT / name).exists()]
if not changed_existing and not new_to_create:
    print("[ALREADY] Phase 2 kernel foundation sources and integration are already present.")
    raise SystemExit(0)

backup_root = ROOT / "build/repair-backups/phase2-kernel-foundation"
backup_root.mkdir(parents=True, exist_ok=True)
backup_dir = backup_root / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
backup_dir.mkdir(parents=False, exist_ok=False)

for name in changed_existing:
    source = ROOT / name
    backup = backup_dir / (name + ".before-phase2")
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, backup)
    if backup.read_bytes() != original[name]:
        fail(f"backup verification failed: {name}")
    print(f"[BACKUP] {backup.relative_to(ROOT)}")


def stage(target, data, mode):
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix="." + target.name + ".phase2.", dir=str(target.parent))
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
committed_existing = []
created_new = []
try:
    for name in changed_existing:
        target = ROOT / name
        staged[name] = stage(target, updated[name].encode("utf-8"), stat.S_IMODE(target.stat().st_mode))
    for name in new_to_create:
        target = ROOT / name
        staged[name] = stage(target, payload_bytes[name], 0o644)

    for name in changed_existing:
        if (ROOT / name).read_bytes() != original[name]:
            raise RuntimeError(f"source changed before commit: {name}")

    for name in changed_existing:
        os.replace(staged[name], ROOT / name)
        committed_existing.append(name)
        print(f"[UPDATED] {name}")
    for name in new_to_create:
        os.replace(staged[name], ROOT / name)
        created_new.append(name)
        print(f"[CREATED] {name}")

    for name in changed_existing:
        if (ROOT / name).read_bytes() != updated[name].encode("utf-8"):
            raise RuntimeError(f"post-write verification failed: {name}")
    for name in payload_bytes:
        if (ROOT / name).read_bytes() != payload_bytes[name]:
            raise RuntimeError(f"new-file verification failed: {name}")
except Exception as error:
    print(f"[ROLLBACK] {error}", file=sys.stderr)
    for name in reversed(committed_existing):
        target = ROOT / name
        restore = stage(target, original[name], stat.S_IMODE(target.stat().st_mode))
        os.replace(restore, target)
        print(f"[RESTORED] {name}", file=sys.stderr)
    for name in reversed(created_new):
        (ROOT / name).unlink(missing_ok=True)
        print(f"[REMOVED] {name}", file=sys.stderr)
    raise SystemExit("[FAIL] Commit failed; source changes were rolled back.")
finally:
    for temp in staged.values():
        temp.unlink(missing_ok=True)

print(f"[PHASE 2 FOUNDATION PASS] Source integration written. Backup: {backup_dir.relative_to(ROOT)}")
PY

printf '\n[PHASE 2 FOUNDATION] Static validation.\n'
bash -n tools/qbuild/qbuild
python3 - <<'PY'
from pathlib import Path
compile(Path("tools/qbuild/module_plan.py").read_text(encoding="utf-8"), "tools/qbuild/module_plan.py", "exec")
print("Python source syntax: PASS")
PY
python3 tools/qbuild/module_plan.py --check
make -j2 test-kernel-foundation
make -j2 all
printf '\n[PHASE 2 FOUNDATION PASS] Build-plan validation, host tests and full kernel build passed.\n'
printf '[PHASE 2 FOUNDATION NOTE] QEMU runtime verification is not claimed by this installer.\n'
