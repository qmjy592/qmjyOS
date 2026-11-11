#!/usr/bin/env python3
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
from datetime import datetime

root = Path(__file__).resolve().parents[2]
target = root / "Makefile"

source = target.read_text(encoding="utf-8")

anchor = (
    "test-boot: kernel.elf kernel64.elf kernel64.bin\n"
    "\tbash tools/verify_boot.sh\n"
)

if source.count(anchor) != 1:
    raise SystemExit(
        "[STOP] Makefile test-boot anchor mismatch; no changes made."
    )

if "qboot-bios:" in source or "test-qboot-bios:" in source:
    raise SystemExit(
        "[STOP] QBoot targets already exist; no changes made."
    )

if "\trm -f $(MODULE_MK)\n" not in source:
    raise SystemExit(
        "[STOP] Makefile clean anchor mismatch; no changes made."
    )

updated = source.replace(
    anchor,
    (
        "test-boot: kernel.elf kernel64.elf kernel64.bin qboot-bios\n"
        "\tbash tools/verify_boot.sh\n"
        "\tbash tools/qboot/test_bios_boot.sh\n"
    ),
    1
)

updated = updated.replace(
    "\trm -f $(MODULE_MK)\n",
    "\trm -f $(MODULE_MK)\n"
    "\trm -rf build/qboot\n",
    1
)

updated += """
# QBoot BIOS disk image and real BIOS boot validation.
.PHONY: qboot-bios test-qboot-bios

qboot-bios: kernel64.bin
>bash tools/qboot/build_bios_image.sh

test-qboot-bios: qboot-bios
>bash tools/qboot/test_bios_boot.sh
"""

backup = target.with_name(
    "Makefile.before-phase1-" +
    datetime.now().strftime("%Y%m%d-%H%M%S")
)

shutil.copy2(target, backup)

fd, temporary = tempfile.mkstemp(
    prefix=".Makefile-phase1-",
    dir=root
)

try:
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        stream.write(updated)
        stream.flush()
        os.fsync(stream.fileno())

    os.replace(temporary, target)

    subprocess.run(
        ["bash", "-n", "tools/qboot/build_bios_image.sh"],
        cwd=root,
        check=True
    )

    subprocess.run(
        ["bash", "-n", "tools/qboot/test_bios_boot.sh"],
        cwd=root,
        check=True
    )

    subprocess.run(
        ["git", "diff", "--check"],
        cwd=root,
        check=True
    )

    result = target.read_text(encoding="utf-8")

    assert "kernel64.bin qboot-bios" in result
    assert "bash tools/qboot/test_bios_boot.sh" in result
    assert "rm -rf build/qboot" in result

except Exception:
    shutil.copy2(backup, target)
    if os.path.exists(temporary):
        os.unlink(temporary)
    raise

print("[PASS] QBoot build integration")
print("[PASS] BIOS disk boot test integration")
print("[PASS] QBoot clean integration")
print("[PASS] QBoot shell syntax")
print("[PASS] git diff --check")
print("[DONE] Phase 1 patch applied.")
print("[BACKUP]", backup)
