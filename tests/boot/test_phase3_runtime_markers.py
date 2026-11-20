#!/usr/bin/env python3
"""Ensure BIOS/UEFI runtime scripts and kernel serial markers stay in sync."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MARKERS = (
    "[Ring 3] Private PML4 and USER/RW/NX stack: ready.",
    "[Ring 3] Private CR3 activated. Entering CPL3.",
    "[Ring 3] ELF process exit and page reclamation: PASS.",
    "[Ring 3] Basic privilege transition: PASS.",
    "Starting cooperative Ring 0 scheduler.",
    "[task A] running",
    "[task B] running",
)

KERNEL = (ROOT / "kernel/core/main.c").read_text(encoding="utf-8")
SCRIPTS = (
    ROOT / "tools/qemu_boot_test.sh",
    ROOT / "tools/qboot/test_bios_boot.sh",
    ROOT / "tools/qboot/test_uefi_boot.sh",
)

for marker in MARKERS[:4]:
    if marker not in KERNEL:
        raise SystemExit(f"[FAIL] Kernel output marker missing: {marker}")

for path in SCRIPTS:
    content = path.read_text(encoding="utf-8")
    for marker in MARKERS:
        if marker not in content:
            raise SystemExit(f"[FAIL] {path.relative_to(ROOT)} does not require marker: {marker}")
    print(f"[PASS] {path.relative_to(ROOT)} requires all Phase 3/scheduler markers")

print("Phase 3 runtime marker contract: PASS")
